#include "IntegratedView.h"

#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>
#include <QCursor>
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
#include <QWaylandCompositor>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <QWindow>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace {
constexpr auto NestedSocket = "evgenium-wayland-0";
constexpr int SessionSettleMs = 2500;
constexpr int StopSettleMs = 1200;
constexpr double Pi = 3.14159265358979323846;
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
    log(QString("loaded character center=%1, MOBA movement=%2")
            .arg(characterCenter_.enabled).arg(mobaMovement_.enabled));
    log(QString("loaded MOBA skills=%1").arg(mobaSkills_.size()));
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
        item.insert("mode", static_cast<int>(binding.mode));
        item.insert("modeName", binding.mode == TapBinding::Quick
                    ? QStringLiteral("Quick tap")
                    : QStringLiteral("Hold until key release"));
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
        {"keyName", keyName(binding.key)},
        {"mode", static_cast<int>(binding.mode)},
        {"modeName", binding.mode == TapBinding::Quick
                     ? QStringLiteral("Quick tap")
                     : QStringLiteral("Hold until key release")}
    };
}

QVariantMap IntegratedView::characterCenter() const
{
    return {
        {"exists", characterCenter_.enabled},
        {"x", characterCenter_.x},
        {"y", characterCenter_.y},
        {"pixelX", qRound(characterCenter_.x * androidWidth_)},
        {"pixelY", qRound(characterCenter_.y * androidHeight_)}
    };
}

QVariantMap IntegratedView::mobaMovement() const
{
    return {
        {"exists", mobaMovement_.enabled},
        {"x", mobaMovement_.x},
        {"y", mobaMovement_.y},
        {"radius", mobaMovement_.radius},
        {"pixelX", qRound(mobaMovement_.x * androidWidth_)},
        {"pixelY", qRound(mobaMovement_.y * androidHeight_)},
        {"radiusPixels", qRound(mobaMovement_.radius
                                * std::min(androidWidth_, androidHeight_))},
        {"requiresCenter", true},
        {"ready", mobaMovement_.enabled && characterCenter_.enabled}
    };
}

QVariantList IntegratedView::mobaSkills() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(mobaSkills_.size()));
    for (const MobaSkillControl &skill : mobaSkills_) {
        result.append(QVariantMap{
            {"x", skill.x},
            {"y", skill.y},
            {"radius", skill.radius},
            {"pixelX", qRound(skill.x * androidWidth_)},
            {"pixelY", qRound(skill.y * androidHeight_)},
            {"diameterPixels", qRound(skill.radius * 2.0
                                       * std::min(androidWidth_, androidHeight_))},
            {"key", skill.key},
            {"keyName", keyName(skill.key)},
            {"mode", static_cast<int>(skill.mode)},
            {"modeName", QStringLiteral("Follow cursor; release to cast")},
            {"calibrated", static_cast<int>(skill.calibrationPoints.size())
                            == CalibrationSampleCount},
            {"calibrationCount", static_cast<int>(skill.calibrationPoints.size())},
            {"ready", characterCenter_.enabled && skill.key != 0
                      && static_cast<int>(skill.calibrationPoints.size())
                         == CalibrationSampleCount}
        });
    }
    return result;
}

QVariantMap IntegratedView::selectedMobaSkill() const
{
    if (selectedMobaSkillIndex_ < 0
        || selectedMobaSkillIndex_ >= static_cast<int>(mobaSkills_.size()))
        return {};
    return mobaSkills().at(selectedMobaSkillIndex_).toMap();
}

QString IntegratedView::calibrationInstruction() const
{
    if (!calibrationActive())
        return {};
    const int direction = calibrationStep_ % CalibrationDirections;
    const int ring = calibrationStep_ / CalibrationDirections;
    static const QStringList directionNames = {
        QStringLiteral("вправо"), QStringLiteral("вниз-вправо"),
        QStringLiteral("вниз"), QStringLiteral("вниз-влево"),
        QStringLiteral("влево"), QStringLiteral("вверх-влево"),
        QStringLiteral("вверх"), QStringLiteral("вверх-вправо")
    };
    const int percent = qRound((ring + 1) * 100.0 / CalibrationRings);
    return QStringLiteral("Скилл удерживается на %1% %2. Кликни ЛКМ точно "
                          "в КОНЕЦ игрового указателя дальности.")
        .arg(percent).arg(directionNames.at(direction));
}

