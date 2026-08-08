#include "IntegratedView.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QWindow>

namespace {
constexpr auto NestedSocket = "evgenium-wayland-0";
}

IntegratedView::IntegratedView(QObject *parent)
    : QObject(parent), engine_(new QQmlApplicationEngine(this)),
      sessionProcess_(new QProcess(this))
{
    engine_->rootContext()->setContextProperty("integratedBackend", this);
}

IntegratedView::~IntegratedView() = default;

void IntegratedView::showAndStart()
{
    ensureWindow();
    restartAndroid();
}

void IntegratedView::ensureWindow()
{
    if (engine_->rootObjects().isEmpty())
        engine_->load(QUrl(QStringLiteral("qrc:/IntegratedView.qml")));

    for (QObject *object : engine_->rootObjects()) {
        if (auto *window = qobject_cast<QWindow *>(object)) {
            window->show();
            window->raise();
            window->requestActivate();
        }
    }
}

QProcessEnvironment IntegratedView::nestedEnvironment() const
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("WAYLAND_DISPLAY", NestedSocket);
    return environment;
}

void IntegratedView::restartAndroid()
{
    if (busy_)
        return;

    setBusy(true);
    emit statusChanged("Restarting the Waydroid session for the integrated display…");
    stopSession([this] {
        startSession([this] {
            showFullUi();
            waitingForSurface_ = true;
            QTimer::singleShot(20000, this, [this] {
                if (busy_ && waitingForSurface_)
                    failOperation("Android started, but its window did not appear within 20 seconds.");
            });
        });
    });
}

void IntegratedView::startSession(const std::function<void()> &completed)
{
    emit statusChanged("Starting Waydroid on evgenium-wayland-0…");
    if (sessionProcess_->state() != QProcess::NotRunning) {
        sessionProcess_->terminate();
        sessionProcess_->waitForFinished(1000);
    }
    sessionProcess_->setProcessEnvironment(nestedEnvironment());
    sessionProcess_->start("waydroid", {"session", "start"});
    if (!sessionProcess_->waitForStarted(3000)) {
        failOperation("Failed to launch the Waydroid session command.");
        return;
    }
    waitForRunning(40, completed);
}

void IntegratedView::showFullUi()
{
    emit statusChanged("Requesting the Android surface…");
    auto *showProcess = new QProcess(this);
    showProcess->setProcessEnvironment(nestedEnvironment());
    connect(showProcess, &QProcess::finished, showProcess, &QObject::deleteLater);
    showProcess->start("waydroid", {"show-full-ui"});
}

void IntegratedView::stopIntegratedSession()
{
    if (busy_)
        return;
    setBusy(true);
    emit statusChanged("Stopping the Waydroid session…");
    stopSession([this] { finishOperation("Waydroid session stopped."); });
}

void IntegratedView::applyResolution(int width, int height)
{
    if (width < 320 || height < 320 || width > 7680 || height > 7680) {
        emit statusChanged("Resolution is outside the supported range.");
        return;
    }

    if (busy_)
        return;

    setBusy(true);
    emit statusChanged(QString("Stopping Android before applying %1 × %2…")
                           .arg(width).arg(height));
    stopSession([this, width, height] {
        emit statusChanged(QString("Writing Android width %1…").arg(width));
        runCommand({"prop", "set", "persist.waydroid.width", QString::number(width)},
                   [this, width, height](int code, const QString &) {
            if (code != 0) {
                failOperation("Failed to set Waydroid width.");
                return;
            }
            emit statusChanged(QString("Writing Android height %1…").arg(height));
            runCommand({"prop", "set", "persist.waydroid.height", QString::number(height)},
                       [this, width, height](int code, const QString &) {
                if (code != 0) {
                    failOperation("Failed to set Waydroid height.");
                    return;
                }
                emit statusChanged("Verifying the saved Android resolution…");
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
                        startSession([this] {
                            showFullUi();
                            waitingForSurface_ = true;
                            QTimer::singleShot(20000, this, [this] {
                                if (busy_ && waitingForSurface_)
                                    failOperation("Resolution was saved, but the Android window did not appear.");
                            });
                        });
                    });
                });
            });
        });
    });
}

void IntegratedView::surfaceReady()
{
    if (!waitingForSurface_)
        return;
    waitingForSurface_ = false;
    finishOperation("Android surface is ready.");
}

void IntegratedView::stopSession(const std::function<void()> &completed)
{
    waitingForSurface_ = false;
    runCommand({"session", "stop"}, [this, completed](int code, const QString &) {
        if (code != 0) {
            failOperation("Failed to stop the Waydroid session.");
            return;
        }
        // The stop command may return before the session D-Bus state settles.
        // Never interpret an empty/transient status response as STOPPED.
        QTimer::singleShot(750, this, [this, completed] {
            waitForStopped(60, 0, completed);
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

void IntegratedView::waitForRunning(int attemptsLeft, const std::function<void()> &completed)
{
    runCommand({"status"}, [this, attemptsLeft, completed](int, const QString &output) {
        if (output.contains("Session: RUNNING") && output.contains("Container: RUNNING")) {
            completed();
            return;
        }
        if (attemptsLeft <= 1) {
            failOperation("Waydroid did not reach the running state within 20 seconds.");
            return;
        }
        QTimer::singleShot(500, this, [this, attemptsLeft, completed] {
            waitForRunning(attemptsLeft - 1, completed);
        });
    });
}

void IntegratedView::waitForStopped(int attemptsLeft, int confirmations,
                                    const std::function<void()> &completed)
{
    runCommand({"status"}, [this, attemptsLeft, confirmations, completed]
               (int exitCode, const QString &output) {
        const bool explicitlyStopped = exitCode == 0
                                    && output.contains("Session: STOPPED");
        const int nextConfirmations = explicitlyStopped ? confirmations + 1 : 0;
        if (nextConfirmations >= 3) {
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

void IntegratedView::finishOperation(const QString &status)
{
    waitingForSurface_ = false;
    emit statusChanged(status);
    setBusy(false);
}

void IntegratedView::failOperation(const QString &status)
{
    waitingForSurface_ = false;
    emit statusChanged(status);
    setBusy(false);
}

void IntegratedView::setBusy(bool busy)
{
    if (busy_ == busy)
        return;
    busy_ = busy;
    emit busyChanged();
}
