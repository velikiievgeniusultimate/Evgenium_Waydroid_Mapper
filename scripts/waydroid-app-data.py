#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
from typing import Any

STORE = pathlib.Path("/var/lib/ewm-waydroid")
REGISTRY = STORE / "registry.json"
LIVE_USER_NAME = "waydroid"
SCHEMA = 1
PACKAGE_RE = re.compile(r"^[A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)+$")


class Error(RuntimeError):
    pass


def log(message: str) -> None:
    print(f"[EWM-APPDATA] {message}", flush=True)


def read_registry(uid: int) -> dict[str, Any]:
    if not REGISTRY.is_file():
        raise Error("Менеджер версий Waydroid ещё не инициализирован")
    try:
        registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    except Exception as exc:
        raise Error(f"Повреждён реестр {REGISTRY}: {exc}") from exc
    if registry.get("schema") != SCHEMA or not isinstance(registry.get("instances"), dict):
        raise Error("Неподдерживаемый реестр EWM")
    if int(registry.get("owner_uid", uid)) != uid:
        raise Error(f"Набор Waydroid принадлежит UID {registry.get('owner_uid')}")
    return registry


def instance_user_root(data_home: pathlib.Path, registry: dict[str, Any], instance: str) -> pathlib.Path:
    if instance not in registry["instances"]:
        raise Error(f"Неизвестный инстанс: {instance}")
    if instance == registry.get("active"):
        return data_home / LIVE_USER_NAME
    return data_home / "ewm-waydroid" / "instances" / instance / "user"


def package_uid_from_list(data_root: pathlib.Path, package: str) -> int | None:
    packages_list = data_root / "system" / "packages.list"
    try:
        with packages_list.open("r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                fields = line.split()
                if len(fields) >= 2 and fields[0] == package:
                    return int(fields[1])
    except (OSError, ValueError):
        pass
    return None


def package_uid_from_xml(data_root: pathlib.Path, package: str) -> int | None:
    packages_xml = data_root / "system" / "packages.xml"
    try:
        text = packages_xml.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    match = re.search(
        rf'<package\b[^>]*\bname="{re.escape(package)}"[^>]*\b(?:userId|sharedUserId)="(\d+)"',
        text,
    )
    if not match:
        match = re.search(
            rf'<package\b[^>]*\b(?:userId|sharedUserId)="(\d+)"[^>]*\bname="{re.escape(package)}"',
            text,
        )
    return int(match.group(1)) if match else None


def package_uid(user_root: pathlib.Path, package: str) -> int:
    data_root = user_root / "data"
    uid = package_uid_from_list(data_root, package)
    if uid is None:
        uid = package_uid_from_xml(data_root, package)
    if uid is None:
        raise Error(
            f"{package} не установлено в выбранном Android. "
            "Сначала установите игру на целевой версии Android и запустите её хотя бы один раз."
        )
    return uid


def run(command: list[str], check: bool = True, timeout: int | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, check=False, timeout=timeout)
    if check and result.returncode:
        raise Error(f"Команда завершилась с кодом {result.returncode}: {' '.join(command)}")
    return result


def stop_waydroid() -> None:
    log("Останавливаю Waydroid перед копированием данных…")
    waydroid = shutil.which("waydroid") or "/usr/bin/waydroid"
    try:
        run([waydroid, "container", "stop"], False, 30)
    except Exception:
        pass
    if shutil.which("systemctl"):
        try:
            run(["systemctl", "stop", "waydroid-container.service"], False, 30)
        except Exception:
            pass


def copy_tree(source: pathlib.Path, destination: pathlib.Path) -> None:
    cp = shutil.which("cp")
    if cp:
        result = subprocess.run(
            [cp, "-a", "--reflink=auto", "--", str(source), str(destination)],
            text=True,
            check=False,
        )
        if result.returncode == 0:
            return
        if destination.exists() or destination.is_symlink():
            if destination.is_dir() and not destination.is_symlink():
                shutil.rmtree(destination, ignore_errors=True)
            else:
                destination.unlink(missing_ok=True)
    shutil.copytree(source, destination, symlinks=True, copy_function=shutil.copy2)


def mapped_android_id(value: int, source_uid: int, target_uid: int) -> int | None:
    if value == source_uid:
        return target_uid
    source_app_id = source_uid % 100000
    target_app_id = target_uid % 100000
    for base in (20000, 50000):
        source_group = base + max(0, source_app_id - 10000)
        target_group = base + max(0, target_app_id - 10000)
        if value == source_group:
            return target_group
    return None


def remap_private_ownership(root: pathlib.Path, source_uid: int, target_uid: int) -> None:
    candidates = [root]
    if root.is_dir() and not root.is_symlink():
        for directory, dirs, files in os.walk(root, followlinks=False):
            base = pathlib.Path(directory)
            candidates.extend(base / name for name in dirs)
            candidates.extend(base / name for name in files)
    for path in candidates:
        try:
            stat = path.lstat()
            new_uid = mapped_android_id(stat.st_uid, source_uid, target_uid)
            new_gid = mapped_android_id(stat.st_gid, source_uid, target_uid)
            if new_uid is not None or new_gid is not None:
                os.lchown(path, new_uid if new_uid is not None else -1,
                           new_gid if new_gid is not None else -1)
        except FileNotFoundError:
            continue


def remove_path(path: pathlib.Path) -> None:
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path, ignore_errors=True)
    else:
        try:
            path.unlink()
        except FileNotFoundError:
            pass