QVariantList IntegratedView::calibrationPoints() const
{
    QVariantList result;
    if (!calibrationActive())
        return result;
    const MobaSkillControl &skill =
        mobaSkills_[static_cast<std::size_t>(calibrationSkillIndex_)];
    for (const QPointF &point : skill.calibrationPoints)
        result.append(QVariantMap{{"x", point.x()}, {"y", point.y()}});
    return result;
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
        binding.mode = settings.value("mode", TapBinding::Quick).toInt()
                       == TapBinding::HoldUntilKeyRelease
                       ? TapBinding::HoldUntilKeyRelease : TapBinding::Quick;
        if (binding.x >= 0.0 && binding.x <= 1.0
            && binding.y >= 0.0 && binding.y <= 1.0)
            bindings_.push_back(binding);
    }
    settings.endArray();

    settings.beginGroup("characterCenter");
    characterCenter_.enabled = settings.value("enabled", false).toBool();
    characterCenter_.x = std::clamp(settings.value("x", 0.5).toDouble(), 0.0, 1.0);
    characterCenter_.y = std::clamp(settings.value("y", 0.5).toDouble(), 0.0, 1.0);
    settings.endGroup();

    settings.beginGroup("mobaMovement");
    mobaMovement_.enabled = settings.value("enabled", false).toBool();
    mobaMovement_.x = std::clamp(settings.value("x", 0.18).toDouble(), 0.0, 1.0);
    mobaMovement_.y = std::clamp(settings.value("y", 0.78).toDouble(), 0.0, 1.0);
    mobaMovement_.radius = std::clamp(settings.value("radius", 0.09).toDouble(),
                                      0.02, 0.35);
    settings.endGroup();

    const int skillCount = settings.beginReadArray("mobaSkills");
    mobaSkills_.clear();
    for (int index = 0; index < skillCount; ++index) {
        settings.setArrayIndex(index);
        MobaSkillControl skill;
        skill.x = std::clamp(settings.value("x", 0.82).toDouble(), 0.0, 1.0);
        skill.y = std::clamp(settings.value("y", 0.76).toDouble(), 0.0, 1.0);
        skill.radius = std::clamp(settings.value("radius", 0.055).toDouble(),
                                  0.02, 0.35);
        skill.key = settings.value("key", 0).toInt();
        skill.mode = MobaSkillControl::FollowCursorReleaseToCast;
        const QStringList encodedPoints = settings.value("calibrationPoints").toStringList();
        for (const QString &encoded : encodedPoints) {
            const QStringList coordinates = encoded.split(',');
            bool xOk = false;
            bool yOk = false;
            const double x = coordinates.value(0).toDouble(&xOk);
            const double y = coordinates.value(1).toDouble(&yOk);
            if (xOk && yOk && x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0)
                skill.calibrationPoints.emplace_back(x, y);
        }
        if (static_cast<int>(skill.calibrationPoints.size()) != CalibrationSampleCount)
            skill.calibrationPoints.clear();
        mobaSkills_.push_back(std::move(skill));
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
        settings.setValue("mode", static_cast<int>(binding.mode));
    }
    settings.endArray();

    settings.beginGroup("characterCenter");
    settings.setValue("enabled", characterCenter_.enabled);
    settings.setValue("x", characterCenter_.x);
    settings.setValue("y", characterCenter_.y);
    settings.endGroup();

    settings.beginGroup("mobaMovement");
    settings.setValue("enabled", mobaMovement_.enabled);
    settings.setValue("x", mobaMovement_.x);
    settings.setValue("y", mobaMovement_.y);
    settings.setValue("radius", mobaMovement_.radius);
    settings.endGroup();

    settings.remove("mobaSkills");
    settings.beginWriteArray("mobaSkills");
    for (qsizetype index = 0; index < static_cast<qsizetype>(mobaSkills_.size()); ++index) {
        settings.setArrayIndex(index);
        const MobaSkillControl &skill = mobaSkills_[static_cast<std::size_t>(index)];
        settings.setValue("x", skill.x);
        settings.setValue("y", skill.y);
        settings.setValue("radius", skill.radius);
        settings.setValue("key", skill.key);
        settings.setValue("mode", static_cast<int>(skill.mode));
        QStringList encodedPoints;
        encodedPoints.reserve(static_cast<qsizetype>(skill.calibrationPoints.size()));
        for (const QPointF &point : skill.calibrationPoints) {
            encodedPoints.append(QString::number(point.x(), 'g', 17)
                                 + ',' + QString::number(point.y(), 'g', 17));
        }
        settings.setValue("calibrationPoints", encodedPoints);
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
        characterCenterSnapshot_ = {};
        mobaMovementSnapshot_ = {};
        mobaSkillsSnapshot_.clear();
        setEditMode(false);
        emit statusChanged("Mapper changes saved.");
        log("mapper draft accepted and saved");
    } else {
        editSnapshot_ = bindings_;
        characterCenterSnapshot_ = characterCenter_;
        mobaMovementSnapshot_ = mobaMovement_;
        mobaSkillsSnapshot_ = mobaSkills_;
        setEditMode(true);
    }
}

