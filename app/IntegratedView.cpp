#include "IntegratedView.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QProcess>
#include <QTimer>
#include <QUrl>

namespace {
constexpr auto NestedSocket = "evgenium-wayland-0";
}

IntegratedView::IntegratedView(QObject *parent)
    : QObject(parent), engine_(new QQmlApplicationEngine(this)),
      sessionProcess_(new QProcess(this))
{
    engine_->rootContext()->setContextProperty("integratedBackend", this);
}

void IntegratedView::ensureCompositor()
{
    if (engine_->rootObjects().isEmpty())
        engine_->load(QUrl(QStringLiteral("qrc:/IntegratedView.qml")));
}

QProcessEnvironment IntegratedView::nestedEnvironment() const
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("WAYLAND_DISPLAY", NestedSocket);
    return environment;
}

void IntegratedView::prepareAndStart(int width, int height)
{
    if (busy_)
        return;
    if (width < 320 || height < 320 || width > 7680 || height > 7680) {
        emit statusChanged("Resolution must be between 320 and 7680 pixels.");
        return;
    }

    setReady(false);
    setWindowVisible(false);
    setBusy(true);
    emit statusChanged("Checking that Waydroid is fully stopped…");

    runCommand({"status"}, [this, width, height](int exitCode, const QString &output) {
        if (exitCode != 0 || !output.contains("Session: STOPPED")) {
            failOperation("Waydroid is still running. Press Stop Waydroid before changing resolution.");
            return;
        }
        ensureCompositor();
        if (engine_->rootObjects().isEmpty()) {
            failOperation("Failed to initialize the hidden integrated compositor.");
            return;
        }
        emit statusChanged("Starting a hidden configuration session…");
        startSession([this, width, height] {
            writeResolution(width, height);
        });
    });
}

void IntegratedView::writeResolution(int width, int height)
{
    emit statusChanged(QString("Saving resolution %1 × %2…").arg(width).arg(height));
    runCommand({"prop", "set", "persist.waydroid.width", QString::number(width)},
               [this, width, height](int code, const QString &) {
        if (code != 0) {
            failOperation("Failed to save the Waydroid width.");
            return;
        }
        runCommand({"prop", "set", "persist.waydroid.height", QString::number(height)},
                   [this, width, height](int code, const QString &) {
            if (code != 0) {
                failOperation("Failed to save the Waydroid height.");
                return;
            }
            emit statusChanged("Verifying the saved resolution…");
            runCommand({"prop", "get", "persist.waydroid.width"},
                       [this, width, height](int code, const QString &output) {
                if (code != 0 || output.trimmed() != QString::number(width)) {
                    failOperation("Waydroid did not confirm the requested width.");
                    return;
                }
                runCommand({"prop", "get", "persist.waydroid.height"},
                           [this, height](int code, const QString &output) {
                    if (code != 0 || output.trimmed() != QString::number(height)) {
                        failOperation("Waydroid did not confirm the requested height.");
                        return;
                    }
                    emit statusChanged("Resolution confirmed; restarting into the final session…");
                    stopSession([this] {
                        startSession([this] { requestSurface(); });
                    });
                });
            });
        });
    });
}

void IntegratedView::startSession(const std::function<void()> &completed)
{
    emit statusChanged("Starting Waydroid in the hidden integrated compositor…");
    if (sessionProcess_->state() != QProcess::NotRunning) {
        failOperation("The previous Waydroid session process is still active.");
        return;
    }
    sessionProcess_->setProcessEnvironment(nestedEnvironment());
    sessionProcess_->start("waydroid", {"session", "start"});
    if (!sessionProcess_->waitForStarted(3000)) {
        failOperation("Failed to launch the Waydroid session command.");
        return;
    }
    waitForRunning(60, completed);
}

