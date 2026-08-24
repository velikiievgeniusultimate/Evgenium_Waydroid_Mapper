#!/usr/bin/env python3
"""Apply reversible EWM device identities to an initialized Waydroid install."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile


WAYDROID_ROOT = Path("/var/lib/waydroid")
CONFIG_PATH = WAYDROID_ROOT / "waydroid.cfg"
BASE_PROP_PATH = WAYDROID_ROOT / "waydroid_base.prop"
STATE_PATH = WAYDROID_ROOT / "ewm-device-profile.json"
BACKUP_PATH = WAYDROID_ROOT / "waydroid.cfg.ewm-backup"

PROFILES: dict[str, dict[str, str]] = {
    "native": {},
    # COPG currently targets this identity for com.mobile.legends.  Deliberately
    # leave the fingerprint, Android version, ABI and Mesa GPU untouched: EWM
    # only needs to pass the game's model whitelist, not forge a whole phone.
    "poco-f5": {
        "ro.product.brand": "POCO",
        "ro.product.manufacturer": "Xiaomi",
        "ro.product.model": "23049PCD8G",
        "ro.product.device": "marble",
        "ro.product.name": "marble_global",
    },
}
# Version 0.25.0 briefly exposed a full-build spoof.  These keys remain managed
# only so 0.25.2 can remove every experimental override and restore the native
# values recorded in the version-2 state file.  Never reuse them for MLBB's
# harmless model whitelist profile.
RETIRED_MANAGED_KEYS = (
    "ro.product.first_api_level",
    "ro.product.mod_device",
    "ro.build.product",
    "ro.build.flavor",
    "ro.build.id",
    "ro.build.display.id",
    "ro.build.version.incremental",
    "ro.build.version.security_patch",
    "ro.build.type",
    "ro.build.tags",
    "ro.build.description",
    "ro.build.fingerprint",
    "ro.system.build.fingerprint",
    "ro.system_ext.build.fingerprint",
    "ro.product.build.fingerprint",
    "ro.vendor.build.fingerprint",
    "ro.odm.build.fingerprint",
    "ro.com.google.clientidbase",
)
MANAGED_KEY_ORDER = tuple(dict.fromkeys(tuple(
    key for profile_name, values in PROFILES.items()
    if profile_name != "native"
    for key in values
) + RETIRED_MANAGED_KEYS))
MANAGED_KEYS = frozenset(MANAGED_KEY_ORDER)
STATE_VERSION = 2
SECTION_RE = re.compile(r"^\s*\[([^]]+)]\s*(?:[#;].*)?$")
KEY_RE = re.compile(r"^\s*([A-Za-z0-9_.-]+)\s*=")


class ProfileError(RuntimeError):
    pass


class NeedsAuthorization(ProfileError):
    pass


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError as error:
        raise ProfileError(f"Waydroid is not initialized: {path} is missing") from error
    except PermissionError as error:
        raise NeedsAuthorization(f"Cannot read {path} without system authorization") from error


def atomic_write(path: Path, content: str, mode: int | None = None) -> None:
    stat = path.stat() if path.exists() else None
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode if mode is not None else (stat.st_mode & 0o777 if stat else 0o644))
        if stat is not None:
            os.chown(temporary, stat.st_uid, stat.st_gid)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def extract_config_values(text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    in_properties = False
    for line in text.splitlines():
        section_match = SECTION_RE.match(line)
        if section_match:
            in_properties = section_match.group(1).strip().lower() == "properties"
            continue
        if not in_properties:
            continue
        key_match = KEY_RE.match(line)
        if not key_match:
            continue
        key = key_match.group(1).lower()
        if key in MANAGED_KEYS:
            result[key] = line.split("=", 1)[1].strip()
    return result


def extract_prop_values(text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in text.splitlines():
        key_match = KEY_RE.match(line)
        if not key_match:
            continue
        key = key_match.group(1).lower()
        if key in MANAGED_KEYS:
            result[key] = line.split("=", 1)[1].strip()
    return result


def profile_lines(values: dict[str, str]) -> list[str]:
    return [f"{key} = {values[key]}\n" for key in MANAGED_KEY_ORDER if key in values]


def replace_config_values(text: str, values: dict[str, str]) -> str:
    lines = text.splitlines(keepends=True)
    output: list[str] = []
    in_properties = False
    found_properties = False
    inserted = False

    for line in lines:
        section_match = SECTION_RE.match(line.rstrip("\r\n"))
        if section_match:
            next_is_properties = section_match.group(1).strip().lower() == "properties"
            if in_properties and not next_is_properties and not inserted:
                output.extend(profile_lines(values))
                inserted = True
            in_properties = next_is_properties
            found_properties = found_properties or in_properties
            output.append(line)
            continue

        key_match = KEY_RE.match(line)
        if in_properties and key_match and key_match.group(1).lower() in MANAGED_KEYS:
            continue
        output.append(line)

    if in_properties and not inserted:
        output.extend(profile_lines(values))
        inserted = True
    if not found_properties and values:
        if output and not output[-1].endswith(("\n", "\r")):
            output[-1] += "\n"
        if output and output[-1].strip():
            output.append("\n")
        output.append("[properties]\n")
        output.extend(profile_lines(values))
    return "".join(output)


def replace_prop_values(text: str, values: dict[str, str]) -> str:
    output: list[str] = []
    for line in text.splitlines(keepends=True):
        key_match = KEY_RE.match(line)
        if key_match and key_match.group(1).lower() in MANAGED_KEYS:
            continue
        output.append(line)
    if output and not output[-1].endswith(("\n", "\r")):
        output[-1] += "\n"
    output.extend(f"{key}={values[key]}\n" for key in MANAGED_KEY_ORDER if key in values)
    return "".join(output)


def load_state(config_text: str | None = None,
               base_text: str | None = None) -> dict[str, object] | None:
    try:
        raw = json.loads(STATE_PATH.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return None
    except PermissionError as error:
        raise NeedsAuthorization("Cannot read EWM profile state without system authorization") from error
    except json.JSONDecodeError as error:
        raise ProfileError(f"Cannot read EWM profile state: {error}") from error
    version = raw.get("version")
    if version == 1:
        # Version 1 managed only the five ro.product identity keys.  Every new
        # key was therefore untouched by EWM and its current explicit value is
        # the correct native value to preserve during the one-time migration.
        # Missing keys stay missing and are removed again when Native is chosen.
        if config_text is not None:
            originals = dict(raw.get("original_config", {}))
            for key, value in extract_config_values(config_text).items():
                originals.setdefault(key, value)
            raw["original_config"] = originals
        if base_text is not None:
            originals = dict(raw.get("original_base", {}))
            for key, value in extract_prop_values(base_text).items():
                originals.setdefault(key, value)
            raw["original_base"] = originals
        raw["version"] = STATE_VERSION
    elif version != STATE_VERSION:
        raise ProfileError("Unsupported EWM device-profile state version")
    return raw


def target_values(profile: str, state: dict[str, object] | None) -> tuple[dict[str, str], dict[str, str]]:
    if profile != "native":
        values = PROFILES[profile]
        return values, values
    if state is None:
        return {}, {}
    return dict(state.get("original_config", {})), dict(state.get("original_base", {}))


def check(profile: str) -> int:
    config_text = read_text(CONFIG_PATH)
    base_text = read_text(BASE_PROP_PATH)
    state = load_state(config_text, base_text)

    if profile == "native" and state is None:
        print("Native Waydroid identity is not managed by EWM.")
        return 0
    if profile != "native" and state is None:
        print("EWM must capture the native identity before applying this profile.")
        return 10

    desired_config, desired_base = target_values(profile, state)
    matches = (extract_config_values(config_text) == desired_config
               and extract_prop_values(base_text) == desired_base)
    print(f"Device profile {profile}: {'already applied' if matches else 'needs update'}.")
    return 0 if matches else 10


def apply(profile: str) -> int:
    if os.geteuid() != 0:
        raise ProfileError("Applying a device profile requires root authorization")

    config_text = read_text(CONFIG_PATH)
    base_text = read_text(BASE_PROP_PATH)
    state = load_state(config_text, base_text)
    if profile != "native" and state is None:
        state = {
            "version": STATE_VERSION,
            "active": profile,
            "original_config": extract_config_values(config_text),
            "original_base": extract_prop_values(base_text),
        }
        atomic_write(STATE_PATH, json.dumps(state, indent=2, sort_keys=True) + "\n", 0o644)
        if not BACKUP_PATH.exists():
            shutil.copy2(CONFIG_PATH, BACKUP_PATH)
    elif state is not None:
        state["active"] = profile
        atomic_write(STATE_PATH, json.dumps(state, indent=2, sort_keys=True) + "\n", 0o644)

    desired_config, desired_base = target_values(profile, state)
    atomic_write(CONFIG_PATH, replace_config_values(config_text, desired_config))
    atomic_write(BASE_PROP_PATH, replace_prop_values(base_text, desired_base))

    if (extract_config_values(read_text(CONFIG_PATH)) != desired_config
            or extract_prop_values(read_text(BASE_PROP_PATH)) != desired_base):
        raise ProfileError("Waydroid device identity failed post-write verification")

    if profile == "native":
        STATE_PATH.unlink(missing_ok=True)
        print("Restored the native Waydroid device identity.")
    else:
        print("Applied POCO F5 (23049PCD8G) for Mobile Legends.")
    return 0


def main(arguments: list[str]) -> int:
    if len(arguments) != 2 or arguments[0] not in {"check", "apply"}:
        print("usage: device-profile.py {check|apply} {native|poco-f5}", file=sys.stderr)
        return 2
    operation, profile = arguments
    if profile not in PROFILES:
        print(f"Unknown device profile: {profile}", file=sys.stderr)
        return 2
    try:
        return check(profile) if operation == "check" else apply(profile)
    except NeedsAuthorization as error:
        print(f"EWM device profile authorization required: {error}", file=sys.stderr)
        return 10 if operation == "check" else 2
    except ProfileError as error:
        print(f"EWM device profile error: {error}", file=sys.stderr)
        return 2
    except OSError as error:
        print(f"EWM device profile I/O error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