void IntegratedView::setEditMode(bool enabled)
{
    if (calibrationActive())
        cancelMobaSkillCalibration();
    if (mobaMovementActive_)
        endMobaMovement();
    if (!activeTapPoints_.isEmpty())
        releaseAllTapTouches();
    if (editMode_ == enabled)
        return;
    editMode_ = enabled;
    setWaitingForKey(false);
    selectedBindingIndex_ = -1;
    selectedMobaSkillIndex_ = -1;
    keyCaptureTarget_ = KeyCaptureTarget::None;
    emit selectedBindingChanged();
    emit selectedMobaSkillChanged();
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

void IntegratedView::addCharacterCenterAt(double normalizedX, double normalizedY)
{
    if (!editMode_)
        return;
    const bool movedExisting = characterCenter_.enabled;
    if (movedExisting)
        invalidateMobaSkillCalibrations(
            "Character center moved — MOBA skills must be recalibrated");
    characterCenter_.enabled = true;
    characterCenter_.x = std::clamp(normalizedX, 0.0, 1.0);
    characterCenter_.y = std::clamp(normalizedY, 0.0, 1.0);
    emit characterCenterChanged();
    emit mobaMovementChanged();
    emit mobaSkillsChanged();
    setEditorMessage(movedExisting
        ? "Character center moved — only one center can exist"
        : "Character center created — drag the cross onto the hero");
    log(QString("character center %1 x=%2 y=%3")
            .arg(movedExisting ? QStringLiteral("moved") : QStringLiteral("created"))
            .arg(characterCenter_.x).arg(characterCenter_.y));
}

void IntegratedView::moveCharacterCenter(double normalizedX, double normalizedY)
{
    if (!editMode_ || !characterCenter_.enabled)
        return;
    characterCenter_.x = std::clamp(normalizedX, 0.0, 1.0);
    characterCenter_.y = std::clamp(normalizedY, 0.0, 1.0);
    invalidateMobaSkillCalibrations(
        "Character center moved — MOBA skills must be recalibrated");
    emit characterCenterChanged();
    emit mobaMovementChanged();
    emit mobaSkillsChanged();
}

void IntegratedView::addMobaMovementAt(double normalizedX, double normalizedY)
{
    if (!editMode_)
        return;
    const bool movedExisting = mobaMovement_.enabled;
    mobaMovement_.enabled = true;
    mobaMovement_.x = std::clamp(normalizedX, 0.0, 1.0);
    mobaMovement_.y = std::clamp(normalizedY, 0.0, 1.0);
    emit mobaMovementChanged();
    setEditorMessage(characterCenter_.enabled
        ? (movedExisting
            ? "MOBA movement moved — drag the triangle to change its radius"
            : "MOBA movement created — hold RMB to steer")
        : "Warning: MOBA movement requires a Character center cross");
    log(QString("MOBA movement %1 x=%2 y=%3 radius=%4 centerReady=%5")
            .arg(movedExisting ? QStringLiteral("moved") : QStringLiteral("created"))
            .arg(mobaMovement_.x).arg(mobaMovement_.y)
            .arg(mobaMovement_.radius).arg(characterCenter_.enabled));
}

void IntegratedView::moveMobaMovement(double normalizedX, double normalizedY)
{
    if (!editMode_ || !mobaMovement_.enabled)
        return;
    mobaMovement_.x = std::clamp(normalizedX, 0.0, 1.0);
    mobaMovement_.y = std::clamp(normalizedY, 0.0, 1.0);
    emit mobaMovementChanged();
}

void IntegratedView::resizeMobaMovement(double normalizedRadius)
{
    if (!editMode_ || !mobaMovement_.enabled)
        return;
    const double minimumRadius = 32.0 / std::max(1, std::min(androidWidth_, androidHeight_));
    mobaMovement_.radius = std::clamp(normalizedRadius, minimumRadius, 0.35);
    emit mobaMovementChanged();
}

void IntegratedView::addMobaSkillAt(double normalizedX, double normalizedY)
{
    if (!editMode_ || calibrationActive())
        return;
    MobaSkillControl skill;
    skill.x = std::clamp(normalizedX, 0.0, 1.0);
    skill.y = std::clamp(normalizedY, 0.0, 1.0);
    mobaSkills_.push_back(skill);
    emit mobaSkillsChanged();
    selectMobaSkill(static_cast<int>(mobaSkills_.size()) - 1);
    setEditorMessage(characterCenter_.enabled
        ? "MOBA skill created — bind a key, then calibrate it"
        : "MOBA skill created — add Character center before calibration");
    log(QString("MOBA skill created: index=%1 x=%2 y=%3")
            .arg(mobaSkills_.size() - 1).arg(skill.x).arg(skill.y));
}

void IntegratedView::moveMobaSkill(int index, double normalizedX, double normalizedY)
{
    if (!editMode_ || calibrationActive() || index < 0
        || index >= static_cast<int>(mobaSkills_.size()))
        return;
    MobaSkillControl &skill = mobaSkills_[static_cast<std::size_t>(index)];
    skill.x = std::clamp(normalizedX, 0.0, 1.0);
    skill.y = std::clamp(normalizedY, 0.0, 1.0);
    const bool invalidated = !skill.calibrationPoints.empty();
    skill.calibrationPoints.clear();
    emit mobaSkillsChanged();
    if (selectedMobaSkillIndex_ == index)
        emit selectedMobaSkillChanged();
    if (invalidated)
        setEditorMessage("Skill joystick moved — calibration must be repeated");
}

void IntegratedView::resizeMobaSkill(int index, double normalizedRadius)
{
    if (!editMode_ || calibrationActive() || index < 0
        || index >= static_cast<int>(mobaSkills_.size()))
        return;
    const double minimumRadius = 24.0 / std::max(1, std::min(androidWidth_, androidHeight_));
    MobaSkillControl &skill = mobaSkills_[static_cast<std::size_t>(index)];
    const double nextRadius = std::clamp(normalizedRadius, minimumRadius, 0.35);
    if (qFuzzyCompare(skill.radius, nextRadius))
        return;
    skill.radius = nextRadius;
    const bool invalidated = !skill.calibrationPoints.empty();
    skill.calibrationPoints.clear();
    emit mobaSkillsChanged();
    if (selectedMobaSkillIndex_ == index)
        emit selectedMobaSkillChanged();
    if (invalidated)
        setEditorMessage("Skill diameter changed — calibration must be repeated");
}

void IntegratedView::selectMobaSkill(int index)
{
    if (!editMode_ || index < 0 || index >= static_cast<int>(mobaSkills_.size()))
        index = -1;
    if (selectedMobaSkillIndex_ == index)
        return;
    if (waitingForKey_ && keyCaptureTarget_ == KeyCaptureTarget::MobaSkill)
        setWaitingForKey(false);
    selectedMobaSkillIndex_ = index;
    if (index >= 0 && selectedBindingIndex_ >= 0) {
        selectedBindingIndex_ = -1;
        emit selectedBindingChanged();
    }
    emit selectedMobaSkillChanged();
}

void IntegratedView::setSelectedMobaSkillPosition(int pixelX, int pixelY)
{
    if (selectedMobaSkillIndex_ < 0)
        return;
    moveMobaSkill(selectedMobaSkillIndex_,
                  pixelX / static_cast<double>(androidWidth_),
                  pixelY / static_cast<double>(androidHeight_));
}

void IntegratedView::setSelectedMobaSkillDiameter(int diameterPixels)
{
    if (selectedMobaSkillIndex_ < 0)
        return;
    const double radius = diameterPixels
        / (2.0 * std::max(1, std::min(androidWidth_, androidHeight_)));
    resizeMobaSkill(selectedMobaSkillIndex_, radius);
}

void IntegratedView::setSelectedMobaSkillMode(int mode)
{
    if (!editMode_ || selectedMobaSkillIndex_ < 0
        || selectedMobaSkillIndex_ >= static_cast<int>(mobaSkills_.size()))
        return;
    Q_UNUSED(mode);
    mobaSkills_[static_cast<std::size_t>(selectedMobaSkillIndex_)].mode =
        MobaSkillControl::FollowCursorReleaseToCast;
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
}

void IntegratedView::beginRebindSelectedMobaSkill()
{
    if (!editMode_ || calibrationActive() || selectedMobaSkillIndex_ < 0
        || selectedMobaSkillIndex_ >= static_cast<int>(mobaSkills_.size()))
        return;
    keyCaptureTarget_ = KeyCaptureTarget::MobaSkill;
    setWaitingForKey(true);
    setEditorMessage("Press the MOBA skill key (Esc cancels)");
}

void IntegratedView::removeMobaSkill(int index)
{
    if (!editMode_ || calibrationActive() || index < 0
        || index >= static_cast<int>(mobaSkills_.size()))
        return;
    const QString removedKey = keyName(mobaSkills_[static_cast<std::size_t>(index)].key);
    mobaSkills_.erase(mobaSkills_.begin() + index);
    selectedMobaSkillIndex_ = -1;
    keyCaptureTarget_ = KeyCaptureTarget::None;
    setWaitingForKey(false);
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
    setEditorMessage(QString("Removed MOBA skill %1").arg(removedKey));
    log("MOBA skill removed: " + removedKey);
}

QPointF IntegratedView::calibrationVector(int step) const
{
    const int ring = std::clamp(step / CalibrationDirections, 0,
                                CalibrationRings - 1);
    const int direction = ((step % CalibrationDirections)
                           + CalibrationDirections) % CalibrationDirections;
    const double radius = (ring + 1.0) / CalibrationRings;
    const double angle = direction * (2.0 * Pi / CalibrationDirections);
    return {std::cos(angle) * radius, std::sin(angle) * radius};
}

void IntegratedView::beginMobaSkillCalibration(int index)
{
    if (!editMode_ || calibrationActive() || index < 0
        || index >= static_cast<int>(mobaSkills_.size()))
        return;
    if (!characterCenter_.enabled) {
        setEditorMessage("Calibration needs Character center — add the cross first");
        emit statusChanged("MOBA skill calibration needs a Character center.");
        return;
    }
    if (!inputSurface_) {
        setEditorMessage("Android surface is unavailable; reopen Integrated Android");
        return;
    }

    MobaSkillControl &skill = mobaSkills_[static_cast<std::size_t>(index)];
    calibrationBackupPoints_ = skill.calibrationPoints;
    skill.calibrationPoints.clear();
    calibrationSkillIndex_ = index;
    calibrationStep_ = 0;
    calibrationPointReady_ = false;
    calibrationTouchId_ = allocateTouchId();
    if (calibrationTouchId_ < 0) {
        calibrationSkillIndex_ = -1;
        skill.calibrationPoints = calibrationBackupPoints_;
        calibrationBackupPoints_.clear();
        emit calibrationChanged();
        return;
    }

    calibrationLastTouch_ = {skill.x, skill.y};
    if (!sendTouchPoint(calibrationTouchId_, calibrationLastTouch_,
                        Qt::TouchPointPressed)) {
        calibrationTouchId_ = -1;
        calibrationSkillIndex_ = -1;
        skill.calibrationPoints = calibrationBackupPoints_;
        calibrationBackupPoints_.clear();
        emit calibrationChanged();
        return;
    }
    activeTapPoints_.insert(calibrationTouchId_, calibrationLastTouch_);
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
    emit calibrationChanged();
    setEditorMessage("MOBA skill calibration started — follow the card at the top");
    QTimer::singleShot(120, this, [this] {
        if (calibrationActive())
            moveCalibrationTouch();
    });
    log(QString("MOBA skill calibration started: index=%1 touch=%2 samples=%3")
            .arg(index).arg(calibrationTouchId_).arg(CalibrationSampleCount));
}

void IntegratedView::moveCalibrationTouch()
{
    if (!calibrationActive() || calibrationTouchId_ < 0)
        return;
    const MobaSkillControl &skill =
        mobaSkills_[static_cast<std::size_t>(calibrationSkillIndex_)];
    calibrationPointReady_ = false;
    const QPointF vector = calibrationVector(calibrationStep_);
    const double radiusPixels = skill.radius * std::min(androidWidth_, androidHeight_);
    calibrationLastTouch_ = {
        std::clamp(skill.x + vector.x() * radiusPixels / androidWidth_, 0.0, 1.0),
        std::clamp(skill.y + vector.y() * radiusPixels / androidHeight_, 0.0, 1.0)
    };
    sendTouchPoint(calibrationTouchId_, calibrationLastTouch_, Qt::TouchPointMoved);
    activeTapPoints_[calibrationTouchId_] = calibrationLastTouch_;
    emit calibrationChanged();
    QTimer::singleShot(180, this, [this] {
        if (!calibrationActive())
            return;
        calibrationPointReady_ = true;
        emit calibrationChanged();
    });
    log(QString("calibration sample ready: %1/%2 vector=%3,%4")
            .arg(calibrationStep_ + 1).arg(CalibrationSampleCount)
            .arg(vector.x()).arg(vector.y()));
}

void IntegratedView::recordMobaSkillCalibrationPoint(double normalizedX,
                                                      double normalizedY)
{
    if (!calibrationActive() || !calibrationPointReady_)
        return;
    MobaSkillControl &skill =
        mobaSkills_[static_cast<std::size_t>(calibrationSkillIndex_)];
    skill.calibrationPoints.emplace_back(
        std::clamp(normalizedX, 0.0, 1.0),
        std::clamp(normalizedY, 0.0, 1.0));
    log(QString("calibration point recorded: %1/%2 screen=%3,%4")
            .arg(skill.calibrationPoints.size()).arg(CalibrationSampleCount)
            .arg(normalizedX).arg(normalizedY));
    ++calibrationStep_;
    calibrationPointReady_ = false;
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
    emit calibrationChanged();
    if (calibrationStep_ >= CalibrationSampleCount) {
        finishMobaSkillCalibration();
        return;
    }
    QTimer::singleShot(80, this, [this] {
        if (calibrationActive())
            moveCalibrationTouch();
    });
}

void IntegratedView::undoMobaSkillCalibrationPoint()
{
    if (!calibrationActive())
        return;
    MobaSkillControl &skill =
        mobaSkills_[static_cast<std::size_t>(calibrationSkillIndex_)];
    if (skill.calibrationPoints.empty() || calibrationStep_ <= 0)
        return;
    skill.calibrationPoints.pop_back();
    --calibrationStep_;
    calibrationPointReady_ = false;
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
    emit calibrationChanged();
    moveCalibrationTouch();
    log(QString("calibration stepped back to point %1/%2")
            .arg(calibrationStep_ + 1).arg(CalibrationSampleCount));
}

void IntegratedView::finishMobaSkillCalibration()
{
    if (!calibrationActive())
        return;
    const int completedIndex = calibrationSkillIndex_;
    if (calibrationTouchId_ >= 0) {
        sendTouchPoint(calibrationTouchId_, calibrationLastTouch_,
                       Qt::TouchPointReleased);
        activeTapPoints_.remove(calibrationTouchId_);
    }
    calibrationTouchId_ = -1;
    calibrationSkillIndex_ = -1;
    calibrationStep_ = 0;
    calibrationPointReady_ = false;
    calibrationBackupPoints_.clear();
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
    emit calibrationChanged();
    setEditorMessage("Calibration complete — press Done to save the skill profile");
    emit mobaSkillCalibrationCompleted(completedIndex);
    log(QString("MOBA skill calibration completed: index=%1").arg(completedIndex));
}

void IntegratedView::cancelMobaSkillCalibration()
{
    if (!calibrationActive())
        return;
    const int cancelledIndex = calibrationSkillIndex_;
    if (calibrationTouchId_ >= 0) {
        sendTouchPoint(calibrationTouchId_, calibrationLastTouch_,
                       Qt::TouchPointReleased);
        activeTapPoints_.remove(calibrationTouchId_);
    }
    mobaSkills_[static_cast<std::size_t>(cancelledIndex)].calibrationPoints =
        calibrationBackupPoints_;
    calibrationBackupPoints_.clear();
    calibrationTouchId_ = -1;
    calibrationSkillIndex_ = -1;
    calibrationStep_ = 0;
    calibrationPointReady_ = false;
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
    emit calibrationChanged();
    setEditorMessage("Calibration cancelled; the previous profile was restored");
    log(QString("MOBA skill calibration cancelled: index=%1").arg(cancelledIndex));
}

void IntegratedView::invalidateMobaSkillCalibrations(const QString &reason)
{
    bool changed = false;
    for (MobaSkillControl &skill : mobaSkills_) {
        if (!skill.calibrationPoints.empty()) {
            skill.calibrationPoints.clear();
            changed = true;
        }
    }
    if (!changed)
        return;
    emit mobaSkillsChanged();
    if (selectedMobaSkillIndex_ >= 0)
        emit selectedMobaSkillChanged();
    setEditorMessage(reason);
    log("all MOBA skill calibrations invalidated: " + reason);
}

void IntegratedView::removeCharacterCenter()
{
    if (!editMode_ || !characterCenter_.enabled)
        return;
    characterCenter_.enabled = false;
    invalidateMobaSkillCalibrations(
        "Character center removed — MOBA skill calibrations were cleared");
    emit characterCenterChanged();
    emit mobaMovementChanged();
    emit mobaSkillsChanged();
    setEditorMessage(mobaMovement_.enabled
        ? "Character center removed — MOBA movement is now disabled"
        : "Character center removed");
    log("character center removed from mapper draft");
}

void IntegratedView::removeMobaMovement()
{
    if (!editMode_ || !mobaMovement_.enabled)
        return;
    mobaMovement_.enabled = false;
    emit mobaMovementChanged();
    setEditorMessage("MOBA movement removed");
    log("MOBA movement removed from mapper draft");
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
    if (index >= 0 && selectedMobaSkillIndex_ >= 0) {
        selectedMobaSkillIndex_ = -1;
        emit selectedMobaSkillChanged();
    }
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

void IntegratedView::setSelectedBindingMode(int mode)
{
    if (!editMode_ || selectedBindingIndex_ < 0
        || selectedBindingIndex_ >= static_cast<int>(bindings_.size()))
        return;
    TapBinding &binding = bindings_[static_cast<std::size_t>(selectedBindingIndex_)];
    binding.mode = mode == TapBinding::HoldUntilKeyRelease
                   ? TapBinding::HoldUntilKeyRelease : TapBinding::Quick;
    emit bindingsChanged();
    emit selectedBindingChanged();
    setEditorMessage(binding.mode == TapBinding::Quick
        ? "Quick tap: touch releases immediately after activation"
        : "Hold mode: touch releases with the keyboard key");
}

void IntegratedView::beginRebindSelected()
{
    if (!editMode_ || selectedBindingIndex_ < 0
        || selectedBindingIndex_ >= static_cast<int>(bindings_.size()))
        return;
    keyCaptureTarget_ = KeyCaptureTarget::TapBinding;
    setWaitingForKey(true);
    setEditorMessage("Press the new keyboard key (Esc cancels)");
}

void IntegratedView::captureSelectedKey(int key)
{
    for (TapBinding &binding : bindings_) {
        if (binding.key == key)
            binding.key = 0;
    }
    for (MobaSkillControl &skill : mobaSkills_) {
        if (skill.key == key)
            skill.key = 0;
    }

    if (keyCaptureTarget_ == KeyCaptureTarget::TapBinding
        && selectedBindingIndex_ >= 0
        && selectedBindingIndex_ < static_cast<int>(bindings_.size())) {
        bindings_[static_cast<std::size_t>(selectedBindingIndex_)].key = key;
        log(QString("binding key changed: index=%1 key=%2")
                .arg(selectedBindingIndex_).arg(keyName(key)));
    } else if (keyCaptureTarget_ == KeyCaptureTarget::MobaSkill
               && selectedMobaSkillIndex_ >= 0
               && selectedMobaSkillIndex_ < static_cast<int>(mobaSkills_.size())) {
        mobaSkills_[static_cast<std::size_t>(selectedMobaSkillIndex_)].key = key;
        log(QString("MOBA skill key changed: index=%1 key=%2")
                .arg(selectedMobaSkillIndex_).arg(keyName(key)));
    } else {
        return;
    }
    keyCaptureTarget_ = KeyCaptureTarget::None;
    setWaitingForKey(false);
    setEditorMessage(QString("Bound to %1 — press Done to accept changes").arg(keyName(key)));
    emit bindingsChanged();
    emit selectedBindingChanged();
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
}

void IntegratedView::removeBinding(int index)
{
    if (!editMode_ || index < 0 || index >= static_cast<int>(bindings_.size()))
        return;
    const QString removedKey = keyName(bindings_[static_cast<std::size_t>(index)].key);
    if (selectedBindingIndex_ == index)
        setWaitingForKey(false);
    bindings_.erase(bindings_.begin() + index);
    emit bindingsChanged();
    selectedBindingIndex_ = -1;
    keyCaptureTarget_ = KeyCaptureTarget::None;
    emit selectedBindingChanged();
    setEditorMessage(QString("Removed %1 binding").arg(removedKey));
    log("binding removed: " + removedKey);
}

void IntegratedView::setWaitingForKey(bool enabled)
{
    if (!enabled)
        keyCaptureTarget_ = KeyCaptureTarget::None;
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
    const bool isMousePress = event->type() == QEvent::MouseButtonPress;
    const bool isMouseRelease = event->type() == QEvent::MouseButtonRelease;
    const bool isMouseMove = event->type() == QEvent::MouseMove;
    if (isMousePress || isMouseRelease || isMouseMove) {
        QWindow *target = integratedWindow();
        if (!windowVisible_ || !target || watched != target || editMode_)
            return QObject::eventFilter(watched, event);

        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (isMouseMove && !activeMobaSkillTouchIds_.isEmpty()) {
            QPointF pointer;
            if (windowToNormalized(target, mouseEvent->position(), &pointer, true))
                updateMobaSkills(pointer);
        }

        if (mobaMovement_.enabled
            && isMousePress && mouseEvent->button() == Qt::RightButton) {
            QPointF pointer;
            if (!windowToNormalized(target, mouseEvent->position(), &pointer))
                return true;
            if (!characterCenter_.enabled) {
                emit statusChanged("MOBA movement needs a Character center. Press F5 and add the cross.");
                log("MOBA RMB ignored: Character center is missing");
                return true;
            }
            beginMobaMovement(pointer);
            return true;
        }

        if (mobaMovement_.enabled && isMouseMove
            && mouseEvent->buttons().testFlag(Qt::RightButton)) {
            if (mobaMovementActive_) {
                QPointF pointer;
                if (windowToNormalized(target, mouseEvent->position(), &pointer, true))
                    updateMobaMovement(pointer);
            }
            return true;
        }

        if (mobaMovement_.enabled && isMouseRelease
            && mouseEvent->button() == Qt::RightButton) {
            if (mobaMovementActive_)
                endMobaMovement();
            return true;
        }
    }

    const bool isPress = event->type() == QEvent::KeyPress;
    const bool isRelease = event->type() == QEvent::KeyRelease;
    if (!isPress && !isRelease)
        return QObject::eventFilter(watched, event);

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    const int key = keyEvent->key();

    if (calibrationActive()) {
        if (isPress && key == Qt::Key_Escape && !keyEvent->isAutoRepeat())
            cancelMobaSkillCalibration();
        else if (isPress && key == Qt::Key_F5)
            setEditorMessage("Finish or cancel the active skill calibration first");
        return true;
    }

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
            captureSelectedKey(key);
        return true;
    }

    if (editMode_) {
        // Let Qt Quick controls receive text/numeric input while editing.
        // Mapped taps remain disabled because this branch precedes lookup below.
        return QObject::eventFilter(watched, event);
    }

    const auto skill = std::find_if(mobaSkills_.cbegin(), mobaSkills_.cend(),
                                    [key](const MobaSkillControl &item) {
        return item.key != 0 && item.key == key;
    });
    if (skill != mobaSkills_.cend()) {
        const int index = static_cast<int>(std::distance(mobaSkills_.cbegin(), skill));
        if (isPress && !keyEvent->isAutoRepeat()) {
            QWindow *target = integratedWindow();
            QPointF pointer;
            const QPoint local = target ? target->mapFromGlobal(QCursor::pos()) : QPoint();
            if (target && windowToNormalized(target, local, &pointer, true))
                beginMobaSkill(index, pointer);
        } else if (isRelease) {
            endMobaSkill(index);
        }
        return true;
    }

    const auto binding = std::find_if(bindings_.cbegin(), bindings_.cend(),
                                      [key](const TapBinding &item) {
        return item.key == key;
    });
    if (binding != bindings_.cend()) {
        // Tap activation is centralized here. A future per-binding option may
        // explicitly call endMobaMovement() before this block; the default path
        // deliberately never interferes with movement touch id 0.
        if (binding->mode == TapBinding::Quick) {
            if (isPress && !keyEvent->isAutoRepeat())
                triggerQuickTap(binding->x, binding->y);
        } else if (isPress && !keyEvent->isAutoRepeat()) {
            beginHeldTap(key, binding->x, binding->y);
        } else if (isRelease) {
            endHeldTap(key);
        }
        return true;
    }

    return QObject::eventFilter(watched, event);
}

QWindow *IntegratedView::integratedWindow() const
{
    for (QWindow *window : QGuiApplication::allWindows()) {
        if (window->title() == "Evgenium Waydroid Mapper — Integrated Android")
            return window;
    }
    return nullptr;
}

bool IntegratedView::windowToNormalized(QWindow *target, const QPointF &local,
                                        QPointF *normalized,
                                        bool clampToSurface) const
{
    if (!target || androidWidth_ <= 0 || androidHeight_ <= 0)
        return false;
    const double scale = std::min(target->width() / static_cast<double>(androidWidth_),
                                  target->height() / static_cast<double>(androidHeight_));
    if (scale <= 0.0)
        return false;
    const double renderedWidth = androidWidth_ * scale;
    const double renderedHeight = androidHeight_ * scale;
    const double left = (target->width() - renderedWidth) / 2.0;
    const double top = (target->height() - renderedHeight) / 2.0;
    double x = (local.x() - left) / renderedWidth;
    double y = (local.y() - top) / renderedHeight;
    const bool inside = x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0;
    if (!inside && !clampToSurface)
        return false;
    x = std::clamp(x, 0.0, 1.0);
    y = std::clamp(y, 0.0, 1.0);
    *normalized = {x, y};
    return true;
}

QWaylandCompositor *IntegratedView::waylandCompositor() const
{
    if (engine_->rootObjects().isEmpty())
        return nullptr;
    return qobject_cast<QWaylandCompositor *>(engine_->rootObjects().constFirst());
}

bool IntegratedView::sendTouchPoint(int id, const QPointF &normalized,
                                    Qt::TouchPointState state)
{
    QWaylandCompositor *compositor = waylandCompositor();
    QWaylandSeat *seat = compositor ? compositor->defaultSeat() : nullptr;
    QWaylandSurface *surface = inputSurface_.data();
    if (!seat || !surface) {
        log(QString("touch id=%1 skipped: Wayland seat or Android surface is missing")
                .arg(id));
        return false;
    }

    const QPointF surfacePoint(
        std::clamp(normalized.x(), 0.0, 1.0) * androidWidth_,
        std::clamp(normalized.y(), 0.0, 1.0) * androidHeight_);
    seat->sendTouchPointEvent(surface, id, surfacePoint, state);
    seat->sendTouchFrameEvent(surface->client());
    return true;
}

int IntegratedView::allocateTouchId()
{
    constexpr int MaximumTouchId = 63;
    for (int attempt = 0; attempt < MaximumTouchId; ++attempt) {
        const int id = nextTouchId_;
        nextTouchId_ = nextTouchId_ >= MaximumTouchId ? 1 : nextTouchId_ + 1;
        if (!activeTapPoints_.contains(id))
            return id;
    }
    log("tap ignored: all mapper touch ids are active");
    return -1;
}

void IntegratedView::triggerQuickTap(double normalizedX, double normalizedY)
{
    const int id = allocateTouchId();
    if (id < 0)
        return;
    const QPointF point(normalizedX, normalizedY);
    if (!sendTouchPoint(id, point, Qt::TouchPointPressed))
        return;
    activeTapPoints_.insert(id, point);
    QTimer::singleShot(35, this, [this, id] {
        const auto point = activeTapPoints_.constFind(id);
        if (point == activeTapPoints_.cend())
            return;
        sendTouchPoint(id, point.value(), Qt::TouchPointReleased);
        activeTapPoints_.remove(id);
    });
    log(QString("quick tap touch=%1 normalized=%2,%3")
            .arg(id).arg(normalizedX).arg(normalizedY));
}

void IntegratedView::beginHeldTap(int key, double normalizedX, double normalizedY)
{
    if (heldTapIdsByKey_.contains(key))
        return;
    const int id = allocateTouchId();
    if (id < 0)
        return;
    const QPointF point(normalizedX, normalizedY);
    if (!sendTouchPoint(id, point, Qt::TouchPointPressed))
        return;
    activeTapPoints_.insert(id, point);
    heldTapIdsByKey_.insert(key, id);
    log(QString("held tap down: key=%1 touch=%2 normalized=%3,%4")
            .arg(keyName(key)).arg(id).arg(normalizedX).arg(normalizedY));
}

void IntegratedView::endHeldTap(int key)
{
    const auto held = heldTapIdsByKey_.find(key);
    if (held == heldTapIdsByKey_.end())
        return;
    const int id = held.value();
    const QPointF point = activeTapPoints_.value(id);
    sendTouchPoint(id, point, Qt::TouchPointReleased);
    heldTapIdsByKey_.erase(held);
    activeTapPoints_.remove(id);
    log(QString("held tap up: key=%1 touch=%2").arg(keyName(key)).arg(id));
}

void IntegratedView::releaseAllTapTouches()
{
    const QHash<int, QPointF> touches = activeTapPoints_;
    for (auto touch = touches.cbegin(); touch != touches.cend(); ++touch)
        sendTouchPoint(touch.key(), touch.value(), Qt::TouchPointReleased);
    activeTapPoints_.clear();
    heldTapIdsByKey_.clear();
    activeMobaSkillTouchIds_.clear();
    if (!touches.isEmpty())
        log(QString("released active mapper tap touches=%1").arg(touches.size()));
}

void IntegratedView::beginMobaMovement(const QPointF &pointer)
{
    if (mobaMovementActive_ || !mobaMovement_.enabled || !characterCenter_.enabled)
        return;
    mobaMovementActive_ = true;
    mobaLastPointer_ = pointer;
    mobaLastTouch_ = {mobaMovement_.x, mobaMovement_.y};
    if (!sendTouchPoint(0, mobaLastTouch_, Qt::TouchPointPressed)) {
        mobaMovementActive_ = false;
        return;
    }

    // Give Android one frame to establish the touch at the joystick centre
    // before moving it to the requested direction.
    QTimer::singleShot(16, this, [this] {
        if (mobaMovementActive_)
            updateMobaMovement(mobaLastPointer_);
    });
    log(QString("MOBA RMB down: pointer=%1,%2 joystick=%3,%4")
            .arg(pointer.x()).arg(pointer.y())
            .arg(mobaMovement_.x).arg(mobaMovement_.y));
}

void IntegratedView::updateMobaMovement(const QPointF &pointer)
{
    if (!mobaMovementActive_)
        return;
    mobaLastPointer_ = pointer;
    const double dx = (pointer.x() - characterCenter_.x) * androidWidth_;
    const double dy = (pointer.y() - characterCenter_.y) * androidHeight_;
    const double length = std::hypot(dx, dy);
    if (length < 0.001) {
        mobaLastTouch_ = {mobaMovement_.x, mobaMovement_.y};
    } else {
        const double radiusPixels = mobaMovement_.radius
                                  * std::min(androidWidth_, androidHeight_);
        mobaLastTouch_ = {
            std::clamp(mobaMovement_.x + (dx / length) * radiusPixels / androidWidth_,
                       0.0, 1.0),
            std::clamp(mobaMovement_.y + (dy / length) * radiusPixels / androidHeight_,
                       0.0, 1.0)
        };
    }
    sendTouchPoint(0, mobaLastTouch_, Qt::TouchPointMoved);
}

void IntegratedView::endMobaMovement()
{
    if (!mobaMovementActive_)
        return;
    sendTouchPoint(0, mobaLastTouch_, Qt::TouchPointReleased);
    mobaMovementActive_ = false;
    log(QString("MOBA RMB up: touch=%1,%2")
            .arg(mobaLastTouch_.x()).arg(mobaLastTouch_.y()));
}

QPointF IntegratedView::mobaSkillVectorForPointer(int index,
                                                  const QPointF &pointer) const
{
    if (index < 0 || index >= static_cast<int>(mobaSkills_.size()))
        return {};
    const MobaSkillControl &skill = mobaSkills_[static_cast<std::size_t>(index)];
    if (static_cast<int>(skill.calibrationPoints.size()) != CalibrationSampleCount)
        return {};

    auto screenVertex = [this, &skill](int vertex) {
        return vertex == 0 ? QPointF(characterCenter_.x, characterCenter_.y)
                           : skill.calibrationPoints[static_cast<std::size_t>(vertex - 1)];
    };
    auto joystickVertex = [this](int vertex) {
        return vertex == 0 ? QPointF() : calibrationVector(vertex - 1);
    };
    auto pixelPoint = [this](const QPointF &point) {
        return QPointF(point.x() * androidWidth_, point.y() * androidHeight_);
    };

    const QPointF target = pixelPoint(pointer);
    auto interpolateTriangle = [&](int first, int second, int third,
                                   QPointF *result) {
        const QPointF a = pixelPoint(screenVertex(first));
        const QPointF b = pixelPoint(screenVertex(second));
        const QPointF c = pixelPoint(screenVertex(third));
        const double denominator = (b.y() - c.y()) * (a.x() - c.x())
                                 + (c.x() - b.x()) * (a.y() - c.y());
        if (std::abs(denominator) < 0.000001)
            return false;
        const double wa = ((b.y() - c.y()) * (target.x() - c.x())
                         + (c.x() - b.x()) * (target.y() - c.y())) / denominator;
        const double wb = ((c.y() - a.y()) * (target.x() - c.x())
                         + (a.x() - c.x()) * (target.y() - c.y())) / denominator;
        const double wc = 1.0 - wa - wb;
        constexpr double EdgeTolerance = -0.0001;
        if (wa < EdgeTolerance || wb < EdgeTolerance || wc < EdgeTolerance)
            return false;
        *result = joystickVertex(first) * wa
                + joystickVertex(second) * wb
                + joystickVertex(third) * wc;
        return true;
    };

    QPointF mapped;
    for (int direction = 0; direction < CalibrationDirections; ++direction) {
        const int next = (direction + 1) % CalibrationDirections;
        if (interpolateTriangle(0, 1 + direction, 1 + next, &mapped))
            return mapped;
    }
    for (int ring = 0; ring < CalibrationRings - 1; ++ring) {
        const int innerOffset = 1 + ring * CalibrationDirections;
        const int outerOffset = innerOffset + CalibrationDirections;
        for (int direction = 0; direction < CalibrationDirections; ++direction) {
            const int next = (direction + 1) % CalibrationDirections;
            const int inner = innerOffset + direction;
            const int innerNext = innerOffset + next;
            const int outer = outerOffset + direction;
            const int outerNext = outerOffset + next;
            if (interpolateTriangle(inner, outer, outerNext, &mapped)
                || interpolateTriangle(inner, outerNext, innerNext, &mapped))
                return mapped;
        }
    }

    // Outside the calibrated range, clamp to the nearest edge of the measured
    // outer ring instead of extrapolating perspective errors without bounds.
    double bestDistance = std::numeric_limits<double>::max();
    QPointF bestVector;
    const int outerOffset = 1 + (CalibrationRings - 1) * CalibrationDirections;
    for (int direction = 0; direction < CalibrationDirections; ++direction) {
        const int next = (direction + 1) % CalibrationDirections;
        const QPointF a = pixelPoint(screenVertex(outerOffset + direction));
        const QPointF b = pixelPoint(screenVertex(outerOffset + next));
        const QPointF segment = b - a;
        const double squaredLength = QPointF::dotProduct(segment, segment);
        const double amount = squaredLength < 0.000001 ? 0.0
            : std::clamp(QPointF::dotProduct(target - a, segment) / squaredLength,
                         0.0, 1.0);
        const QPointF closest = a + segment * amount;
        const QPointF difference = target - closest;
        const double distance = QPointF::dotProduct(difference, difference);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestVector = joystickVertex(outerOffset + direction) * (1.0 - amount)
                       + joystickVertex(outerOffset + next) * amount;
        }
    }
    const double length = std::hypot(bestVector.x(), bestVector.y());
    return length > 1.0 ? bestVector / length : bestVector;
}

