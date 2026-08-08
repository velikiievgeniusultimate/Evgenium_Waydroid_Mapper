#include "IntegratedView.h"

#include <QDateTime>
#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace {
constexpr auto NestedSocket = "evgenium-wayland-0";
constexpr int SessionSettleMs = 2500;
constexpr int StopSettleMs = 1200;
}

IntegratedView::IntegratedView(QObject *parent)
    : QObject(parent), engine_(new QQmlApplicationEngine(this)),
      sessionProcess_(new QProcess(this))
{
    engine_->rootContext()->setContextProperty("integratedBackend", this);

    connect(sessionProcess_, &QProcess::readyReadStandardOutput, this, [this] {
        log("session stdout: " + QString::fromUtf8(sessionProcess_->readAllStandardOutput()).trimmed());
    });
    connect(sessionProcess_, &QProcess::readyReadStandardError, this, [this] {
        log("session stderr: " + QString::fromUtf8(sessionProcess_->readAllStandardError()).trimmed());
    });
    connect(sessionProcess_, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
        log(QString("session process finished: code=%1 status=%2")
                .arg(code).arg(static_cast<int>(status)));
    });
    connect(sessionProcess_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        log(QString("session process error=%1: %2")
                .arg(static_cast<int>(error)).arg(sessionProcess_->errorString()));
    });

    log("controller created; explicit Stop is required before configuration");
}

void IntegratedView::log(const QString &message) const
{
    qInfo().noquote() << QString("[EWM %1] %2")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), message);
}

void IntegratedView::ensureCompositor()
{
    if (!engine_->rootObjects().isEmpty())
        return;
    log("loading hidden Qt Wayland compositor");
    engine_->load(QUrl(QStringLiteral("qrc:/IntegratedView.qml")));
    log(QString("compositor root objects=%1").arg(engine_->rootObjects().size()));
}

QProcessEnvironment IntegratedView::nestedEnvironment() const
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("WAYLAND_DISPLAY", NestedSocket);
    return environment;
}

void IntegratedView::stopIntegratedSession()
{
    if (busy_)
        return;

    log("USER ACTION: Stop Waydroid");
    setBusy(true);
    setReady(false);
    setConfigurationUnlocked(false);
    setWindowVisible(false);
    waitingForSurface_ = false;
    emit statusChanged("Stopping Waydroid…");

    stopSession("user-requested stop", [this] {
        setConfigurationUnlocked(true);
        setBusy(false);
        emit statusChanged("Waydroid stop command completed. Resolution is unlocked.");
        log("STATE: configuration unlocked");
    });
}

void IntegratedView::prepareAndStart(int width, int height)
{
    if (busy_)
        return;
    if (!configurationUnlocked_) {
        emit statusChanged("Press Stop Waydroid before preparing a new resolution.");
        log("prepare rejected: explicit Stop has not completed");
        return;
    }
    if (width < 320 || height < 320 || width > 7680 || height > 7680) {
        emit statusChanged("Resolution must be between 320 and 7680 pixels.");
        return;
    }

    log(QString("USER ACTION: prepare %1x%2").arg(width).arg(height));
    setConfigurationUnlocked(false);
    setReady(false);
    setWindowVisible(false);
    setBusy(true);
    ensureCompositor();
    if (engine_->rootObjects().isEmpty()) {
        failOperation("Failed to initialize the hidden integrated compositor.");
        return;
    }

    emit statusChanged("Starting the hidden configuration session…");
    startSession("configuration", [this, width, height] {
        writeResolution(width, height);
    });
}

void IntegratedView::startSession(const QString &purpose,
                                  const std::function<void()> &completed)
{
    if (sessionProcess_->state() != QProcess::NotRunning) {
        log("old local session launcher still exists; terminating it before start");
        sessionProcess_->terminate();
        if (!sessionProcess_->waitForFinished(1500))
            sessionProcess_->kill();
    }

    log(QString("START session (%1), WAYLAND_DISPLAY=%2")
            .arg(purpose, QString::fromLatin1(NestedSocket)));
    sessionProcess_->setProcessEnvironment(nestedEnvironment());
    sessionProcess_->start("waydroid", {"session", "start"});
    if (!sessionProcess_->waitForStarted(3000)) {
        failOperation("Could not start the Waydroid session process. See console log.");
        return;
    }

    emit statusChanged(QString("Waydroid %1 session started; allowing Android to settle…")
                           .arg(purpose));
    QTimer::singleShot(SessionSettleMs, this, [this, purpose, completed] {
        if (sessionProcess_->state() == QProcess::NotRunning) {
            failOperation(QString("The Waydroid %1 session exited early. See console log.")
                              .arg(purpose));
            return;
        }
        log(QString("session settle delay complete (%1)").arg(purpose));
        completed();
    });
}