def copy_app_data(
    data_home: pathlib.Path,
    uid: int,
    source_instance: str,
    target_instance: str,
    package: str,
) -> pathlib.Path | None:
    if os.geteuid() != 0:
        raise Error("Копирование данных приложений требует root")
    if source_instance == target_instance:
        raise Error("Источник и цель должны быть разными версиями Android")
    if not PACKAGE_RE.fullmatch(package):
        raise Error(f"Некорректное имя Android-пакета: {package}")

    registry = read_registry(uid)
    source_root = instance_user_root(data_home, registry, source_instance)
    target_root = instance_user_root(data_home, registry, target_instance)
    if not source_root.is_dir():
        raise Error(f"Пользовательские данные источника отсутствуют: {source_root}")
    if not target_root.is_dir():
        raise Error(f"Пользовательские данные цели отсутствуют: {target_root}")

    source_uid = package_uid(source_root, package)
    target_uid = package_uid(target_root, package)

    source_data = source_root / "data"
    target_data = target_root / "data"
    paths = [
        ("user/0", True),
        ("user_de/0", True),
        ("media/0/Android/data", False),
        ("media/0/Android/obb", False),
        ("media/0/Android/media", False),
    ]

    available: list[tuple[pathlib.Path, pathlib.Path, bool, pathlib.Path]] = []
    for base, private in paths:
        relative = pathlib.Path(base) / package
        source = source_data / relative
        if source.exists() or source.is_symlink():
            available.append((source, target_data / relative, private, relative))
    if not available:
        raise Error(f"Для {package} в исходном Android не найдено данных для переноса")

    stop_waydroid()

    timestamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_root = (
        data_home / "ewm-waydroid" / "app-data-backups"
        / target_instance / package / timestamp
    )
    changed: list[tuple[pathlib.Path, pathlib.Path | None]] = []

    log(
        f"Копирую {package}: {source_instance} (UID {source_uid}) "
        f"→ {target_instance} (UID {target_uid})"
    )
    try:
        for source, destination, private, relative in available:
            destination.parent.mkdir(parents=True, exist_ok=True)
            backup: pathlib.Path | None = None
            if destination.exists() or destination.is_symlink():
                backup = backup_root / relative
                backup.parent.mkdir(parents=True, exist_ok=True)
                os.replace(destination, backup)

            staging = destination.parent / (
                f".ewm-copy-{package.replace('.', '_')}-{os.getpid()}-{destination.name}"
            )
            remove_path(staging)
            try:
                copy_tree(source, staging)
                if private:
                    remap_private_ownership(staging, source_uid, target_uid)
                os.replace(staging, destination)
            except Exception:
                remove_path(staging)
                if backup is not None and backup.exists() and not destination.exists():
                    os.replace(backup, destination)
                raise
            changed.append((destination, backup))
            log(f"Перенесено: /data/{relative}")

    except Exception:
        log("Ошибка копирования; возвращаю прежние данные целевого Android…")
        for destination, backup in reversed(changed):
            remove_path(destination)
            if backup is not None and (backup.exists() or backup.is_symlink()):
                destination.parent.mkdir(parents=True, exist_ok=True)
                os.replace(backup, destination)
        raise

    if backup_root.exists():
        log(f"Резервная копия прежних данных цели: {backup_root}")
        return backup_root
    log("На целевом Android прежних данных этого приложения не было; backup не потребовался")
    return None


def parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-home", required=True)
    parser.add_argument("--uid", required=True, type=int)
    parser.add_argument("--gid", required=True, type=int)
    subparsers = parser.add_subparsers(dest="cmd", required=True)

    copy = subparsers.add_parser("copy")
    copy.add_argument("--source", required=True)
    copy.add_argument("--target", required=True)
    copy.add_argument("--package", required=True)
    return parser


def main() -> int:
    args = parser().parse_args()
    data_home = pathlib.Path(args.data_home).expanduser().resolve()
    try:
        if args.cmd == "copy":
            copy_app_data(data_home, args.uid, args.source, args.target, args.package)
        return 0
    except Exception as exc:
        print(f"[EWM-APPDATA] ERROR: {exc}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