void IntegratedView::beginMobaSkill(int index, const QPointF &pointer)
{
    if (index < 0 || index >= static_cast<int>(mobaSkills_.size())
        || activeMobaSkillTouchIds_.contains(index))
        return;
    const MobaSkillControl &skill = mobaSkills_[static_cast<std::size_t>(index)];
    if (!characterCenter_.enabled) {
        emit statusChanged("MOBA skill needs Character center. Press F5 and add the cross.");
        log(QString("MOBA skill ignored: index=%1 Character center missing").arg(index));
        return;
    }
    if (static_cast<int>(skill.calibrationPoints.size()) != CalibrationSampleCount) {
        emit statusChanged("MOBA skill is not calibrated. Press F5, open its gear and calibrate it.");
        log(QString("MOBA skill ignored: index=%1 calibration missing").arg(index));
        return;
    }
    const int touchId = allocateTouchId();
    if (touchId < 0)
        return;
    const QPointF center(skill.x, skill.y);
    if (!sendTouchPoint(touchId, center, Qt::TouchPointPressed))
        return;
    activeMobaSkillTouchIds_.insert(index, touchId);
    activeTapPoints_.insert(touchId, center);
    QTimer::singleShot(16, this, [this, index, pointer] {
        if (activeMobaSkillTouchIds_.contains(index))
            updateMobaSkills(pointer);
    });
    log(QString("MOBA skill down: index=%1 key=%2 touch=%3")
            .arg(index).arg(keyName(skill.key)).arg(touchId));
}

