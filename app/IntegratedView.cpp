#include "IntegratedView.h"

#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QWindow>
#include <QtMath>
#include <algorithm>
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
    QCoreApplication::instance()->installEventFilter(this);
    loadBindings();

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
    log(QString("loaded tap bindings=%1").arg(bindings_.size()));
}

void IntegratedView::log(const QString &message) const
{
    qInfo().noquote() << QString("[EWM %1] %2")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), message);
}

QVariantList IntegratedView::bindings() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(bindings_.size()));
    for (const TapBinding &binding : bindings_) {
        QVariantMap item;
        item.insert("x", binding.x);
        item.insert("y", binding.y);
        item.insert("key", binding.key);
        item.insert("keyName", keyName(binding.key));
        result.append(item);
    }
    return result;
}

QVariantMap IntegratedView::selectedBinding() const
{
    if (selectedBindingIndex_ < 0
        || selectedBindingIndex_ >= static_cast<int>(bindings_.size()))
        return {};
    const TapBinding &binding = bindings_[static_cast<std::size_t>(selectedBindingIndex_)];
    return {
        {"x", binding.x},
        {"y", binding.y},
        {"pixelX", qRound(binding.x * androidWidth_)},
        {"pixelY", qRound(binding.y * androidHeight_)},
        {"key", binding.key},
        {"keyName", keyName(binding.key)}
    };
}

QString IntegratedView::keyName(int key) const
{
    if (key == 0)
        return "—";
    const QString name = QKeySequence(key).toString(QKeySequence::NativeText);
    return name.isEmpty() ? QString::number(key) : name;
}

void IntegratedView::loadBindings()
{
    QSettings settings;
    const int count = settings.beginReadArray("tapBindings");
    bindings_.clear();
    for (int index = 0; index < count; ++index) {
        settings.setArrayIndex(index);
        TapBinding binding;
        binding.x = settings.value("x").toDouble();
        binding.y = settings.value("y").toDouble();
        binding.key = settings.value("key").toInt();
        if (binding.x >= 0.0 && binding.x <= 1.0
            && binding.y >= 0.0 && binding.y <= 1.0)
            bindings_.push_back(binding);
    }
    settings.endArray();
}

void IntegratedView::saveBindings() const
{
    QSettings settings;
    settings.remove("tapBindings");
    settings.beginWriteArray("tapBindings");
    for (qsizetype index = 0; index < static_cast<qsizetype>(bindings_.size()); ++index) {
        settings.setArrayIndex(index);
        const TapBinding &binding = bindings_[static_cast<std::size_t>(index)];
        settings.setValue("x", binding.x);
        settings.setValue("y", binding.y);
        settings.setValue("key", binding.key);
    }
    settings.endArray();
    settings.sync();
}

void IntegratedView::toggleEditMode()
{
    if (!ready_ || !windowVisible_) {
        emit statusChanged("Open Integrated Android before entering mapper edit mode.");
        return;
    }
    if (editMode_) {
        saveBindings();
        editSnapshot_.clear();
        setEditMode(false);
        emit statusChanged("Mapper changes saved.");
        log("mapper draft accepted and saved");
    } else {
        editSnapshot_ = bindings_;
        setEditMode(true);
    }
}

void IntegratedView::setEditMode(bool enabled)
{
    if (editMode_ == enabled)
        return;
    editMode_ = enabled;
    setWaitingForKey(false);
    selectedBindingIndex_ = -1;
    emit selectedBindingChanged();
    setEditorMessage(enabled
        ? "Editing mapper — right-click the Android screen to add a control"
        : "F5 — open mapper editor");
    emit editModeChanged();
    log(QString("mapper edit mode=%1").arg(enabled));
}

void IntegratedView::addTapAt(double normalizedX, double normalizedY)
{
    if (!editMode_)
        return;
    TapBinding binding;
    binding.x = std::clamp(normalizedX, 0.0, 1.0);
    binding.y = std::clamp(normalizedY, 0.0, 1.0);
    bindings_.push_back(binding);
    emit bindingsChanged();
    selectBinding(static_cast<int>(bindings_.size()) - 1);
    setEditorMessage("Tap control created — use its gear to configure the key or coordinates");
    log(QString("unbound tap created x=%1 y=%2").arg(binding.x).arg(binding.y));
}

