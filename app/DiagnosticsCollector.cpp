#include "DiagnosticsCollector.h"
#include "AppLog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#include <memory>

namespace {
constexpr auto Version = "0.18.0";

const auto UserProbe = R"EWM(
set +e
export LC_ALL=C
export PATH=/usr/local/sbin:/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin

section() { printf '\n===== %s =====\n' "$1"; }
run() {
    printf '\n$'
    printf ' %q' "$@"
    printf '\n'
    timeout --signal=KILL 8s "$@" 2>&1
    code=$?
    printf '[exit=%s]\n' "$code"
}

section 'HOST AND PACKAGES'
run uname -a
run cat /etc/os-release
run pacman -Q waydroid lxc python dbus polkit qt6-base qt6-declarative qt6-wayland linux-zen
run waydroid --version

section 'DESKTOP SESSION (SAFE FIELDS ONLY)'
printf 'XDG_SESSION_TYPE=%s\n' "${XDG_SESSION_TYPE:-unset}"
printf 'XDG_CURRENT_DESKTOP=%s\n' "${XDG_CURRENT_DESKTOP:-unset}"
printf 'WAYLAND_DISPLAY=%s\n' "${WAYLAND_DISPLAY:-unset}"
printf 'DISPLAY=%s\n' "${DISPLAY:-unset}"

section 'SYSTEMD UNIT'
run systemctl status waydroid-container.service --no-pager -l
run systemctl show waydroid-container.service --no-pager \
    -p Id -p LoadState -p ActiveState -p SubState -p Result \
    -p MainPID -p ControlPID -p ControlGroup -p ExecMainCode \
    -p ExecMainStatus -p NRestarts -p StateChangeTimestamp \
    -p ActiveEnterTimestamp -p InactiveEnterTimestamp
run systemctl cat waydroid-container.service

section 'DBUS MANAGER'
run busctl --system --timeout=3s status id.waydro.Container
run busctl --system --timeout=3s tree id.waydro.Container
run busctl --system --timeout=3s introspect id.waydro.Container /ContainerManager
run busctl --system --timeout=3s call id.waydro.Container /ContainerManager org.freedesktop.DBus.Peer Ping

section 'WAYDROID CLIENT STATUS (BOUNDED)'
run waydroid status
run waydroid log

section 'RELEVANT PROCESSES'
run sh -c "ps -eo pid,ppid,user,uid,stat,wchan:32,etimes,cgroup:48,args --sort=pid | grep -E 'waydroid|lxc-(start|stop|info)|waydroid-net|PID.*PPID' || true"
run pgrep -a -f 'waydroid|lxc-(start|stop|info)|waydroid-net'

section 'DEVICES AND BINDER'
run ls -la /dev/binder /dev/vndbinder /dev/hwbinder /dev/ashmem /dev/binderfs
run find /dev/binderfs -maxdepth 2 -printf '%M %u:%g %p -> %l\n'
run sh -c "zgrep -E 'CONFIG_ANDROID_BINDER|CONFIG_ASHMEM' /proc/config.gz 2>/dev/null || true"
run sh -c "grep -E 'binder|ashmem' /proc/filesystems /proc/misc 2>/dev/null || true"

section 'VISIBLE MOUNTS AND WAYDROID NETWORK'
run findmnt -R /var/lib/waydroid
run sh -c "findmnt -rn -o TARGET,SOURCE,FSTYPE,OPTIONS | grep -E 'waydroid|lxc|binder' || true"
run sh -c "ip -details -brief link show | grep -Ei 'waydroid|lxc|veth|NAME' || true"
run sh -c "ip route show table all | grep -Ei 'waydroid|lxc' || true"

section 'FILESYSTEM SPACE'
run df -hT / /var /var/lib/waydroid
run df -i / /var /var/lib/waydroid
)EWM";

const auto RootProbe = R"EWM(
set +e
export LC_ALL=C
export PATH=/usr/local/sbin:/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin

section() { printf '\n===== ROOT: %s =====\n' "$1"; }
run() {
    printf '\n#'
    printf ' %q' "$@"
    printf '\n'
    timeout --signal=KILL 12s "$@" 2>&1
    code=$?
    printf '[exit=%s]\n' "$code"
}

section 'WAYDROID SERVICE JOURNAL'
run journalctl -u waydroid-container.service -b --no-pager -o short-precise -n 1500

section 'RELEVANT KERNEL JOURNAL'
run sh -c "journalctl -k -b --no-pager -o short-precise -n 1800 | grep -Ei 'waydroid|lxc|binder|ashmem|veth|bridge|mount|overlay|loop|cgroup|oom|segfault|hung task|blocked for more than' || true"

section 'SYSTEMD CGROUP AND PROCESS TREE'
run systemctl status waydroid-container.service --no-pager -l
run systemctl show waydroid-container.service --no-pager \
    -p Id -p LoadState -p ActiveState -p SubState -p Result \
    -p MainPID -p ControlPID -p ControlGroup -p ExecMainCode \
    -p ExecMainStatus -p NRestarts -p StateChangeTimestamp \
    -p ActiveEnterTimestamp -p InactiveEnterTimestamp
run systemd-cgls /system.slice/waydroid-container.service --no-pager
mainpid=$(systemctl show -p MainPID --value waydroid-container.service 2>/dev/null)
if [[ "$mainpid" =~ ^[1-9][0-9]*$ ]]; then
    run pstree -alp "$mainpid"
fi

section 'LXC STATE'
run lxc-info -P /var/lib/waydroid/lxc -n waydroid -s -p -i -H
run lxc-ls -P /var/lib/waydroid/lxc -f
run lxc-checkconfig
run sh -c "find /var/lib/waydroid/lxc -maxdepth 3 -type f -printf '%M %u:%g %s %TY-%Tm-%TdT%TH:%TM:%TS %p\n' | sort"

section 'LXC AND WAYDROID LOG FILE TAILS'
while IFS= read -r file; do
    printf '\n--- %s ---\n' "$file"
    timeout --signal=KILL 4s tail -n 400 "$file" 2>&1
done < <(find /var/lib/waydroid /var/log -maxdepth 5 -type f \
    \( -iname '*waydroid*.log' -o -iname 'lxc*.log' -o -path '*/lxc/waydroid/*.log' \) \
    -size -16M 2>/dev/null | sort -u)

section 'BLOCKED PROCESS KERNEL STACKS'
for pid in $(pgrep -f 'waydroid|lxc-(start|stop|info)|waydroid-net'); do
    [ "$pid" = "$$" ] && continue
    [ -r "/proc/$pid/status" ] || continue
    printf '\n--- PID %s ---\n' "$pid"
    printf 'cmdline: '
    tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null
    printf '\n'
    grep -E '^(Name|State|Pid|PPid|TracerPid|Uid|Gid|Threads|SigPnd|ShdPnd|voluntary_ctxt_switches|nonvoluntary_ctxt_switches):' "/proc/$pid/status" 2>/dev/null
    printf 'wchan: '
    cat "/proc/$pid/wchan" 2>/dev/null
    printf '\nstack:\n'
    timeout --signal=KILL 3s cat "/proc/$pid/stack" 2>&1
    printf '\nsyscall:\n'
    cat "/proc/$pid/syscall" 2>&1
    printf '\nthread stacks:\n'
    thread_count=0
    for task in "/proc/$pid"/task/*; do
        [ -d "$task" ] || continue
        thread_count=$((thread_count + 1))
        [ "$thread_count" -le 64 ] || { printf '[thread list truncated]\n'; break; }
        tid=${task##*/}
        printf '\nTID %s comm=' "$tid"
        cat "$task/comm" 2>/dev/null
        printf 'wchan='
        cat "$task/wchan" 2>/dev/null
        printf '\n'
        timeout --signal=KILL 2s cat "$task/stack" 2>&1
    done
    printf '\nfd summary:\n'
    ls -la "/proc/$pid/fd" 2>&1 | tail -n 80