void IntegratedView::updateMobaSkills(const QPointF &pointer)
{
    const QHash<int, int> active = activeMobaSkillTouchIds_;
    for (auto item = active.cbegin(); item != active.cend(); ++item) {
        const int index = item.key();
        const int touchId = item.value();
        if (index < 0 || index >= static_cast<int>(mobaSkills_.size()))
            continue;
        const MobaSkillControl &skill = mobaSkills_[static_cast<std::size_t>(index)];
        const QPointF vector = mobaSkillVectorForPointer(index, pointer);
        const double radiusPixels = skill.radius * std::min(androidWidth_, androidHeight_);
        const QPointF touch(
            std::clamp(skill.x + vector.x() * radiusPixels / androidWidth_, 0.0, 1.0),
            std::clamp(skill.y + vector.y() * radiusPixels / androidHeight_, 0.0, 1.0));
        if (sendTouchPoint(touchId, touch, Qt::TouchPointMoved))
            activeTapPoints_[touchId] = touch;
    }
}

void IntegratedView::endMobaSkill(int index)
{
    const auto active = activeMobaSkillTouchIds_.find(index);
    if (active == activeMobaSkillTouchIds_.end())
        return;
    const int touchId = active.value();
    const QPointF point = activeTapPoints_.value(touchId);
    sendTouchPoint(touchId, point, Qt::TouchPointReleased);
    activeMobaSkillTouchIds_.erase(active);
    activeTapPoints_.remove(touchId);
    log(QString("MOBA skill cast: index=%1 touch=%2").arg(index).arg(touchId));
}