void IntegratedView::moveBinding(int index, double normalizedX, double normalizedY)
{
    if (!editMode_ || index < 0 || index >= static_cast<int>(bindings_.size()))
        return;
    TapBinding &binding = bindings_[static_cast<std::size_t>(index)];
    binding.x = std::clamp(normalizedX, 0.0, 1.0);
    binding.y = std::clamp(normalizedY, 0.0, 1.0);
    emit bindingsChanged();
    if (selectedBindingIndex_ == index)
        emit selectedBindingChanged();
    log(QString("binding moved: index=%1 x=%2 y=%3")
            .arg(index).arg(binding.x).arg(binding.y));
}

void IntegratedView::selectBinding(int index)
{
    if (!editMode_ || index < 0 || index >= static_cast<int>(bindings_.size()))
        index = -1;
    if (index < 0 && waitingForKey_) {
        setWaitingForKey(false);
        setEditorMessage("Key selection cancelled");
    }
    if (selectedBindingIndex_ == index)
        return;
    selectedBindingIndex_ = index;
    emit selectedBindingChanged();
}

void IntegratedView::setSelectedBindingPosition(int pixelX, int pixelY)
{
    if (!editMode_ || selectedBindingIndex_ < 0
        || selectedBindingIndex_ >= static_cast<int>(bindings_.size()))
        return;
    TapBinding &binding = bindings_[static_cast<std::size_t>(selectedBindingIndex_)];
    binding.x = std::clamp(pixelX / static_cast<double>(androidWidth_), 0.0, 1.0);
    binding.y = std::clamp(pixelY / static_cast<double>(androidHeight_), 0.0, 1.0);
    emit bindingsChanged();
    emit selectedBindingChanged();
}

void IntegratedView::beginRebindSelected()
{
    if (!editMode_ || selectedBindingIndex_ < 0
        || selectedBindingIndex_ >= static_cast<int>(bindings_.size()))
        return;
    setWaitingForKey(true);
    setEditorMessage("Press the new keyboard key (Esc cancels)");
}

void IntegratedView::captureBindingKey(int key)
{
    if (selectedBindingIndex_ < 0
        || selectedBindingIndex_ >= static_cast<int>(bindings_.size()))
        return;

    for (qsizetype index = 0; index < static_cast<qsizetype>(bindings_.size()); ++index) {
        if (index != selectedBindingIndex_
            && bindings_[static_cast<std::size_t>(index)].key == key)
            bindings_[static_cast<std::size_t>(index)].key = 0;
    }
    bindings_[static_cast<std::size_t>(selectedBindingIndex_)].key = key;
    setWaitingForKey(false);
    setEditorMessage(QString("Bound to %1 — press Done to accept changes").arg(keyName(key)));
    emit bindingsChanged();
    emit selectedBindingChanged();
    log(QString("binding key changed: index=%1 key=%2")
            .arg(selectedBindingIndex_).arg(keyName(key)));
}

void IntegratedView::removeBinding(int index)
{
    if (!editMode_ || index < 0 || index >= static_cast<int>(bindings_.size()))
        return;
    const QString removedKey = keyName(bindings_[static_cast<std::size_t>(index)].key);
    bindings_.erase(bindings_.begin() + index);
    emit bindingsChanged();
    selectedBindingIndex_ = -1;
    emit selectedBindingChanged();
    setEditorMessage(QString("Removed %1 binding").arg(removedKey));
    log("binding removed: " + removedKey);
}

void IntegratedView::setWaitingForKey(bool enabled)
{
    if (waitingForKey_ == enabled)
        return;
    waitingForKey_ = enabled;
    emit waitingForKeyChanged();
}

void IntegratedView::setEditorMessage(const QString &message)
{
    if (editorMessage_ == message)
        return;
    editorMessage_ = message;
    emit editorMessageChanged();
}