done

section 'NAMESPACES AND MOUNTS'
run sh -c "lsns -o NS,TYPE,NPROCS,PID,USER,COMMAND,PATH | grep -Ei 'waydroid|lxc|NS.*TYPE' || true"
run sh -c "findmnt -rn -o TARGET,SOURCE,FSTYPE,OPTIONS | grep -E 'waydroid|lxc|binder|overlay' || true"
run sh -c "grep -E 'waydroid|lxc|binder|overlay' /proc/self/mountinfo || true"
run losetup -a

section 'BINDER DETAILS'
run find /dev/binderfs -maxdepth 3 -printf '%M %u:%g %s %p -> %l\n'
run sh -c "grep -E 'binder|ashmem' /proc/filesystems /proc/misc /proc/modules 2>/dev/null || true"

section 'WAYDROID NETWORK DETAILS'
run ip -details link show waydroid0
run ip address show dev waydroid0
run sh -c "ip route show table all | grep -Ei 'waydroid|lxc' || true"
run sh -c "bridge link show | grep -Ei 'waydroid|lxc|veth' || true"
run sh -c "nft list ruleset | grep -i -C 4 waydroid || true"
run sh -c "iptables-save | grep -i waydroid || true"

section 'SHORT SYSCALL TRACE OF STUCK MANAGER'
mainpid=$(systemctl show -p MainPID --value waydroid-container.service 2>/dev/null)
if [[ "$mainpid" =~ ^[1-9][0-9]*$ ]] && command -v strace >/dev/null 2>&1; then
    printf 'Attaching strace to MainPID=%s for at most 6 seconds\n' "$mainpid"
    timeout --signal=INT --kill-after=2s 6s strace -f -tt -T -s 256 -p "$mainpid" 2>&1
    printf '[strace exit=%s]\n' "$?"
else
    printf 'strace unavailable or service MainPID is zero (MainPID=%s)\n' "$mainpid"
fi

section 'RECENT WAYDROID COREDUMPS'
run coredumpctl --no-pager --no-legend list waydroid
run coredumpctl --no-pager --no-legend list lxc-start
)EWM";

QString readFileTail(const QString &path, qint64 maximumBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("Could not read %1: %2\n")
            .arg(path, file.errorString());
    if (file.size() > maximumBytes)
        file.seek(file.size() - maximumBytes);
    return QString::fromUtf8(file.readAll());
}
}

DiagnosticsCollector::DiagnosticsCollector(QObject *parent)
    : QObject(parent)
{
}

void DiagnosticsCollector::collect()
{
    if (running_)
        return;

    running_ = true;
    emit runningChanged(true);
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    lastPath_ = QDir::homePath()
        + QStringLiteral("/evgenium-waydroid-mega-log-%1.txt").arg(timestamp);

    QFile output(lastPath_);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        finishCollection(false, QStringLiteral("Cannot create diagnostic file: %1")
                                      .arg(output.errorString()));
        return;
    }
    QTextStream stream(&output);
    stream << "Evgenium Waydroid Mapper MEGA diagnostics\n"
           << "collector_version=" << Version << '\n'
           << "created=" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << '\n'
           << "qt=" << qVersion() << '\n'
           << "kernel_type=" << QSysInfo::kernelType() << '\n'
           << "kernel_version=" << QSysInfo::kernelVersion() << '\n'
           << "architecture=" << QSysInfo::currentCpuArchitecture() << '\n'
           << "scope=Waydroid/EWM only; unrelated user files and full environment are not collected\n";
    output.close();

    appendSection(QStringLiteral("EWM PERSISTENT RUNTIME LOG (last 2 MiB)"),
                  readFileTail(AppLog::path(), 2 * 1024 * 1024));
    startUserCollection();
}