void IntegratedView::releaseAllMobaSkillTouches()
{
    const QList<int> indexes = activeMobaSkillTouchIds_.keys();
    for (int index : indexes)
        endMobaSkill(index);
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
    if (calibrationActive())
        cancelMobaSkillCalibration();
    if (mobaMovementActive_)
        endMobaMovement();
    setBusy(true);
    setReady(false);
    if (editMode_) {
        bindings_ = editSnapshot_;
        characterCenter_ = characterCenterSnapshot_;
        mobaMovement_ = mobaMovementSnapshot_;
        mobaSkills_ = mobaSkillsSnapshot_;
        editSnapshot_.clear();
        mobaSkillsSnapshot_.clear();
        emit bindingsChanged();
        emit characterCenterChanged();
        emit mobaMovementChanged();
        emit mobaSkillsChanged();
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
        invalidateMobaSkillCalibrations(
            "Android resolution changed — MOBA skills must be recalibrated");
        emit resolutionChanged();
        emit bindingsChanged();
        emit characterCenterChanged();
        emit mobaMovementChanged();
        emit mobaSkillsChanged();
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

void IntegratedView::surfaceReady(QObject *surfaceObject)
{
    log("EVENT: Android xdg_toplevel surface arrived");
    auto *surface = qobject_cast<QWaylandSurface *>(surfaceObject);
    if (!surface) {
        failOperation("Android surface has an unexpected Qt type.");
        return;
    }
    inputSurface_ = surface;
    connect(surface, &QWaylandSurface::surfaceDestroyed, this, [this, surface] {
        if (inputSurface_ != surface)
            return;
        inputSurface_.clear();
        mobaMovementActive_ = false;
        activeTapPoints_.clear();
        heldTapIdsByKey_.clear();
        activeMobaSkillTouchIds_.clear();
        if (calibrationActive()) {
            const int index = calibrationSkillIndex_;
            if (index >= 0 && index < static_cast<int>(mobaSkills_.size()))
                mobaSkills_[static_cast<std::size_t>(index)].calibrationPoints =
                    calibrationBackupPoints_;
            calibrationBackupPoints_.clear();
            calibrationSkillIndex_ = -1;
            calibrationStep_ = 0;
            calibrationTouchId_ = -1;
            calibrationPointReady_ = false;
            emit mobaSkillsChanged();
            emit selectedMobaSkillChanged();
            emit calibrationChanged();
        }
        log("Android input surface destroyed; local touch state cleared");
    });
    if (!waitingForSurface_) {
        log("surface stored for input; readiness event ignored because none was requested");
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
    if (calibrationActive())
        cancelMobaSkillCalibration();
    if (mobaMovementActive_)
        endMobaMovement();
    if (editMode_) {
        bindings_ = editSnapshot_;
        characterCenter_ = characterCenterSnapshot_;
        mobaMovement_ = mobaMovementSnapshot_;
        mobaSkills_ = mobaSkillsSnapshot_;
        editSnapshot_.clear();
        mobaSkillsSnapshot_.clear();
        emit bindingsChanged();
        emit characterCenterChanged();
        emit mobaMovementChanged();
        emit mobaSkillsChanged();
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
    if (calibrationActive())
        cancelMobaSkillCalibration();
    if (mobaMovementActive_)
        endMobaMovement();
    if (!activeTapPoints_.isEmpty())
        releaseAllTapTouches();
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