void IntegratedView::writeResolution(int width, int height)
{
    emit statusChanged(QString("Applying %1 × %2 to the running configuration session…")
                           .arg(width).arg(height));
    runCommand({"prop", "set", "persist.waydroid.width", QString::number(width)},
               [this, width, height](int widthCode, const QString &) {
        if (widthCode != 0) {
            failOperation("Waydroid rejected the width property. See console log.");
            return;
        }
        runCommand({"prop", "set", "persist.waydroid.height", QString::number(height)},
                   [this, width, height](int heightCode, const QString &) {
            if (heightCode != 0) {
                failOperation("Waydroid rejected the height property. See console log.");
                return;
            }

            // Readback is diagnostic only. It is deliberately not a launch gate.
            runCommand({"prop", "get", "persist.waydroid.width"},
                       [this, width, height](int code, const QString &output) {
                log(QString("diagnostic width readback: requested=%1 code=%2 value='%3'")
                        .arg(width).arg(code).arg(output.trimmed()));
                runCommand({"prop", "get", "persist.waydroid.height"},
                           [this, height](int code, const QString &output) {
                    log(QString("diagnostic height readback: requested=%1 code=%2 value='%3'")
                            .arg(height).arg(code).arg(output.trimmed()));
                    emit statusChanged("Resolution commands completed; restarting Waydroid…");
                    stopSession("configuration restart", [this] {
                        startSession("final", [this] { requestSurface(); });
                    });
                });
            });
        });
    });
}

void IntegratedView::stopSession(const QString &purpose,
                                 const std::function<void()> &completed)
{
    log(QString("STOP session (%1)").arg(purpose));
    runCommand({"session", "stop"}, [this, purpose, completed]
               (int code, const QString &output) {
        // A non-zero code may simply mean that Waydroid was already stopped.
        // Log it, but do not reintroduce the unreliable status-text gate.
        log(QString("stop command returned for %1: code=%2 output='%3'")
                .arg(purpose).arg(code).arg(output.trimmed()));
        emit statusChanged("Stop command completed; allowing Waydroid to settle…");
        QTimer::singleShot(StopSettleMs, this, [this, purpose, completed] {
            log(QString("stop settle delay complete (%1)").arg(purpose));
            completed();
        });
    });
}

void IntegratedView::requestSurface()
{
    emit statusChanged("Final Waydroid session is up; requesting Android surface…");
    log("START show-full-ui on nested socket");
    waitingForSurface_ = true;
    runCommand({"show-full-ui"}, [this](int code, const QString &output) {
        log(QString("show-full-ui finished: code=%1 output='%2'")
                .arg(code).arg(output.trimmed()));
    }, nestedEnvironment());

    QTimer::singleShot(30000, this, [this] {
        if (busy_ && waitingForSurface_)
            failOperation("No Android surface arrived within 30 seconds. See console log.");
    });
}

void IntegratedView::surfaceReady()
{
    log("EVENT: Android xdg_toplevel surface arrived");
    if (!waitingForSurface_) {
        log("surface event ignored because no surface is currently requested");
        return;
    }
    waitingForSurface_ = false;
    setReady(true);
    setBusy(false);
    emit statusChanged("Android is ready. Open Integrated Android.");
    log("STATE: ready; integrated window unlocked");
}

void IntegratedView::openIntegratedWindow()
{
    log("USER ACTION: Open Integrated Android");
    if (!ready_ || busy_) {
        emit statusChanged("Android has not finished preparing yet.");
        return;
    }
    setWindowVisible(true);
}

void IntegratedView::hideIntegratedWindow()
{
    log("integrated window hidden");
    setWindowVisible(false);
}

void IntegratedView::runCommand(const QStringList &arguments,
                                const std::function<void(int, const QString &)> &completed,
                                const QProcessEnvironment &environment)
{
    auto *command = new QProcess(this);
    command->setProcessEnvironment(environment);
    const QString printable = "waydroid " + arguments.join(' ');
    const auto finished = std::make_shared<bool>(false);
    log("COMMAND start: " + printable);
    connect(command, &QProcess::errorOccurred, this,
            [this, command, printable, completed, finished](QProcess::ProcessError error) {
        log(QString("COMMAND error: %1 error=%2 message='%3'")
                .arg(printable).arg(static_cast<int>(error)).arg(command->errorString()));
        if (error == QProcess::FailedToStart && !*finished) {
            *finished = true;
            const QString output = command->errorString();
            command->deleteLater();
            completed(-1, output);
        }
    });
    connect(command, &QProcess::finished, this,
            [this, command, completed, printable, finished]
            (int exitCode, QProcess::ExitStatus status) {
        if (*finished)
            return;
        *finished = true;
        const QString output = QString::fromUtf8(command->readAllStandardOutput())
                             + QString::fromUtf8(command->readAllStandardError());
        log(QString("COMMAND finish: %1 code=%2 status=%3 output='%4'")
                .arg(printable).arg(exitCode).arg(static_cast<int>(status)).arg(output.trimmed()));
        command->deleteLater();
        completed(exitCode, output);
    });
    command->start("waydroid", arguments);
    QTimer::singleShot(30000, this, [this, command, printable, finished] {
        if (*finished)
            return;
        log("COMMAND timeout after 30s, killing: " + printable);
        command->kill();
    });
}

void IntegratedView::failOperation(const QString &status)
{
    log("FAIL: " + status);
    waitingForSurface_ = false;
    setReady(false);
    setWindowVisible(false);
    setBusy(false);
    emit statusChanged(status);
}

void IntegratedView::setBusy(bool busy)
{
    if (busy_ == busy)
        return;
    busy_ = busy;
    emit busyChanged();
}

void IntegratedView::setReady(bool ready)
{
    if (ready_ == ready)
        return;
    ready_ = ready;
    emit readyChanged();
}

void IntegratedView::setConfigurationUnlocked(bool unlocked)
{
    if (configurationUnlocked_ == unlocked)
        return;
    configurationUnlocked_ = unlocked;
    emit configurationUnlockedChanged();
}

void IntegratedView::setWindowVisible(bool visible)
{
    if (windowVisible_ == visible)
        return;
    windowVisible_ = visible;
    emit windowVisibleChanged();
}