void DiagnosticsCollector::startUserCollection()
{
    emit progressChanged(QStringLiteral("Collecting user-side Waydroid state…"));
    runSection(QStringLiteral("USER-SIDE LIVE PROBES"), QStringLiteral("/bin/bash"),
               {QStringLiteral("-c"), QString::fromUtf8(UserProbe)}, 45000,
               [this](int, const QString &) { startRootCollection(); });
}

void DiagnosticsCollector::startRootCollection()
{
    emit progressChanged(QStringLiteral(
        "Collecting root Waydroid/LXC state… confirm the system authorization prompt."));
    const QString pkexec = QFileInfo::exists(QStringLiteral("/usr/bin/pkexec"))
        ? QStringLiteral("/usr/bin/pkexec") : QStringLiteral("pkexec");
    runSection(QStringLiteral("PRIVILEGED LIVE PROBES"), pkexec,
               {QStringLiteral("/bin/bash"), QStringLiteral("-c"),
                QString::fromUtf8(RootProbe)}, 180000,
               [this](int code, const QString &output) {
        const bool privileged = code == 0
            || output.contains(QStringLiteral("ROOT: WAYDROID SERVICE JOURNAL"));
        finishCollection(privileged,
            privileged
                ? QStringLiteral("MEGA-log saved. Attach this file to the chat.")
                : QStringLiteral("MEGA-log saved, but root data is incomplete. "
                                 "Authorization may have been cancelled."));
    });
}

void DiagnosticsCollector::runSection(
    const QString &name, const QString &program, const QStringList &arguments,
    int timeoutMs, const std::function<void(int, const QString &)> &completed)
{
    activeProcess_ = new QProcess(this);
    activeTimeout_ = new QTimer(activeProcess_);
    activeTimeout_->setSingleShot(true);
    const auto done = std::make_shared<bool>(false);

    connect(activeTimeout_, &QTimer::timeout, activeProcess_,
            [this, done] {
        if (*done || !activeProcess_)
            return;
        activeProcess_->kill();
    });
    connect(activeProcess_, &QProcess::finished, this,
            [this, name, completed, done](int code, QProcess::ExitStatus status) {
        if (*done)
            return;
        *done = true;
        if (activeTimeout_)
            activeTimeout_->stop();
        QString output = QString::fromUtf8(activeProcess_->readAllStandardOutput());
        output += QString::fromUtf8(activeProcess_->readAllStandardError());
        output += QStringLiteral("\n[collector process exit=%1 status=%2]\n")
            .arg(code).arg(static_cast<int>(status));
        appendSection(name, output);
        activeProcess_->deleteLater();
        activeProcess_ = nullptr;
        activeTimeout_ = nullptr;
        completed(code, output);
    });
    connect(activeProcess_, &QProcess::errorOccurred, this,
            [this, name, completed, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *done)
            return;
        *done = true;
        const QString output = QStringLiteral("Failed to start collector process: %1\n")
            .arg(activeProcess_->errorString());
        appendSection(name, output);
        activeProcess_->deleteLater();
        activeProcess_ = nullptr;
        activeTimeout_ = nullptr;
        completed(-1, output);
    });
    activeProcess_->start(program, arguments);
    activeTimeout_->start(timeoutMs);
}

void DiagnosticsCollector::appendSection(const QString &title, const QString &text)
{
    QFile output(lastPath_);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream stream(&output);
    stream << "\n\n================ " << title << " ================\n"
           << text;
}

void DiagnosticsCollector::finishCollection(bool privilegedDataCollected,
                                             const QString &message)
{
    if (activeProcess_ && activeProcess_->state() != QProcess::NotRunning)
        activeProcess_->kill();
    running_ = false;
    emit runningChanged(false);
    emit finished(lastPath_, privilegedDataCollected, message);
}