void IntegratedView::waitForRunning(int attemptsLeft,
                                    const std::function<void()> &completed)
{
    runCommand({"status"}, [this, attemptsLeft, completed]
               (int exitCode, const QString &output) {
        if (exitCode == 0 && output.contains("Session: RUNNING")
                          && output.contains("Container: RUNNING")) {
            completed();
            return;
        }
        if (attemptsLeft <= 1) {
            failOperation("Waydroid did not reach the running state within 30 seconds.");
            return;
        }
        QTimer::singleShot(500, this, [this, attemptsLeft, completed] {
            waitForRunning(attemptsLeft - 1, completed);
        });
    });
}

void IntegratedView::requestSurface()
{
    emit statusChanged("Waydroid is running; waiting for the Android surface…");
    waitingForSurface_ = true;
    auto *showProcess = new QProcess(this);
    showProcess->setProcessEnvironment(nestedEnvironment());
    connect(showProcess, &QProcess::finished, showProcess, &QObject::deleteLater);
    showProcess->start("waydroid", {"show-full-ui"});
    QTimer::singleShot(30000, this, [this] {
        if (busy_ && waitingForSurface_)
            failOperation("Waydroid started, but no Android surface arrived within 30 seconds.");
    });
}

void IntegratedView::surfaceReady()
{
    if (!waitingForSurface_)
        return;
    waitingForSurface_ = false;
    setReady(true);
    setBusy(false);
    emit statusChanged("Waydroid is ready. You can open Integrated Android.");
}

void IntegratedView::openIntegratedWindow()
{
    if (!ready_ || busy_) {
        emit statusChanged("Prepare Waydroid successfully before opening Integrated Android.");
        return;
    }
    setWindowVisible(true);
}

void IntegratedView::hideIntegratedWindow()
{
    setWindowVisible(false);
}

void IntegratedView::stopIntegratedSession()
{
    if (busy_)
        return;
    setBusy(true);
    setReady(false);
    setWindowVisible(false);
    emit statusChanged("Stopping Waydroid…");
    stopSession([this] {
        emit statusChanged("Waydroid is stopped. Resolution controls are unlocked.");
        setBusy(false);
    });
}

void IntegratedView::stopSession(const std::function<void()> &completed)
{
    waitingForSurface_ = false;
    runCommand({"session", "stop"}, [this, completed](int code, const QString &) {
        if (code != 0) {
            failOperation("Failed to stop the Waydroid session.");
            return;
        }
        QTimer::singleShot(750, this, [this, completed] {
            waitForStopped(60, 0, completed);
        });
    });
}

void IntegratedView::waitForStopped(int attemptsLeft, int confirmations,
                                    const std::function<void()> &completed)
{
    runCommand({"status"}, [this, attemptsLeft, confirmations, completed]
               (int exitCode, const QString &output) {
        const bool stopped = exitCode == 0 && output.contains("Session: STOPPED");
        const int nextConfirmations = stopped ? confirmations + 1 : 0;
        const bool launcherExited = sessionProcess_->state() == QProcess::NotRunning;
        if (nextConfirmations >= 3 && launcherExited) {
            completed();
            return;
        }
        if (attemptsLeft <= 1) {
            failOperation("Waydroid did not confirm a stable stopped state within 30 seconds.");
            return;
        }
        QTimer::singleShot(500, this, [this, attemptsLeft, nextConfirmations, completed] {
            waitForStopped(attemptsLeft - 1, nextConfirmations, completed);
        });
    });
}

void IntegratedView::runCommand(const QStringList &arguments,
                                const std::function<void(int, const QString &)> &completed,
                                const QProcessEnvironment &environment)
{
    auto *command = new QProcess(this);
    command->setProcessEnvironment(environment);
    connect(command, &QProcess::finished, this,
            [command, completed](int exitCode, QProcess::ExitStatus) {
        const QString output = QString::fromUtf8(command->readAllStandardOutput())
                             + QString::fromUtf8(command->readAllStandardError());
        command->deleteLater();
        completed(exitCode, output);
    });
    command->start("waydroid", arguments);
}

void IntegratedView::failOperation(const QString &status)
{
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

void IntegratedView::setWindowVisible(bool visible)
{
    if (windowVisible_ == visible)
        return;
    windowVisible_ = visible;
    emit windowVisibleChanged();
}