bool IntegratedView::eventFilter(QObject *watched, QEvent *event)
{
    const bool isPress = event->type() == QEvent::KeyPress;
    const bool isRelease = event->type() == QEvent::KeyRelease;
    if (!isPress && !isRelease)
        return QObject::eventFilter(watched, event);

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    const int key = keyEvent->key();

    if (key == Qt::Key_F5 && windowVisible_) {
        if (isPress && !keyEvent->isAutoRepeat())
            toggleEditMode();
        return true;
    }

    if (!windowVisible_)
        return QObject::eventFilter(watched, event);

    if (waitingForKey_) {
        if (isRelease)
            return true;
        if (key == Qt::Key_Escape) {
            setWaitingForKey(false);
            setEditorMessage("Binding cancelled");
            return true;
        }
        const bool modifier = key == Qt::Key_Shift || key == Qt::Key_Control
                           || key == Qt::Key_Alt || key == Qt::Key_Meta;
        if (key != Qt::Key_unknown && key != Qt::Key_F11 && !modifier)
            captureBindingKey(key);
        return true;
    }

    if (editMode_) {
        // Let Qt Quick controls receive text/numeric input while editing.
        // Mapped taps remain disabled because this branch precedes lookup below.
        return QObject::eventFilter(watched, event);
    }

    const auto binding = std::find_if(bindings_.cbegin(), bindings_.cend(),
                                      [key](const TapBinding &item) {
        return item.key == key;
    });
    if (binding != bindings_.cend()) {
        if (isPress && !keyEvent->isAutoRepeat())
            injectTap(binding->x, binding->y);
        return true;
    }

    return QObject::eventFilter(watched, event);
}

void IntegratedView::injectTap(double normalizedX, double normalizedY)
{
    QWindow *target = nullptr;
    for (QWindow *window : QGuiApplication::allWindows()) {
        if (window->title() == "Evgenium Waydroid Mapper — Integrated Android") {
            target = window;
            break;
        }
    }
    if (!target || !target->isVisible()) {
        log("tap injection skipped: integrated QWindow is not visible");
        return;
    }

    const double scale = std::min(target->width() / static_cast<double>(androidWidth_),
                                  target->height() / static_cast<double>(androidHeight_));
    const double renderedWidth = androidWidth_ * scale;
    const double renderedHeight = androidHeight_ * scale;
    const double left = (target->width() - renderedWidth) / 2.0;
    const double top = (target->height() - renderedHeight) / 2.0;
    const QPointF local(left + normalizedX * renderedWidth,
                        top + normalizedY * renderedHeight);
    const QPointF global = target->mapToGlobal(local.toPoint());

    QMouseEvent press(QEvent::MouseButtonPress, local, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &press);

    const QPointer<QWindow> guardedTarget(target);
    QTimer::singleShot(35, this, [this, guardedTarget, local, global] {
        if (!guardedTarget)
            return;
        QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(guardedTarget, &release);
    });
    log(QString("tap injected: normalized=%1,%2 window=%3,%4")
            .arg(normalizedX).arg(normalizedY).arg(local.x()).arg(local.y()));
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
    if (editMode_) {
        bindings_ = editSnapshot_;
        editSnapshot_.clear();
        emit bindingsChanged();
        log("mapper draft reverted because Waydroid is stopping");
    }
    setEditMode(false);
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
    const bool resolutionChangedNow = androidWidth_ != width || androidHeight_ != height;
    androidWidth_ = width;
    androidHeight_ = height;
    if (resolutionChangedNow) {
        emit resolutionChanged();
        emit bindingsChanged();
        if (selectedBindingIndex_ >= 0)
            emit selectedBindingChanged();
    }
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

            emit statusChanged("Enabling native mouse-to-touch conversion…");
            runCommand({"prop", "set", "persist.waydroid.fake_touch", "*"},
                       [this, width, height](int touchCode, const QString &) {
                if (touchCode != 0) {
                    failOperation("Waydroid rejected mouse-to-touch mode. See console log.");
                    return;
                }
                log("fake_touch enabled for all Android packages");

                // Readback is diagnostic only. It is deliberately not a launch gate.
                runCommand({"prop", "get", "persist.waydroid.width"},
                           [this, width, height](int code, const QString &output) {
                    log(QString("diagnostic width readback: requested=%1 code=%2 value='%3'")
                            .arg(width).arg(code).arg(output.trimmed()));
                    runCommand({"prop", "get", "persist.waydroid.height"},
                               [this, height](int code, const QString &output) {
                        log(QString("diagnostic height readback: requested=%1 code=%2 value='%3'")
                                .arg(height).arg(code).arg(output.trimmed()));
                        emit statusChanged("Resolution and touch mode applied; restarting Waydroid…");
                        stopSession("configuration restart", [this] {
                            startSession("final", [this] { requestSurface(); });
                        });
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
    if (editMode_) {
        bindings_ = editSnapshot_;
        editSnapshot_.clear();
        emit bindingsChanged();
        log("mapper draft reverted because integrated window was hidden");
    }
    setEditMode(false);
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
