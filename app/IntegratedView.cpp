#include "IntegratedView.h"

#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWaylandCompositor>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <QWindow>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <unistd.h>

namespace {
constexpr auto NestedSocket = "evgenium-wayland-0";
constexpr auto WaydroidContainerUnit = "waydroid-container.service";
constexpr int SessionReadyTimeoutMs = 45000;
constexpr int SessionReadySettleMs = 300;
constexpr int ServicePollIntervalMs = 300;
constexpr int ServicePollAttempts = 20;
constexpr double Pi = 3.14159265358979323846;

bool writeCircularAvatar(const QString &sourcePath, const QString &destination)
{
    QImage sourceImage(sourcePath);
    if (sourceImage.isNull())
        return false;
    const int cropSide = std::min(sourceImage.width(), sourceImage.height());
    const QRect cropRect((sourceImage.width() - cropSide) / 2,
                         (sourceImage.height() - cropSide) / 2,
                         cropSide, cropSide);
    constexpr int AvatarSize = 512;
    QImage circularAvatar(AvatarSize, AvatarSize, QImage::Format_ARGB32_Premultiplied);
    circularAvatar.fill(Qt::transparent);
    QPainter painter(&circularAvatar);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath circlePath;
    circlePath.addEllipse(QRectF(0.0, 0.0, AvatarSize, AvatarSize));
    painter.setClipPath(circlePath);
    painter.drawImage(circularAvatar.rect(), sourceImage, cropRect);
    painter.end();
    return circularAvatar.save(destination, "PNG");
}
}

IntegratedView::IntegratedView(QObject *parent)
    : QObject(parent), engine_(new QQmlApplicationEngine(this)),
      sessionProcess_(new QProcess(this))
{
    engine_->rootContext()->setContextProperty("integratedBackend", this);
    QCoreApplication::instance()->installEventFilter(this);
    QSettings settings;
    androidWidth_ = std::clamp(settings.value("session/lastWidth", 1920).toInt(),
                               320, 7680);
    androidHeight_ = std::clamp(settings.value("session/lastHeight", 1080).toInt(),
                                320, 7680);
    loadBindings();

    connect(sessionProcess_, &QProcess::readyReadStandardOutput, this, [this] {
        handleSessionOutput("stdout",
            QString::fromUtf8(sessionProcess_->readAllStandardOutput()));
    });
    connect(sessionProcess_, &QProcess::readyReadStandardError, this, [this] {
        handleSessionOutput("stderr",
            QString::fromUtf8(sessionProcess_->readAllStandardError()));
    });
    connect(sessionProcess_, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
        log(QString("session process finished: code=%1 status=%2")
                .arg(code).arg(static_cast<int>(status)));
        if (sessionStartPending_) {
            const QString purpose = sessionStartPurpose_;
            handleSessionStartFailure(
                purpose,
                QString("The Waydroid %1 session exited before Android became ready.")
                    .arg(purpose));
        }
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
    log(QString("loaded profile='%1' designed=%2x%3 selected=%4x%5")
            .arg(activeProfileName_).arg(profileResolutionWidth_)
            .arg(profileResolutionHeight_).arg(androidWidth_).arg(androidHeight_));
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
        {"holdThresholdMs", mobaMovement_.holdThresholdMs},
        {"clickDistancePercent", qRound(mobaMovement_.clickDistanceModifier * 100.0)},
        {"requiresCenter", true},
        {"ready", mobaMovement_.enabled && characterCenter_.enabled}
    };
}

QVariantMap IntegratedView::skillCancel() const
{
    return {
        {"exists", skillCancel_.enabled},
        {"x", skillCancel_.x},
        {"y", skillCancel_.y},
        {"pixelX", qRound(skillCancel_.x * androidWidth_)},
        {"pixelY", qRound(skillCancel_.y * androidHeight_)},
        {"key", skillCancel_.key},
        {"keyName", keyName(skillCancel_.key)},
        {"ready", skillCancel_.enabled && skillCancel_.key != 0}
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
            {"speedLevel", skill.speedLevel},
            {"artificialCenterEnabled", skill.artificialCenterEnabled},
            {"artificialX", skill.artificialX},
            {"artificialY", skill.artificialY},
            {"artificialPixelX", qRound(skill.artificialX * androidWidth_)},
            {"artificialPixelY", qRound(skill.artificialY * androidHeight_)},
            {"calibrated", static_cast<int>(skill.calibrationPoints.size())
                            == CalibrationSampleCount},
            {"calibrationStale", skill.calibrationStale},
            {"calibrationRecoveryAvailable", skill.recoveryValid},
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

bool IntegratedView::profileResolutionCompatible() const
{
    const MapperProfile *profile = findProfile(activeProfileId_);
    return profile && profileSupportsResolution(*profile, androidWidth_, androidHeight_);
}

QString IntegratedView::profileResolutionWarning() const
{
    if (profileResolutionCompatible())
        return {};
    return QString("⚠ '%1' has no variant for %2 × %3 — press F6")
        .arg(activeProfileName_).arg(androidWidth_).arg(androidHeight_);
}

QString IntegratedView::resolutionKey(int width, int height) const
{
    return QString("%1x%2").arg(width).arg(height);
}

bool IntegratedView::parseResolutionKey(const QString &key, int *width, int *height) const
{
    const QStringList parts = key.split('x');
    bool widthOk = false;
    bool heightOk = false;
    const int parsedWidth = parts.value(0).toInt(&widthOk);
    const int parsedHeight = parts.value(1).toInt(&heightOk);
    if (!widthOk || !heightOk || parsedWidth < 320 || parsedWidth > 7680
        || parsedHeight < 320 || parsedHeight > 7680)
        return false;
    if (width)
        *width = parsedWidth;
    if (height)
        *height = parsedHeight;
    return true;
}

IntegratedView::MapperProfile *IntegratedView::findProfile(const QString &profileId)
{
    const auto found = std::find_if(profiles_.begin(), profiles_.end(),
        [&profileId](const MapperProfile &profile) { return profile.id == profileId; });
    return found == profiles_.end() ? nullptr : &*found;
}

const IntegratedView::MapperProfile *IntegratedView::findProfile(
    const QString &profileId) const
{
    const auto found = std::find_if(profiles_.cbegin(), profiles_.cend(),
        [&profileId](const MapperProfile &profile) { return profile.id == profileId; });
    return found == profiles_.cend() ? nullptr : &*found;
}

bool IntegratedView::profileSupportsResolution(const MapperProfile &profile,
                                                int width, int height) const
{
    return profile.resolutions.contains(resolutionKey(width, height));
}

QString IntegratedView::closestProfileResolution(const MapperProfile &profile,
                                                  int width, int height) const
{
    QString best;
    double bestScore = std::numeric_limits<double>::max();
    const double targetAspect = width / static_cast<double>(std::max(1, height));
    const double targetArea = std::max(1.0, static_cast<double>(width) * height);
    for (const QString &key : profile.resolutions) {
        int candidateWidth = 0;
        int candidateHeight = 0;
        if (!parseResolutionKey(key, &candidateWidth, &candidateHeight))
            continue;
        const double aspect = candidateWidth
                            / static_cast<double>(std::max(1, candidateHeight));
        const double area = static_cast<double>(candidateWidth) * candidateHeight;
        const double score = std::abs(std::log(aspect / targetAspect)) * 4.0
                           + std::abs(std::log(area / targetArea));
        if (score < bestScore) {
            bestScore = score;
            best = key;
        }
    }
    return best;
}

QVariantList IntegratedView::profiles() const
{
    QVariantList result;
    const QString currentKey = resolutionKey(androidWidth_, androidHeight_);
    for (const MapperProfile &profile : profiles_) {
        QStringList readableResolutions;
        for (const QString &key : profile.resolutions) {
            int width = 0;
            int height = 0;
            if (parseResolutionKey(key, &width, &height))
                readableResolutions.append(QString("%1 × %2").arg(width).arg(height));
        }
        const bool supported = profile.resolutions.contains(currentKey);
        result.append(QVariantMap{
            {"id", profile.id},
            {"name", profile.name},
            {"letter", profile.name.trimmed().left(1).toUpper()},
            {"imageUrl", profile.imagePath.isEmpty()
                ? QString() : QUrl::fromLocalFile(profile.imagePath).toString()},
            {"isDefault", profile.isDefault},
            {"active", profile.id == activeProfileId_},
            {"supported", supported},
            {"supportedResolutions", readableResolutions},
            {"currentResolution", QString("%1 × %2").arg(androidWidth_).arg(androidHeight_)},
            {"statusText", supported
                ? QString("Ready for %1 × %2").arg(androidWidth_).arg(androidHeight_)
                : (profile.isDefault
                    ? QString("Fixed to %1").arg(readableResolutions.value(0, "—"))
                    : QString("Not configured for %1 × %2")
                        .arg(androidWidth_).arg(androidHeight_))}
        });
    }
    return result;
}

QVariantMap IntegratedView::pendingProfile() const
{
    const MapperProfile *profile = findProfile(pendingProfileId_);
    if (!profile)
        return {};
    return {
        {"id", profile->id},
        {"name", profile->name},
        {"isDefault", profile->isDefault},
        {"canAdapt", !profile->isDefault && !profile->resolutions.isEmpty()},
        {"sourceResolution", pendingProfileSourceWidth_ > 0
            ? QString("%1 × %2").arg(pendingProfileSourceWidth_)
                                      .arg(pendingProfileSourceHeight_)
            : QString("—")},
        {"targetResolution", QString("%1 × %2").arg(androidWidth_).arg(androidHeight_)}
    };
}

void IntegratedView::loadControls(QSettings &settings)
{
    const int count = settings.beginReadArray("tapBindings");
    bindings_.clear();
    for (int index = 0; index < count; ++index) {
        settings.setArrayIndex(index);
        TapBinding binding;
        binding.x = settings.value("x").toDouble();
        binding.y = settings.value("y").toDouble();
        binding.key = settings.value("key").toInt();
        binding.mode = settings.value("mode", TapBinding::HoldUntilKeyRelease).toInt()
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
    mobaMovement_.holdThresholdMs = std::clamp(
        settings.value("holdThresholdMs", 120).toInt(), 30, 500);
    mobaMovement_.clickDistanceModifier = std::clamp(
        settings.value("clickDistanceModifier", 1.0).toDouble(), 0.1, 5.0);
    settings.endGroup();

    settings.beginGroup("skillCancel");
    skillCancel_.enabled = settings.value("enabled", false).toBool();
    skillCancel_.x = std::clamp(settings.value("x", 0.88).toDouble(), 0.0, 1.0);
    skillCancel_.y = std::clamp(settings.value("y", 0.18).toDouble(), 0.0, 1.0);
    skillCancel_.key = settings.value("key", 0).toInt();
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
        skill.speedLevel = std::clamp(settings.value("speedLevel", 4).toInt(),
                                      1, 5);
        skill.artificialCenterEnabled = settings.value(
            "artificialCenterEnabled", false).toBool();
        skill.artificialX = std::clamp(
            settings.value("artificialX", skill.x).toDouble(), 0.0, 1.0);
        skill.artificialY = std::clamp(
            settings.value("artificialY", skill.y).toDouble(), 0.0, 1.0);
        skill.calibrationStale = settings.value("calibrationStale", false).toBool();
        skill.recoveryValid = settings.value("recoveryValid", false).toBool();
        skill.recoveryX = std::clamp(settings.value("recoveryX", skill.x).toDouble(),
                                     0.0, 1.0);
        skill.recoveryY = std::clamp(settings.value("recoveryY", skill.y).toDouble(),
                                     0.0, 1.0);
        skill.recoveryRadius = std::clamp(
            settings.value("recoveryRadius", skill.radius).toDouble(), 0.02, 0.35);
        skill.recoveryArtificialCenterEnabled = settings.value(
            "recoveryArtificialCenterEnabled", skill.artificialCenterEnabled).toBool();
        skill.recoveryArtificialX = std::clamp(settings.value(
            "recoveryArtificialX", skill.artificialX).toDouble(), 0.0, 1.0);
        skill.recoveryArtificialY = std::clamp(settings.value(
            "recoveryArtificialY", skill.artificialY).toDouble(), 0.0, 1.0);
        skill.recoveryCharacterCenterEnabled = settings.value(
            "recoveryCharacterCenterEnabled", characterCenter_.enabled).toBool();
        skill.recoveryCharacterCenterX = std::clamp(settings.value(
            "recoveryCharacterCenterX", characterCenter_.x).toDouble(), 0.0, 1.0);
        skill.recoveryCharacterCenterY = std::clamp(settings.value(
            "recoveryCharacterCenterY", characterCenter_.y).toDouble(), 0.0, 1.0);
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
        const QStringList encodedRecoveryPoints =
            settings.value("recoveryCalibrationPoints").toStringList();
        for (const QString &encoded : encodedRecoveryPoints) {
            const QStringList coordinates = encoded.split(',');
            bool xOk = false;
            bool yOk = false;
            const double x = coordinates.value(0).toDouble(&xOk);
            const double y = coordinates.value(1).toDouble(&yOk);
            if (xOk && yOk && x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0)
                skill.recoveryCalibrationPoints.emplace_back(x, y);
        }
        if (static_cast<int>(skill.recoveryCalibrationPoints.size())
                != CalibrationSampleCount) {
            skill.recoveryCalibrationPoints.clear();
            skill.recoveryValid = false;
        }
        if (skill.calibrationPoints.empty())
            skill.calibrationStale = false;
        mobaSkills_.push_back(std::move(skill));
    }
    settings.endArray();
}

void IntegratedView::saveControls(QSettings &settings) const
{
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
    settings.setValue("holdThresholdMs", mobaMovement_.holdThresholdMs);
    settings.setValue("clickDistanceModifier", mobaMovement_.clickDistanceModifier);
    settings.endGroup();

    settings.beginGroup("skillCancel");
    settings.setValue("enabled", skillCancel_.enabled);
    settings.setValue("x", skillCancel_.x);
    settings.setValue("y", skillCancel_.y);
    settings.setValue("key", skillCancel_.key);
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
        settings.setValue("speedLevel", skill.speedLevel);
        settings.setValue("artificialCenterEnabled", skill.artificialCenterEnabled);
        settings.setValue("artificialX", skill.artificialX);
        settings.setValue("artificialY", skill.artificialY);
        settings.setValue("calibrationStale", skill.calibrationStale);
        settings.setValue("recoveryValid", skill.recoveryValid);
        settings.setValue("recoveryX", skill.recoveryX);
        settings.setValue("recoveryY", skill.recoveryY);
        settings.setValue("recoveryRadius", skill.recoveryRadius);
        settings.setValue("recoveryArtificialCenterEnabled",
                          skill.recoveryArtificialCenterEnabled);
        settings.setValue("recoveryArtificialX", skill.recoveryArtificialX);
        settings.setValue("recoveryArtificialY", skill.recoveryArtificialY);
        settings.setValue("recoveryCharacterCenterEnabled",
                          skill.recoveryCharacterCenterEnabled);
        settings.setValue("recoveryCharacterCenterX",
                          skill.recoveryCharacterCenterX);
        settings.setValue("recoveryCharacterCenterY",
                          skill.recoveryCharacterCenterY);
        QStringList encodedPoints;
        encodedPoints.reserve(static_cast<qsizetype>(skill.calibrationPoints.size()));
        for (const QPointF &point : skill.calibrationPoints) {
            encodedPoints.append(QString::number(point.x(), 'g', 17)
                                 + ',' + QString::number(point.y(), 'g', 17));
        }
        settings.setValue("calibrationPoints", encodedPoints);
        QStringList encodedRecoveryPoints;
        encodedRecoveryPoints.reserve(
            static_cast<qsizetype>(skill.recoveryCalibrationPoints.size()));
        for (const QPointF &point : skill.recoveryCalibrationPoints) {
            encodedRecoveryPoints.append(QString::number(point.x(), 'g', 17)
                                         + ',' + QString::number(point.y(), 'g', 17));
        }
        settings.setValue("recoveryCalibrationPoints", encodedRecoveryPoints);
    }
    settings.endArray();
}

void IntegratedView::saveProfileMetadata(const MapperProfile &profile) const
{
    QSettings settings;
    settings.beginGroup("profiles/" + profile.id);
    settings.setValue("schemaVersion", 2);
    settings.setValue("name", profile.name);
    settings.setValue("imagePath", profile.imagePath);
    settings.setValue("isDefault", profile.isDefault);
    settings.setValue("order", profile.order);
    settings.setValue("resolutionKeys", profile.resolutions);
    int width = 0;
    int height = 0;
    parseResolutionKey(profile.resolutions.value(0), &width, &height);
    settings.setValue("resolutionWidth", width);
    settings.setValue("resolutionHeight", height);
    settings.endGroup();
    settings.sync();
}

void IntegratedView::saveBindings() const
{
    const MapperProfile *profile = findProfile(activeProfileId_);
    if (!profile || !profileSupportsResolution(*profile, androidWidth_, androidHeight_)) {
        log("mapper save skipped: active profile has no current-resolution variant");
        return;
    }
    QSettings settings;
    settings.setValue("profiles/activeId", activeProfileId_);
    settings.beginGroup("profiles/" + activeProfileId_ + "/variants/"
                        + resolutionKey(androidWidth_, androidHeight_));
    settings.remove("");
    settings.setValue("variantSchema", 1);
    saveControls(settings);
    settings.endGroup();
    settings.sync();
}

bool IntegratedView::loadProfileVariant(const QString &profileId,
                                        const QString &variantKey)
{
    QSettings settings;
    const QString group = "profiles/" + profileId + "/variants/" + variantKey;
    if (settings.value(group + "/variantSchema", 0).toInt() < 1)
        return false;
    // Switching profiles is an atomic control-set replacement. Release every
    // synthetic finger and discard editor selection state before reading the
    // new variant so no gesture from the previous character can survive it.
    clearControls();
    settings.beginGroup(group);
    loadControls(settings);
    settings.endGroup();
    emitAllControlsChanged();
    return true;
}

void IntegratedView::loadBindings()
{
    QSettings settings;
    activeProfileId_ = settings.value("profiles/activeId", "default").toString();
    if (activeProfileId_.isEmpty() || activeProfileId_.contains('/'))
        activeProfileId_ = "default";

    settings.beginGroup("profiles");
    const QStringList profileIds = settings.childGroups();
    settings.endGroup();
    bool controlsLoaded = false;
    QString schemaOneProfile;

    for (const QString &profileId : profileIds) {
        if (profileId.contains('/'))
            continue;
        settings.beginGroup("profiles/" + profileId);
        const int schema = settings.value("schemaVersion", 0).toInt();
        if (schema < 1) {
            settings.endGroup();
            continue;
        }
        MapperProfile profile;
        profile.id = profileId;
        profile.name = settings.value("name",
            profileId == "default" ? "Default" : "Empty profile").toString();
        profile.imagePath = settings.value("imagePath").toString();
        if (!profile.imagePath.isEmpty() && QFileInfo::exists(profile.imagePath)) {
            const QImage storedAvatar(profile.imagePath);
            const bool alreadyCircular = storedAvatar.width() == 512
                && storedAvatar.height() == 512
                && storedAvatar.hasAlphaChannel()
                && qAlpha(storedAvatar.pixel(0, 0)) == 0;
            if (!alreadyCircular) {
                const QString directory = QStandardPaths::writableLocation(
                    QStandardPaths::AppDataLocation) + "/profile-images";
                const QString destination = directory + '/' + profile.id + '-'
                    + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
                if (QDir().mkpath(directory)
                    && writeCircularAvatar(profile.imagePath, destination)) {
                    profile.imagePath = destination;
                    settings.setValue("imagePath", destination);
                }
            }
        }
        profile.isDefault = settings.value("isDefault", profileId == "default").toBool();
        profile.order = settings.value("order", profile.isDefault ? 0 : 100).toInt();
        if (schema >= 2) {
            const QStringList storedKeys = settings.value("resolutionKeys").toStringList();
            for (const QString &key : storedKeys) {
                int width = 0;
                int height = 0;
                if (parseResolutionKey(key, &width, &height)) {
                    const QString normalized = resolutionKey(width, height);
                    if (!profile.resolutions.contains(normalized))
                        profile.resolutions.append(normalized);
                }
            }
            settings.beginGroup("variants");
            const QStringList variantGroups = settings.childGroups();
            settings.endGroup();
            for (const QString &key : variantGroups) {
                int width = 0;
                int height = 0;
                if (parseResolutionKey(key, &width, &height)
                    && !profile.resolutions.contains(resolutionKey(width, height)))
                    profile.resolutions.append(resolutionKey(width, height));
            }
        } else {
            const int width = settings.value("resolutionWidth", 0).toInt();
            const int height = settings.value("resolutionHeight", 0).toInt();
            if (width >= 320 && height >= 320)
                profile.resolutions.append(resolutionKey(width, height));
            if (profileId == activeProfileId_) {
                loadControls(settings);
                controlsLoaded = true;
                schemaOneProfile = profileId;
            }
        }
        profiles_.push_back(std::move(profile));
        settings.endGroup();
    }

    if (profiles_.empty()) {
        MapperProfile profile;
        profile.id = "default";
        profile.name = "Default";
        profile.isDefault = true;
        profile.order = 0;
        profiles_.push_back(profile);
        activeProfileId_ = "default";
        loadControls(settings);
        controlsLoaded = true;
    }

    std::sort(profiles_.begin(), profiles_.end(),
        [](const MapperProfile &left, const MapperProfile &right) {
            if (left.isDefault != right.isDefault)
                return left.isDefault;
            if (left.order != right.order)
                return left.order < right.order;
            return left.name.localeAwareCompare(right.name) < 0;
        });

    MapperProfile *active = findProfile(activeProfileId_);
    if (!active) {
        active = &profiles_.front();
        activeProfileId_ = active->id;
    }
    activeProfileName_ = active->name;

    QString loadedKey;
    if (!controlsLoaded) {
        const QString currentKey = resolutionKey(androidWidth_, androidHeight_);
        loadedKey = active->resolutions.contains(currentKey)
                  ? currentKey : closestProfileResolution(*active, androidWidth_, androidHeight_);
        if (!loadedKey.isEmpty())
            controlsLoaded = loadProfileVariant(active->id, loadedKey);
    } else if (!active->resolutions.isEmpty()) {
        loadedKey = active->resolutions.front();
    }
    if (!controlsLoaded)
        clearControls();

    profileResolutionWidth_ = 0;
    profileResolutionHeight_ = 0;
    parseResolutionKey(loadedKey, &profileResolutionWidth_, &profileResolutionHeight_);
    settings.setValue("profiles/activeId", activeProfileId_);

    if (!schemaOneProfile.isEmpty() && !loadedKey.isEmpty()) {
        saveProfileMetadata(*active);
        settings.beginGroup("profiles/" + active->id + "/variants/" + loadedKey);
        settings.remove("");
        settings.setValue("variantSchema", 1);
        saveControls(settings);
        settings.endGroup();
        settings.sync();
        log(QString("migrated profile '%1' from schema 1 to resolution variants")
                .arg(active->name));
    }
}

void IntegratedView::clearControls()
{
    cancelMobaMovementGesture();
    if (!activeTapPoints_.isEmpty())
        releaseAllTapTouches();
    bindings_.clear();
    characterCenter_ = {};
    mobaMovement_ = {};
    skillCancel_ = {};
    mobaSkills_.clear();
    selectedBindingIndex_ = -1;
    selectedMobaSkillIndex_ = -1;
    setWaitingForKey(false);
}

void IntegratedView::emitAllControlsChanged()
{
    emit bindingsChanged();
    emit selectedBindingChanged();
    emit characterCenterChanged();
    emit mobaMovementChanged();
    emit skillCancelChanged();
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
    emit calibrationChanged();
}

void IntegratedView::activateProfileVariant(MapperProfile &profile,
                                             const QString &variantKey,
                                             bool persistVariant)
{
    if (persistVariant && !profile.resolutions.contains(variantKey))
        profile.resolutions.append(variantKey);
    activeProfileId_ = profile.id;
    activeProfileName_ = profile.name;
    profileResolutionWidth_ = 0;
    profileResolutionHeight_ = 0;
    parseResolutionKey(variantKey, &profileResolutionWidth_, &profileResolutionHeight_);
    QSettings settings;
    settings.setValue("profiles/activeId", activeProfileId_);
    settings.sync();
    if (persistVariant)
        saveProfileMetadata(profile);
    pendingProfileId_.clear();
    pendingProfileSourceWidth_ = 0;
    pendingProfileSourceHeight_ = 0;
    emit pendingProfileChanged();
    emit profileChanged();
    emit profilesChanged();
}

void IntegratedView::setProfileManagerVisible(bool visible)
{
    if (profileManagerVisible_ == visible)
        return;
    if (visible) {
        setCursorLocked(false);
        cancelMobaMovementGesture();
        if (!activeTapPoints_.isEmpty())
            releaseAllTapTouches();
    } else {
        pendingProfileId_.clear();
        pendingProfileSourceWidth_ = 0;
        pendingProfileSourceHeight_ = 0;
        emit pendingProfileChanged();
    }
    profileManagerVisible_ = visible;
    emit profileManagerVisibleChanged();
}

void IntegratedView::toggleProfileManager()
{
    if (!windowVisible_)
        return;
    if (editMode_ || calibrationActive()) {
        emit statusChanged("Finish mapper editing or calibration before opening profiles.");
        return;
    }
    setProfileManagerVisible(!profileManagerVisible_);
}

void IntegratedView::closeProfileManager()
{
    setProfileManagerVisible(false);
}

void IntegratedView::createProfile()
{
    int number = 1;
    QString name;
    do {
        name = QString("Пустой профиль %1").arg(number++);
    } while (std::any_of(profiles_.cbegin(), profiles_.cend(),
        [&name](const MapperProfile &profile) { return profile.name == name; }));

    MapperProfile profile;
    profile.id = "profile-" + QUuid::createUuid().toString(QUuid::Id128);
    profile.name = name;
    profile.isDefault = false;
    profile.order = profiles_.empty() ? 1
        : std::max_element(profiles_.cbegin(), profiles_.cend(),
            [](const MapperProfile &left, const MapperProfile &right) {
                return left.order < right.order;
            })->order + 1;
    const QString key = resolutionKey(androidWidth_, androidHeight_);
    profile.resolutions.append(key);
    profiles_.push_back(profile);
    MapperProfile &created = profiles_.back();
    clearControls();
    emitAllControlsChanged();
    saveProfileMetadata(created);
    activateProfileVariant(created, key, true);
    saveBindings();
    emit statusChanged(QString("Created '%1' for %2 × %3. Press F5 to configure it.")
        .arg(created.name).arg(androidWidth_).arg(androidHeight_));
    log(QString("profile created id=%1 name='%2' resolution=%3")
            .arg(created.id, created.name, key));
}

void IntegratedView::duplicateProfile(const QString &profileId)
{
    const MapperProfile *sourceProfile = findProfile(profileId);
    if (!sourceProfile)
        return;

    QString baseName = sourceProfile->name + QStringLiteral(" — копия");
    QString copyName = baseName;
    int number = 2;
    while (std::any_of(profiles_.cbegin(), profiles_.cend(),
                       [&copyName](const MapperProfile &profile) {
        return profile.name == copyName;
    })) {
        copyName = QStringLiteral("%1 %2").arg(baseName).arg(number++);
    }

    MapperProfile copy = *sourceProfile;
    copy.id = "profile-" + QUuid::createUuid().toString(QUuid::Id128);
    copy.name = copyName;
    copy.isDefault = false;
    copy.order = profiles_.empty() ? 1
        : std::max_element(profiles_.cbegin(), profiles_.cend(),
            [](const MapperProfile &left, const MapperProfile &right) {
                return left.order < right.order;
            })->order + 1;

    QSettings settings;
    QHash<QString, QVariant> variantValues;
    settings.beginGroup("profiles/" + sourceProfile->id + "/variants");
    for (const QString &key : settings.allKeys())
        variantValues.insert(key, settings.value(key));
    settings.endGroup();

    if (!sourceProfile->imagePath.isEmpty()
        && QFileInfo::exists(sourceProfile->imagePath)) {
        const QString directory = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation) + "/profile-images";
        if (QDir().mkpath(directory)) {
            const QString destination = directory + '/' + copy.id + '-'
                + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
            copy.imagePath = QFile::copy(sourceProfile->imagePath, destination)
                           ? destination : QString();
        } else {
            copy.imagePath.clear();
        }
    }

    profiles_.push_back(copy);
    MapperProfile &created = profiles_.back();
    saveProfileMetadata(created);
    settings.beginGroup("profiles/" + created.id + "/variants");
    for (auto item = variantValues.cbegin(); item != variantValues.cend(); ++item)
        settings.setValue(item.key(), item.value());
    settings.endGroup();
    settings.sync();
    emit profilesChanged();
    emit statusChanged(QString("Created profile copy '%1'.").arg(created.name));
    log(QString("profile duplicated source=%1 target=%2 variants=%3")
            .arg(profileId, created.id).arg(created.resolutions.size()));
}

void IntegratedView::deleteProfile(const QString &profileId)
{
    if (editMode_ || calibrationActive())
        return;
    const auto removed = std::find_if(profiles_.begin(), profiles_.end(),
        [&profileId](const MapperProfile &profile) {
            return profile.id == profileId;
        });
    if (removed == profiles_.end())
        return;
    if (removed->isDefault) {
        emit statusChanged("Default is permanent and cannot be deleted.");
        return;
    }

    const bool deletingActive = removed->id == activeProfileId_;
    const QString removedName = removed->name;
    const QString removedImage = removed->imagePath;
    if (deletingActive)
        saveBindings();

    QSettings settings;
    settings.remove("profiles/" + removed->id);
    profiles_.erase(removed);

    const QString imageDirectory = QDir::cleanPath(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/profile-images");
    const QString imagePath = QDir::cleanPath(QFileInfo(removedImage).absoluteFilePath());
    if (!removedImage.isEmpty() && imagePath.startsWith(imageDirectory + '/'))
        QFile::remove(imagePath);

    if (pendingProfileId_ == profileId) {
        pendingProfileId_.clear();
        pendingProfileSourceWidth_ = 0;
        pendingProfileSourceHeight_ = 0;
        emit pendingProfileChanged();
    }

    if (!deletingActive) {
        settings.sync();
        emit profilesChanged();
        emit statusChanged(QString("Deleted profile '%1'.").arg(removedName));
        log(QString("profile deleted id=%1 active=0").arg(profileId));
        return;
    }

    const QString currentKey = resolutionKey(androidWidth_, androidHeight_);
    auto fallback = std::find_if(profiles_.begin(), profiles_.end(),
        [&currentKey](const MapperProfile &profile) {
            return profile.isDefault && profile.resolutions.contains(currentKey);
        });
    if (fallback == profiles_.end()) {
        fallback = std::find_if(profiles_.begin(), profiles_.end(),
            [&currentKey](const MapperProfile &profile) {
                return profile.resolutions.contains(currentKey);
            });
    }
    if (fallback == profiles_.end()) {
        fallback = std::find_if(profiles_.begin(), profiles_.end(),
            [](const MapperProfile &profile) { return profile.isDefault; });
    }
    if (fallback == profiles_.end())
        fallback = profiles_.begin();

    const bool loaded = fallback != profiles_.end()
        && fallback->resolutions.contains(currentKey)
        && loadProfileVariant(fallback->id, currentKey);
    if (fallback != profiles_.end()) {
        activeProfileId_ = fallback->id;
        activeProfileName_ = fallback->name;
        profileResolutionWidth_ = 0;
        profileResolutionHeight_ = 0;
        if (loaded) {
            profileResolutionWidth_ = androidWidth_;
            profileResolutionHeight_ = androidHeight_;
        } else {
            clearControls();
            emitAllControlsChanged();
            parseResolutionKey(fallback->resolutions.value(0),
                               &profileResolutionWidth_, &profileResolutionHeight_);
        }
        settings.setValue("profiles/activeId", activeProfileId_);
    }
    settings.sync();
    emit profileChanged();
    emit profilesChanged();
    emit statusChanged(loaded
        ? QString("Deleted active profile '%1'; switched to '%2'.")
              .arg(removedName, activeProfileName_)
        : QString("Deleted active profile '%1'; switched to '%2', which has no "
                  "variant for this resolution. Create or select a compatible profile.")
              .arg(removedName, activeProfileName_));
    log(QString("profile deleted id=%1 active=1 fallback=%2 loaded=%3")
            .arg(profileId, activeProfileId_).arg(loaded));
}

void IntegratedView::selectProfile(const QString &profileId)
{
    if (editMode_ || calibrationActive())
        return;
    MapperProfile *profile = findProfile(profileId);
    if (!profile)
        return;
    const QString currentKey = resolutionKey(androidWidth_, androidHeight_);
    if (profile->resolutions.contains(currentKey)) {
        if (!loadProfileVariant(profile->id, currentKey)) {
            emit statusChanged("The selected profile variant could not be loaded.");
            return;
        }
        activateProfileVariant(*profile, currentKey, false);
        emit statusChanged(QString("Active profile: %1 • %2 × %3")
            .arg(profile->name).arg(androidWidth_).arg(androidHeight_));
        log(QString("profile selected id=%1 variant=%2").arg(profile->id, currentKey));
        return;
    }

    pendingProfileId_ = profile->id;
    pendingProfileSourceWidth_ = 0;
    pendingProfileSourceHeight_ = 0;
    const QString sourceKey = closestProfileResolution(*profile, androidWidth_, androidHeight_);
    parseResolutionKey(sourceKey, &pendingProfileSourceWidth_, &pendingProfileSourceHeight_);
    emit pendingProfileChanged();
    emit profileAdaptationRequested();
}

void IntegratedView::renameProfile(const QString &profileId, const QString &name)
{
    MapperProfile *profile = findProfile(profileId);
    const QString trimmed = name.trimmed().left(64);
    if (!profile || profile->isDefault || trimmed.isEmpty())
        return;
    profile->name = trimmed;
    if (activeProfileId_ == profile->id) {
        activeProfileName_ = trimmed;
        emit profileChanged();
    }
    saveProfileMetadata(*profile);
    emit profilesChanged();
}

void IntegratedView::setProfileImage(const QString &profileId, const QUrl &sourceUrl)
{
    MapperProfile *profile = findProfile(profileId);
    if (!profile || !sourceUrl.isLocalFile())
        return;
    const QString source = sourceUrl.toLocalFile();
    QFileInfo sourceInfo(source);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
        return;
    const QString suffix = sourceInfo.suffix().toLower();
    const QStringList allowed = {"png", "jpg", "jpeg", "webp", "bmp", "gif"};
    if (!allowed.contains(suffix))
        return;
    const QString directory = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + "/profile-images";
    if (!QDir().mkpath(directory))
        return;
    // A unique filename also invalidates QML's image cache immediately when
    // the user replaces a PNG with another PNG for the same profile.
    QImage sourceImage(source);
    if (sourceImage.isNull()) {
        emit statusChanged("The selected profile image could not be decoded.");
        return;
    }
    const int cropSide = std::min(sourceImage.width(), sourceImage.height());
    const QRect cropRect((sourceImage.width() - cropSide) / 2,
                         (sourceImage.height() - cropSide) / 2,
                         cropSide, cropSide);
    constexpr int AvatarSize = 512;
    QImage circularAvatar(AvatarSize, AvatarSize, QImage::Format_ARGB32_Premultiplied);
    circularAvatar.fill(Qt::transparent);
    QPainter painter(&circularAvatar);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath circlePath;
    circlePath.addEllipse(QRectF(0.0, 0.0, AvatarSize, AvatarSize));
    painter.setClipPath(circlePath);
    painter.drawImage(circularAvatar.rect(), sourceImage, cropRect);
    painter.end();

    const QString destination = directory + '/' + profile->id + '-'
        + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
    const QString previousImage = profile->imagePath;
    if (!circularAvatar.save(destination, "PNG")) {
        emit statusChanged("Could not save the circular profile image.");
        return;
    }
    profile->imagePath = destination;
    if (!previousImage.isEmpty() && previousImage != destination
        && QFileInfo(previousImage).absolutePath() == QFileInfo(directory).absoluteFilePath())
        QFile::remove(previousImage);
    saveProfileMetadata(*profile);
    emit profilesChanged();
}

void IntegratedView::adaptPendingProfileAutomatically()
{
    MapperProfile *profile = findProfile(pendingProfileId_);
    if (!profile || profile->isDefault)
        return;
    const QString sourceKey = closestProfileResolution(*profile,
                                                        androidWidth_, androidHeight_);
    if (sourceKey.isEmpty() || !loadProfileVariant(profile->id, sourceKey))
        return;
    markAllMobaSkillCalibrationsStale(
        "Profile adapted to another resolution — skill calibration was preserved for review");
    const QString targetKey = resolutionKey(androidWidth_, androidHeight_);
    activateProfileVariant(*profile, targetKey, true);
    saveBindings();
    emit statusChanged(QString("'%1' was proportionally adapted from %2 to %3 × %4. "
                               "Press F5 to fine-tune it.")
        .arg(profile->name, sourceKey).arg(androidWidth_).arg(androidHeight_));
    log(QString("profile auto-adapted id=%1 source=%2 target=%3")
            .arg(profile->id, sourceKey, targetKey));
}

void IntegratedView::createPendingProfileFromScratch()
{
    MapperProfile *profile = findProfile(pendingProfileId_);
    if (!profile || profile->isDefault)
        return;
    clearControls();
    emitAllControlsChanged();
    const QString targetKey = resolutionKey(androidWidth_, androidHeight_);
    activateProfileVariant(*profile, targetKey, true);
    saveBindings();
    emit statusChanged(QString("Blank %1 × %2 variant created for '%3'. Press F5 to build it.")
        .arg(androidWidth_).arg(androidHeight_).arg(profile->name));
    log(QString("blank profile variant created id=%1 target=%2")
            .arg(profile->id, targetKey));
}

void IntegratedView::cancelPendingProfileSwitch()
{
    pendingProfileId_.clear();
    pendingProfileSourceWidth_ = 0;
    pendingProfileSourceHeight_ = 0;
    emit pendingProfileChanged();
}

void IntegratedView::toggleEditMode()
{
    if (!ready_ || !windowVisible_) {
        emit statusChanged("Open Integrated Android before entering mapper edit mode.");
        return;
    }
    if (!editMode_ && !profileResolutionCompatible()) {
        emit statusChanged("This profile has no variant for the current resolution. Press F6 first.");
        return;
    }
    if (editMode_) {
        saveBindings();
        editSnapshot_.clear();
        characterCenterSnapshot_ = {};
        mobaMovementSnapshot_ = {};
        skillCancelSnapshot_ = {};
        mobaSkillsSnapshot_.clear();
        setEditMode(false);
        emit statusChanged("Mapper changes saved.");
        log("mapper draft accepted and saved");
    } else {
        editSnapshot_ = bindings_;
        characterCenterSnapshot_ = characterCenter_;
        mobaMovementSnapshot_ = mobaMovement_;
        skillCancelSnapshot_ = skillCancel_;
        mobaSkillsSnapshot_ = mobaSkills_;
        setEditMode(true);
    }
}

void IntegratedView::setEditMode(bool enabled)
{
    if (enabled) {
        setCursorLocked(false);
        setProfileManagerVisible(false);
    }
    if (calibrationActive())
        cancelMobaSkillCalibration();
    cancelMobaMovementGesture();
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
    keyCaptureTarget_ = KeyCaptureTarget::TapBinding;
    clearBindingOnCancel_ = true;
    setWaitingForKey(true);
    setEditorMessage("Tap created in hold mode — press its keyboard key, or click outside to leave it unbound");
    log(QString("unbound tap created x=%1 y=%2").arg(binding.x).arg(binding.y));
}

void IntegratedView::addCharacterCenterAt(double normalizedX, double normalizedY)
{
    if (!editMode_)
        return;
    const bool movedExisting = characterCenter_.enabled;
    if (movedExisting)
        markAllMobaSkillCalibrationsStale(
            "Character center moved — calibrated skills were marked for review");
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
    const double nextX = std::clamp(normalizedX, 0.0, 1.0);
    const double nextY = std::clamp(normalizedY, 0.0, 1.0);
    if (qFuzzyCompare(characterCenter_.x, nextX)
        && qFuzzyCompare(characterCenter_.y, nextY))
        return;
    markAllMobaSkillCalibrationsStale(
        "Character center moved — calibrated skills were marked for review");
    characterCenter_.x = nextX;
    characterCenter_.y = nextY;
    emit characterCenterChanged();
    emit mobaMovementChanged();
    emit mobaSkillsChanged();
}

void IntegratedView::setCharacterCenterPosition(int pixelX, int pixelY)
{
    moveCharacterCenter(pixelX / static_cast<double>(std::max(1, androidWidth_)),
                        pixelY / static_cast<double>(std::max(1, androidHeight_)));
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

void IntegratedView::setMobaMovementPosition(int pixelX, int pixelY)
{
    if (!editMode_ || !mobaMovement_.enabled)
        return;
    mobaMovement_.x = std::clamp(pixelX / static_cast<double>(std::max(1, androidWidth_)),
                                 0.0, 1.0);
    mobaMovement_.y = std::clamp(pixelY / static_cast<double>(std::max(1, androidHeight_)),
                                 0.0, 1.0);
    emit mobaMovementChanged();
}

void IntegratedView::setMobaMovementHoldThreshold(int milliseconds)
{
    if (!editMode_ || !mobaMovement_.enabled)
        return;
    mobaMovement_.holdThresholdMs = std::clamp(milliseconds, 30, 500);
    emit mobaMovementChanged();
}

void IntegratedView::setMobaMovementDistanceModifier(int percent)
{
    if (!editMode_ || !mobaMovement_.enabled)
        return;
    mobaMovement_.clickDistanceModifier = std::clamp(percent / 100.0, 0.1, 5.0);
    emit mobaMovementChanged();
}

void IntegratedView::addSkillCancelAt(double normalizedX, double normalizedY)
{
    if (!editMode_ || calibrationActive())
        return;
    skillCancel_.enabled = true;
    skillCancel_.x = std::clamp(normalizedX, 0.0, 1.0);
    skillCancel_.y = std::clamp(normalizedY, 0.0, 1.0);
    emit skillCancelChanged();
    setEditorMessage(skillCancel_.key == 0
        ? "MOBA skill cancel placed — right-click it, open Settings and bind a key"
        : "MOBA skill cancel moved");
    log(QString("skill cancel placed x=%1 y=%2 key=%3")
            .arg(skillCancel_.x).arg(skillCancel_.y)
            .arg(keyName(skillCancel_.key)));
}

void IntegratedView::moveSkillCancel(double normalizedX, double normalizedY)
{
    if (!editMode_ || calibrationActive() || !skillCancel_.enabled)
        return;
    skillCancel_.x = std::clamp(normalizedX, 0.0, 1.0);
    skillCancel_.y = std::clamp(normalizedY, 0.0, 1.0);
    emit skillCancelChanged();
}

void IntegratedView::setSkillCancelPosition(int pixelX, int pixelY)
{
    moveSkillCancel(pixelX / static_cast<double>(androidWidth_),
                    pixelY / static_cast<double>(androidHeight_));
}

void IntegratedView::beginRebindSkillCancel()
{
    if (!editMode_ || calibrationActive() || !skillCancel_.enabled)
        return;
    keyCaptureTarget_ = KeyCaptureTarget::SkillCancel;
    setWaitingForKey(true);
    setEditorMessage("Press the MOBA skill cancel key (Esc cancels)");
}

void IntegratedView::removeSkillCancel()
{
    if (!editMode_ || calibrationActive() || !skillCancel_.enabled)
        return;
    skillCancel_ = {};
    if (keyCaptureTarget_ == KeyCaptureTarget::SkillCancel)
        setWaitingForKey(false);
    emit skillCancelChanged();
    setEditorMessage("MOBA skill cancel removed; skill cancellation is unavailable");
    log("skill cancel removed");
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
    const double nextX = std::clamp(normalizedX, 0.0, 1.0);
    const double nextY = std::clamp(normalizedY, 0.0, 1.0);
    if (qFuzzyCompare(skill.x, nextX) && qFuzzyCompare(skill.y, nextY))
        return;
    markMobaSkillCalibrationStale(skill,
        "Calibration was preserved, but the skill centre changed");
    skill.x = nextX;
    skill.y = nextY;
    emit mobaSkillsChanged();
    if (selectedMobaSkillIndex_ == index)
        emit selectedMobaSkillChanged();
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
    markMobaSkillCalibrationStale(skill,
        "Calibration was preserved, but the skill diameter changed");
    skill.radius = nextRadius;
    emit mobaSkillsChanged();
    if (selectedMobaSkillIndex_ == index)
        emit selectedMobaSkillChanged();
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

void IntegratedView::setSelectedMobaSkillSpeed(int level)
{
    if (!editMode_ || calibrationActive() || selectedMobaSkillIndex_ < 0
        || selectedMobaSkillIndex_ >= static_cast<int>(mobaSkills_.size()))
        return;
    MobaSkillControl &skill =
        mobaSkills_[static_cast<std::size_t>(selectedMobaSkillIndex_)];
    const int nextLevel = std::clamp(level, 1, 5);
    if (skill.speedLevel == nextLevel)
        return;
    skill.speedLevel = nextLevel;
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
    setEditorMessage(QString("MOBA skill speed profile set to level %1")
                         .arg(nextLevel));
}

void IntegratedView::setSelectedMobaSkillArtificialCenterEnabled(bool enabled)
{
    if (!editMode_ || calibrationActive() || selectedMobaSkillIndex_ < 0
        || selectedMobaSkillIndex_ >= static_cast<int>(mobaSkills_.size()))
        return;
    MobaSkillControl &skill =
        mobaSkills_[static_cast<std::size_t>(selectedMobaSkillIndex_)];
    if (skill.artificialCenterEnabled == enabled)
        return;
    skill.artificialCenterEnabled = enabled;
    if (enabled && skill.artificialX == 0.82 && skill.artificialY == 0.76) {
        skill.artificialX = skill.x;
        skill.artificialY = skill.y;
    }
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
    setEditorMessage(enabled
        ? "Artificial centre enabled: DOWN there, then MOVE to the real centre"
        : "Artificial centre disabled");
}

void IntegratedView::setSelectedMobaSkillArtificialCenterPosition(int pixelX,
                                                                   int pixelY)
{
    if (selectedMobaSkillIndex_ < 0)
        return;
    moveMobaSkillArtificialCenter(
        selectedMobaSkillIndex_,
        pixelX / static_cast<double>(std::max(1, androidWidth_)),
        pixelY / static_cast<double>(std::max(1, androidHeight_)));
}

void IntegratedView::moveMobaSkillArtificialCenter(int index,
                                                    double normalizedX,
                                                    double normalizedY)
{
    if (!editMode_ || calibrationActive() || index < 0
        || index >= static_cast<int>(mobaSkills_.size()))
        return;
    MobaSkillControl &skill = mobaSkills_[static_cast<std::size_t>(index)];
    const double nextX = std::clamp(normalizedX, 0.0, 1.0);
    const double nextY = std::clamp(normalizedY, 0.0, 1.0);
    if (qFuzzyCompare(skill.artificialX, nextX)
        && qFuzzyCompare(skill.artificialY, nextY))
        return;
    skill.artificialX = nextX;
    skill.artificialY = nextY;
    emit mobaSkillsChanged();
    if (selectedMobaSkillIndex_ == index)
        emit selectedMobaSkillChanged();
    log(QString("MOBA skill artificial centre moved: index=%1 x=%2 y=%3")
            .arg(index).arg(nextX).arg(nextY));
}

void IntegratedView::acceptSelectedMobaSkillCalibration()
{
    if (!editMode_ || selectedMobaSkillIndex_ < 0
        || selectedMobaSkillIndex_ >= static_cast<int>(mobaSkills_.size()))
        return;
    MobaSkillControl &skill =
        mobaSkills_[static_cast<std::size_t>(selectedMobaSkillIndex_)];
    skill.calibrationStale = false;
    skill.recoveryValid = false;
    skill.recoveryCalibrationPoints.clear();
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
    setEditorMessage("Calibration marked as correct and kept unchanged");
}

void IntegratedView::restoreSelectedMobaSkillCalibration()
{
    if (!editMode_ || calibrationActive() || selectedMobaSkillIndex_ < 0
        || selectedMobaSkillIndex_ >= static_cast<int>(mobaSkills_.size()))
        return;
    MobaSkillControl &skill =
        mobaSkills_[static_cast<std::size_t>(selectedMobaSkillIndex_)];
    if (!skill.recoveryValid)
        return;
    skill.x = skill.recoveryX;
    skill.y = skill.recoveryY;
    skill.radius = skill.recoveryRadius;
    skill.artificialCenterEnabled = skill.recoveryArtificialCenterEnabled;
    skill.artificialX = skill.recoveryArtificialX;
    skill.artificialY = skill.recoveryArtificialY;
    characterCenter_.enabled = skill.recoveryCharacterCenterEnabled;
    characterCenter_.x = skill.recoveryCharacterCenterX;
    characterCenter_.y = skill.recoveryCharacterCenterY;
    skill.calibrationPoints = skill.recoveryCalibrationPoints;
    skill.calibrationStale = false;
    skill.recoveryValid = false;
    skill.recoveryCalibrationPoints.clear();
    emit characterCenterChanged();
    emit mobaMovementChanged();
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
    setEditorMessage("Original skill geometry and calibration restored");
}

void IntegratedView::beginRebindSelectedMobaSkill()
{
    if (!editMode_ || calibrationActive() || selectedMobaSkillIndex_ < 0
        || selectedMobaSkillIndex_ >= static_cast<int>(mobaSkills_.size()))
        return;
    keyCaptureTarget_ = KeyCaptureTarget::MobaSkill;
    clearBindingOnCancel_ = false;
    setWaitingForKey(true);
    setEditorMessage("Press the MOBA skill key (Esc cancels)");
}

void IntegratedView::duplicateMobaSkill(int index)
{
    if (!editMode_ || calibrationActive() || index < 0
        || index >= static_cast<int>(mobaSkills_.size()))
        return;
    MobaSkillControl copy = mobaSkills_[static_cast<std::size_t>(index)];
    copy.x = std::clamp(copy.x + 28.0 / std::max(1, androidWidth_), 0.0, 1.0);
    copy.y = std::clamp(copy.y + 28.0 / std::max(1, androidHeight_), 0.0, 1.0);
    copy.key = 0;
    if (!copy.calibrationPoints.empty()) {
        copy.calibrationStale = true;
        copy.recoveryValid = false;
        copy.recoveryCalibrationPoints.clear();
    }
    mobaSkills_.push_back(copy);
    emit mobaSkillsChanged();
    selectMobaSkill(static_cast<int>(mobaSkills_.size()) - 1);
    setEditorMessage("MOBA skill copied; set its bind and review calibration");
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

QPointF IntegratedView::safeCalibrationTouch(const QPointF &point) const
{
    QPointF safe(std::clamp(point.x(), 0.0, 1.0),
                 std::clamp(point.y(), 0.0, 1.0));
    if (!calibrationActive()
        || calibrationSkillIndex_ >= static_cast<int>(mobaSkills_.size()))
        return safe;

    const MobaSkillControl &skill =
        mobaSkills_[static_cast<std::size_t>(calibrationSkillIndex_)];
    const double width = std::max(1, androidWidth_);
    const double height = std::max(1, androidHeight_);
    const double maximumRadius = skill.radius * std::min(width, height);
    const double dx = (safe.x() - skill.x) * width;
    const double dy = (safe.y() - skill.y) * height;
    const double distance = std::hypot(dx, dy);
    if (distance > maximumRadius && distance > 0.0) {
        const double scale = maximumRadius / distance;
        safe = {skill.x + dx * scale / width,
                skill.y + dy * scale / height};
    }

    // Axis clamping can only move the point towards a centre which is already
    // on-screen, so this final clamp preserves the circle-radius invariant too.
    return {std::clamp(safe.x(), 0.0, 1.0),
            std::clamp(safe.y(), 0.0, 1.0)};
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
    calibrationBackupSkill_ = skill;
    hasCalibrationBackupSkill_ = true;
    skill.calibrationPoints.clear();
    calibrationSkillIndex_ = index;
    calibrationStep_ = 0;
    calibrationPointReady_ = false;
    calibrationTouchId_ = -1;
    const int generation = ++calibrationMotionGeneration_;
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
    emit calibrationChanged();
    setEditorMessage("Calibration armed — waiting for the Start click to finish");

    // beginMobaSkillCalibration() is called from the Start button's release
    // handler. Starting an Android touch re-entrantly inside that physical
    // mouse release can make fake_touch cast the skill immediately. Wait until
    // the popups are gone and the complete mouse event has left Qt first.
    QTimer::singleShot(350, this, [this, generation] {
        if (calibrationActive()
            && calibrationMotionGeneration_ == generation)
            startCalibrationTouch();
    });
    log(QString("MOBA skill calibration armed: index=%1 samples=%2")
            .arg(index).arg(CalibrationSampleCount));
}

void IntegratedView::startCalibrationTouch()
{
    if (!calibrationActive() || calibrationTouchId_ >= 0)
        return;
    MobaSkillControl &skill =
        mobaSkills_[static_cast<std::size_t>(calibrationSkillIndex_)];
    const int touchId = allocateTouchId();
    if (touchId < 0) {
        cancelMobaSkillCalibration();
        return;
    }

    const QPointF downPoint = skill.artificialCenterEnabled
        ? QPointF(std::clamp(skill.artificialX, 0.0, 1.0),
                  std::clamp(skill.artificialY, 0.0, 1.0))
        : QPointF(skill.x, skill.y);
    calibrationLastTouch_ = downPoint;
    if (!sendTouchPoint(touchId, calibrationLastTouch_, Qt::TouchPointPressed)) {
        cancelMobaSkillCalibration();
        return;
    }
    calibrationTouchId_ = touchId;
    trackTouch(touchId, calibrationLastTouch_);
    setEditorMessage("Calibration touch is held — it will release only on finish or cancel");
    log(QString("MOBA calibration TOUCH DOWN: index=%1 touch=%2 physical=%3,%4 realCenter=%5,%6")
            .arg(calibrationSkillIndex_).arg(touchId)
            .arg(downPoint.x()).arg(downPoint.y()).arg(skill.x).arg(skill.y));

    const int generation = calibrationMotionGeneration_;
    QTimer::singleShot(160, this, [this, generation] {
        if (calibrationActive() && calibrationTouchId_ >= 0
            && calibrationMotionGeneration_ == generation)
            moveCalibrationTouch();
    });
}

void IntegratedView::animateCalibrationTouch(
    const QPointF &from, const QPointF &to, int durationMs, int generation,
    const std::function<void()> &completed)
{
    constexpr int AnimationFrames = 12;
    for (int frame = 1; frame <= AnimationFrames; ++frame) {
        QTimer::singleShot(durationMs * frame / AnimationFrames, this,
                           [this, from, to, generation, completed, frame] {
            if (!calibrationActive() || calibrationTouchId_ < 0
                || calibrationMotionGeneration_ != generation)
                return;
            const double amount = frame / static_cast<double>(AnimationFrames);
            const QPointF raw = from + (to - from) * amount;
            const QPointF point(std::clamp(raw.x(), 0.0, 1.0),
                                std::clamp(raw.y(), 0.0, 1.0));
            if (!sendTouchPoint(calibrationTouchId_, point, Qt::TouchPointMoved)) {
                cancelMobaSkillCalibration();
                return;
            }
            calibrationLastTouch_ = point;
            updateTrackedTouch(calibrationTouchId_, point);
            if (frame == AnimationFrames)
                completed();
        });
    }
}

void IntegratedView::moveCalibrationTouch()
{
    if (!calibrationActive() || calibrationTouchId_ < 0)
        return;
    const MobaSkillControl &skill =
        mobaSkills_[static_cast<std::size_t>(calibrationSkillIndex_)];
    calibrationPointReady_ = false;
    const int expectedStep = calibrationStep_;
    const int generation = ++calibrationMotionGeneration_;
    const QPointF vector = calibrationVector(calibrationStep_);
    const double radiusPixels = skill.radius * std::min(androidWidth_, androidHeight_);
    const QPointF joystickCenter(skill.x, skill.y);
    const QPointF target = safeCalibrationTouch({
        std::clamp(skill.x + vector.x() * radiusPixels / androidWidth_, 0.0, 1.0),
        std::clamp(skill.y + vector.y() * radiusPixels / androidHeight_, 0.0, 1.0)
    });
    emit calibrationChanged();

    // Keep one uninterrupted finger down for the entire wizard. Between
    // samples, slide it back to the joystick centre, pause, then visibly drag
    // from the centre to the requested vector. Never synthesize a release here.
    animateCalibrationTouch(calibrationLastTouch_, joystickCenter, 100, generation,
                            [this, joystickCenter, target, generation, expectedStep] {
        QTimer::singleShot(90, this,
                           [this, joystickCenter, target, generation, expectedStep] {
            if (!calibrationActive() || calibrationTouchId_ < 0
                || calibrationMotionGeneration_ != generation
                || calibrationStep_ != expectedStep)
                return;
            animateCalibrationTouch(joystickCenter, target, 220, generation,
                                    [this, generation, expectedStep] {
                QTimer::singleShot(220, this, [this, generation, expectedStep] {
                    if (!calibrationActive() || calibrationTouchId_ < 0
                        || calibrationMotionGeneration_ != generation
                        || calibrationStep_ != expectedStep)
                        return;
                    calibrationPointReady_ = true;
                    emit calibrationChanged();
                    log(QString("calibration vector held: %1/%2 touch=%3")
                            .arg(expectedStep + 1).arg(CalibrationSampleCount)
                            .arg(calibrationTouchId_));
                });
            });
        });
    });
    log(QString("calibration drag started: %1/%2 vector=%3,%4 touch=%5")
            .arg(calibrationStep_ + 1).arg(CalibrationSampleCount)
            .arg(vector.x()).arg(vector.y()).arg(calibrationTouchId_));
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
    ++calibrationMotionGeneration_;
    if (calibrationTouchId_ >= 0) {
        sendTouchPoint(calibrationTouchId_, calibrationLastTouch_,
                       Qt::TouchPointReleased);
        forgetTouch(calibrationTouchId_);
        log(QString("MOBA calibration TOUCH UP: completed touch=%1")
                .arg(calibrationTouchId_));
    }
    if (completedIndex >= 0 && completedIndex < static_cast<int>(mobaSkills_.size())) {
        MobaSkillControl &skill = mobaSkills_[static_cast<std::size_t>(completedIndex)];
        skill.calibrationStale = false;
        skill.recoveryValid = false;
        skill.recoveryCalibrationPoints.clear();
    }
    calibrationTouchId_ = -1;
    calibrationSkillIndex_ = -1;
    calibrationStep_ = 0;
    calibrationPointReady_ = false;
    calibrationBackupSkill_ = {};
    hasCalibrationBackupSkill_ = false;
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
    ++calibrationMotionGeneration_;
    if (calibrationTouchId_ >= 0) {
        sendTouchPoint(calibrationTouchId_, calibrationLastTouch_,
                       Qt::TouchPointReleased);
        forgetTouch(calibrationTouchId_);
        log(QString("MOBA calibration TOUCH UP: cancelled touch=%1")
                .arg(calibrationTouchId_));
    }
    if (hasCalibrationBackupSkill_)
        mobaSkills_[static_cast<std::size_t>(cancelledIndex)] = calibrationBackupSkill_;
    calibrationBackupSkill_ = {};
    hasCalibrationBackupSkill_ = false;
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

void IntegratedView::markMobaSkillCalibrationStale(MobaSkillControl &skill,
                                                    const QString &reason)
{
    if (skill.calibrationPoints.empty())
        return;
    if (!skill.recoveryValid) {
        skill.recoveryValid = true;
        skill.recoveryX = skill.x;
        skill.recoveryY = skill.y;
        skill.recoveryRadius = skill.radius;
        skill.recoveryArtificialCenterEnabled = skill.artificialCenterEnabled;
        skill.recoveryArtificialX = skill.artificialX;
        skill.recoveryArtificialY = skill.artificialY;
        skill.recoveryCharacterCenterEnabled = characterCenter_.enabled;
        skill.recoveryCharacterCenterX = characterCenter_.x;
        skill.recoveryCharacterCenterY = characterCenter_.y;
        skill.recoveryCalibrationPoints = skill.calibrationPoints;
    }
    skill.calibrationStale = true;
    setEditorMessage(reason);
}

void IntegratedView::markAllMobaSkillCalibrationsStale(const QString &reason)
{
    bool changed = false;
    for (MobaSkillControl &skill : mobaSkills_) {
        if (!skill.calibrationPoints.empty()) {
            markMobaSkillCalibrationStale(skill, reason);
            changed = true;
        }
    }
    if (!changed)
        return;
    emit mobaSkillsChanged();
    if (selectedMobaSkillIndex_ >= 0)
        emit selectedMobaSkillChanged();
    log("all MOBA skill calibrations marked stale: " + reason);
}

void IntegratedView::removeCharacterCenter()
{
    if (!editMode_ || !characterCenter_.enabled)
        return;
    markAllMobaSkillCalibrationsStale(
        "Character center removed — calibrated skills were marked for review");
    characterCenter_.enabled = false;
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
    clearBindingOnCancel_ = false;
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
    if (skillCancel_.key == key)
        skillCancel_.key = 0;

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
    } else if (keyCaptureTarget_ == KeyCaptureTarget::SkillCancel
               && skillCancel_.enabled) {
        skillCancel_.key = key;
        log(QString("MOBA skill cancel key changed: key=%1").arg(keyName(key)));
    } else {
        return;
    }
    keyCaptureTarget_ = KeyCaptureTarget::None;
    clearBindingOnCancel_ = false;
    setWaitingForKey(false);
    setEditorMessage(QString("Bound to %1 — press Done to accept changes").arg(keyName(key)));
    emit bindingsChanged();
    emit selectedBindingChanged();
    emit mobaSkillsChanged();
    emit selectedMobaSkillChanged();
    emit skillCancelChanged();
}

void IntegratedView::cancelKeyCapture(bool clickedOutside)
{
    if (!waitingForKey_)
        return;
    if (clickedOutside && clearBindingOnCancel_
        && keyCaptureTarget_ == KeyCaptureTarget::TapBinding
        && selectedBindingIndex_ >= 0
        && selectedBindingIndex_ < static_cast<int>(bindings_.size())) {
        bindings_[static_cast<std::size_t>(selectedBindingIndex_)].key = 0;
        emit bindingsChanged();
        emit selectedBindingChanged();
    }
    clearBindingOnCancel_ = false;
    setWaitingForKey(false);
    setEditorMessage(clickedOutside
        ? "Tap left unbound; open Settings or double-click it to bind later"
        : "Binding cancelled");
}

void IntegratedView::duplicateBinding(int index)
{
    if (!editMode_ || index < 0 || index >= static_cast<int>(bindings_.size()))
        return;
    TapBinding copy = bindings_[static_cast<std::size_t>(index)];
    copy.x = std::clamp(copy.x + 24.0 / std::max(1, androidWidth_), 0.0, 1.0);
    copy.y = std::clamp(copy.y + 24.0 / std::max(1, androidHeight_), 0.0, 1.0);
    copy.key = 0;
    bindings_.push_back(copy);
    emit bindingsChanged();
    selectBinding(static_cast<int>(bindings_.size()) - 1);
    setEditorMessage("Tap copied without a bind; double-click it to assign a key");
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
    if (!enabled) {
        keyCaptureTarget_ = KeyCaptureTarget::None;
        clearBindingOnCancel_ = false;
    }
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
        if (isMouseMove && cursorLocked_ && target && watched == target) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            constrainCursorToAndroid(target, mouseEvent->position());
        }
        if (!windowVisible_ || !target || watched != target || editMode_)
            return QObject::eventFilter(watched, event);
        if (profileManagerVisible_)
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
            beginMobaMovementPress(pointer);
            return true;
        }

        if (mobaMovement_.enabled && isMouseMove
            && mouseEvent->buttons().testFlag(Qt::RightButton)) {
            QPointF pointer;
            if (windowToNormalized(target, mouseEvent->position(), &pointer, true))
                updateMobaMovementPress(pointer);
            return true;
        }

        if (mobaMovement_.enabled && isMouseRelease
            && mouseEvent->button() == Qt::RightButton) {
            QPointF pointer = mobaLastPointer_;
            windowToNormalized(target, mouseEvent->position(), &pointer, true);
            finishMobaMovementPress(pointer);
            return true;
        }

        // While a timed/held movement touch exists, the compositor's normal
        // pointer stream must not reach Android: even hover motion can replace
        // the synthetic gesture. Preserve left-click tapping by translating it
        // into a separate native touch ID instead.
        if (mobaMovementActive_) {
            if (isMousePress && mouseEvent->button() == Qt::LeftButton) {
                QPointF pointer;
                if (windowToNormalized(target, mouseEvent->position(), &pointer)) {
                    QTimer::singleShot(0, this, [this, pointer] {
                        if (mobaMovementActive_)
                            triggerQuickTap(pointer.x(), pointer.y());
                    });
                }
            }
            return true;
        }

        // A held MOBA skill owns the physical pointer while it aims. We have
        // already converted its motion into synthetic touch above, so never
        // forward the same mouse event to Waydroid a second time: that native
        // pointer stream can replace/cancel the held fake_touch gesture.
        if (!activeMobaSkillTouchIds_.isEmpty())
            return true;

        // The same isolation is required for an ordinary hold-until-release
        // Tap. Previously this final case was missing: moving or clicking the
        // physical mouse could reach Waydroid's fake_touch path while a mapped
        // keyboard finger was still down and make Android replace that finger.
        // Preserve deliberate left clicks as an independent native touch.
        if (!activeTapPoints_.isEmpty()) {
            if (isMousePress && mouseEvent->button() == Qt::LeftButton) {
                QPointF pointer;
                if (windowToNormalized(target, mouseEvent->position(), &pointer)) {
                    QTimer::singleShot(0, this, [this, pointer] {
                        if (!activeTapPoints_.isEmpty())
                            triggerQuickTap(pointer.x(), pointer.y());
                    });
                }
            }
            return true;
        }
    }

    const bool isPress = event->type() == QEvent::KeyPress;
    const bool isRelease = event->type() == QEvent::KeyRelease;
    if (!isPress && !isRelease)
        return QObject::eventFilter(watched, event);

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    const int key = keyEvent->key();

    if (key == Qt::Key_F12 && windowVisible_) {
        if (isPress && !keyEvent->isAutoRepeat())
            toggleCursorLock();
        return true;
    }

    if (calibrationActive()) {
        if (isPress && key == Qt::Key_Escape && !keyEvent->isAutoRepeat())
            cancelMobaSkillCalibration();
        else if (isPress && key == Qt::Key_F5)
            setEditorMessage("Finish or cancel the active skill calibration first");
        return true;
    }

    if (key == Qt::Key_F6 && windowVisible_) {
        if (isPress && !keyEvent->isAutoRepeat())
            toggleProfileManager();
        return true;
    }

    if (profileManagerVisible_)
        return QObject::eventFilter(watched, event);

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
            cancelKeyCapture(false);
            return true;
        }
        const bool modifier = key == Qt::Key_Shift || key == Qt::Key_Control
                           || key == Qt::Key_Alt || key == Qt::Key_Meta;
        if (key != Qt::Key_unknown && key != Qt::Key_F11
            && key != Qt::Key_F12 && !modifier)
            captureSelectedKey(key);
        return true;
    }

    if (editMode_) {
        // Let Qt Quick controls receive text/numeric input while editing.
        // Mapped taps remain disabled because this branch precedes lookup below.
        return QObject::eventFilter(watched, event);
    }

    if (skillCancel_.enabled && skillCancel_.key != 0
        && key == skillCancel_.key) {
        if (isPress && !keyEvent->isAutoRepeat())
            cancelActiveMobaSkills();
        return true;
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
        } else if (isRelease && !keyEvent->isAutoRepeat()) {
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
        // deliberately never interferes with any other allocated touch.
        if (binding->mode == TapBinding::Quick) {
            if (isPress && !keyEvent->isAutoRepeat())
                triggerQuickTap(binding->x, binding->y);
        } else if (isPress && !keyEvent->isAutoRepeat()) {
            beginHeldTap(key, binding->x, binding->y);
        } else if (isRelease && !keyEvent->isAutoRepeat()) {
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
    const QRectF rendered = androidSurfaceRect(target);
    if (rendered.isEmpty())
        return false;
    double x = (local.x() - rendered.left()) / rendered.width();
    double y = (local.y() - rendered.top()) / rendered.height();
    const bool inside = x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0;
    if (!inside && !clampToSurface)
        return false;
    x = std::clamp(x, 0.0, 1.0);
    y = std::clamp(y, 0.0, 1.0);
    *normalized = {x, y};
    return true;
}

QRectF IntegratedView::androidSurfaceRect(QWindow *target) const
{
    if (!target || androidWidth_ <= 0 || androidHeight_ <= 0)
        return {};
    const double scale = std::min(target->width() / static_cast<double>(androidWidth_),
                                  target->height() / static_cast<double>(androidHeight_));
    if (scale <= 0.0)
        return {};
    const double renderedWidth = androidWidth_ * scale;
    const double renderedHeight = androidHeight_ * scale;
    return {(target->width() - renderedWidth) / 2.0,
            (target->height() - renderedHeight) / 2.0,
            renderedWidth, renderedHeight};
}

void IntegratedView::toggleCursorLock()
{
    if (!windowVisible_ || editMode_ || profileManagerVisible_ || calibrationActive()) {
        emit statusChanged("Cursor lock is available in gameplay mode only.");
        return;
    }
    setCursorLocked(!cursorLocked_);
}

void IntegratedView::setCursorLocked(bool locked)
{
    QWindow *target = integratedWindow();
    if (locked && (!target || !windowVisible_))
        locked = false;
    if (cursorLocked_ == locked)
        return;
    if (target)
        target->setMouseGrabEnabled(locked);
    cursorLocked_ = locked;
    emit cursorLockedChanged();
    emit statusChanged(locked
        ? "Cursor locked to the Android picture. Press F12 to release."
        : "Cursor released.");
    log(QString("cursor lock=%1").arg(locked));
    if (locked && target)
        constrainCursorToAndroid(target, target->mapFromGlobal(QCursor::pos()));
}

void IntegratedView::constrainCursorToAndroid(QWindow *target, const QPointF &local)
{
    if (!cursorLocked_ || cursorWarpInProgress_ || !target)
        return;
    const QRectF rect = androidSurfaceRect(target).adjusted(1.0, 1.0, -1.0, -1.0);
    if (rect.isEmpty() || rect.contains(local))
        return;
    const QPointF clamped(std::clamp(local.x(), rect.left(), rect.right()),
                          std::clamp(local.y(), rect.top(), rect.bottom()));
    cursorWarpInProgress_ = true;
    QCursor::setPos(target->mapToGlobal(clamped.toPoint()));
    cursorWarpInProgress_ = false;
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
    // Android expects the first finger in a gesture to be the primary pointer
    // (id 0). Always take the lowest free id, so a lone calibration, skill or
    // tap starts with 0 and simultaneous mapper actions naturally receive 1+.
    for (int id = 0; id <= MaximumTouchId; ++id) {
        if (!activeTapPoints_.contains(id))
            return id;
    }
    log("touch ignored: all mapper touch ids are active");
    return -1;
}

void IntegratedView::trackTouch(int id, const QPointF &point)
{
    const bool wasEmpty = activeTapPoints_.isEmpty();
    activeTapPoints_.insert(id, point);
    if (wasEmpty)
        emit syntheticTouchActiveChanged();
}

void IntegratedView::updateTrackedTouch(int id, const QPointF &point)
{
    const auto active = activeTapPoints_.find(id);
    if (active != activeTapPoints_.end())
        active.value() = point;
}

void IntegratedView::forgetTouch(int id)
{
    const bool hadTouches = !activeTapPoints_.isEmpty();
    activeTapPoints_.remove(id);
    quickTapGenerations_.remove(id);
    if (hadTouches && activeTapPoints_.isEmpty())
        emit syntheticTouchActiveChanged();
}

void IntegratedView::clearTrackedTouches()
{
    if (activeTapPoints_.isEmpty())
        return;
    activeTapPoints_.clear();
    quickTapGenerations_.clear();
    emit syntheticTouchActiveChanged();
}

void IntegratedView::triggerQuickTap(double normalizedX, double normalizedY)
{
    const int id = allocateTouchId();
    if (id < 0)
        return;
    const QPointF point(normalizedX, normalizedY);
    if (!sendTouchPoint(id, point, Qt::TouchPointPressed))
        return;
    trackTouch(id, point);
    const int generation = ++nextQuickTapGeneration_;
    quickTapGenerations_.insert(id, generation);
    QTimer::singleShot(35, this, [this, id, generation] {
        if (quickTapGenerations_.value(id) != generation)
            return;
        const auto point = activeTapPoints_.constFind(id);
        if (point == activeTapPoints_.cend())
            return;
        sendTouchPoint(id, point.value(), Qt::TouchPointReleased);
        forgetTouch(id);
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
    trackTouch(id, point);
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
    forgetTouch(id);
    log(QString("held tap up: key=%1 touch=%2").arg(keyName(key)).arg(id));
}

void IntegratedView::releaseAllTapTouches()
{
    const QHash<int, QPointF> touches = activeTapPoints_;
    for (auto touch = touches.cbegin(); touch != touches.cend(); ++touch)
        sendTouchPoint(touch.key(), touch.value(), Qt::TouchPointReleased);
    clearTrackedTouches();
    heldTapIdsByKey_.clear();
    activeMobaSkillTouchIds_.clear();
    mobaSkillGestureGenerations_.clear();
    mobaSkillPointers_.clear();
    armingMobaSkills_.clear();
    pendingMobaSkillReleases_.clear();
    ++mobaMovementGestureGeneration_;
    mobaMovementPressPending_ = false;
    mobaMovementHoldActive_ = false;
    mobaMovementAutoActive_ = false;
    mobaMovementActive_ = false;
    mobaMovementTouchId_ = -1;
    if (!touches.isEmpty())
        log(QString("released active mapper tap touches=%1").arg(touches.size()));
}

void IntegratedView::beginMobaMovement(const QPointF &pointer)
{
    if (mobaMovementActive_ || !mobaMovement_.enabled || !characterCenter_.enabled)
        return;
    const int touchId = allocateTouchId();
    if (touchId < 0)
        return;
    mobaMovementActive_ = true;
    mobaMovementTouchId_ = touchId;
    mobaLastPointer_ = pointer;
    mobaLastTouch_ = {mobaMovement_.x, mobaMovement_.y};
    if (!sendTouchPoint(touchId, mobaLastTouch_, Qt::TouchPointPressed)) {
        mobaMovementActive_ = false;
        mobaMovementTouchId_ = -1;
        return;
    }
    trackTouch(touchId, mobaLastTouch_);

    // Movement has no dangerous neighbouring control: establish the joystick
    // centre and move the same finger in the very same input turn. The
    // click/hold classifier only decides what happens on release; it must never
    // delay the character's initial response.
    updateMobaMovement(pointer);
    log(QString("MOBA RMB down: touch=%1 pointer=%2,%3 joystick=%4,%5")
            .arg(touchId).arg(pointer.x()).arg(pointer.y())
            .arg(mobaMovement_.x).arg(mobaMovement_.y));
}

void IntegratedView::updateMobaMovement(const QPointF &pointer)
{
    if (!mobaMovementActive_ || mobaMovementTouchId_ < 0)
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
    if (sendTouchPoint(mobaMovementTouchId_, mobaLastTouch_, Qt::TouchPointMoved))
        updateTrackedTouch(mobaMovementTouchId_, mobaLastTouch_);
}

void IntegratedView::endMobaMovement()
{
    if (!mobaMovementActive_)
        return;
    const int touchId = mobaMovementTouchId_;
    if (touchId >= 0)
        sendTouchPoint(touchId, mobaLastTouch_, Qt::TouchPointReleased);
    forgetTouch(touchId);
    mobaMovementActive_ = false;
    mobaMovementTouchId_ = -1;
    log(QString("MOBA RMB up: id=%1 point=%2,%3")
            .arg(touchId).arg(mobaLastTouch_.x()).arg(mobaLastTouch_.y()));
}

void IntegratedView::beginMobaMovementPress(const QPointF &pointer)
{
    // Invalidate the old click-route timer without releasing its Android
    // finger. A click following another click can therefore redirect the live
    // joystick immediately, with no centre reset or one-frame stop.
    ++mobaMovementGestureGeneration_;
    mobaMovementPressPending_ = true;
    mobaMovementHoldActive_ = false;
    mobaMovementAutoActive_ = false;
    mobaLastPointer_ = pointer;
    const bool reusedTouch = mobaMovementActive_;
    if (mobaMovementActive_)
        updateMobaMovement(pointer);
    else
        beginMobaMovement(pointer);
    if (!mobaMovementActive_) {
        mobaMovementPressPending_ = false;
        return;
    }
    const int generation = mobaMovementGestureGeneration_;
    const int threshold = mobaMovement_.holdThresholdMs;
    log(QString("MOBA RMB moving immediately: generation=%1 threshold=%2ms reused=%3 pointer=%4,%5")
            .arg(generation).arg(threshold).arg(reusedTouch)
            .arg(pointer.x()).arg(pointer.y()));

    QTimer::singleShot(threshold, this, [this, generation] {
        if (generation != mobaMovementGestureGeneration_
            || !mobaMovementPressPending_)
            return;
        mobaMovementPressPending_ = false;
        mobaMovementHoldActive_ = mobaMovementActive_;
        log(QString("MOBA RMB classified as hold: generation=%1 active=%2")
                .arg(generation).arg(mobaMovementActive_));
    });
}

void IntegratedView::updateMobaMovementPress(const QPointF &pointer)
{
    mobaLastPointer_ = pointer;
    if (mobaMovementActive_
        && (mobaMovementPressPending_ || mobaMovementHoldActive_))
        updateMobaMovement(pointer);
}

void IntegratedView::finishMobaMovementPress(const QPointF &pointer)
{
    mobaLastPointer_ = pointer;
    if (mobaMovementHoldActive_) {
        log("MOBA RMB hold released");
        cancelMobaMovementGesture();
        return;
    }
    if (!mobaMovementPressPending_)
        return;

    mobaMovementPressPending_ = false;
    startMobaAutoMovement(pointer);
}

void IntegratedView::startMobaAutoMovement(const QPointF &pointer)
{
    const double dx = (pointer.x() - characterCenter_.x) * androidWidth_;
    const double dy = (pointer.y() - characterCenter_.y) * androidHeight_;
    const double distancePixels = std::hypot(dx, dy);
    const double screenUnit = std::max(1, std::min(androidWidth_, androidHeight_));
    // At 100%, a click one shorter-screen side away holds the joystick for
    // 1600 ms. The profile modifier scales this duration linearly.
    const int durationMs = std::clamp(
        qRound((distancePixels / screenUnit) * 1600.0
               * mobaMovement_.clickDistanceModifier),
        60, 6000);

    const int generation = mobaMovementGestureGeneration_;
    mobaMovementAutoActive_ = true;
    if (mobaMovementActive_)
        updateMobaMovement(pointer);
    else
        beginMobaMovement(pointer);
    if (!mobaMovementActive_) {
        mobaMovementAutoActive_ = false;
        return;
    }
    log(QString("MOBA RMB classified as click: generation=%1 distance=%2px duration=%3ms modifier=%4")
            .arg(generation).arg(qRound(distancePixels)).arg(durationMs)
            .arg(mobaMovement_.clickDistanceModifier));

    QTimer::singleShot(durationMs, this, [this, generation] {
        if (generation != mobaMovementGestureGeneration_
            || !mobaMovementAutoActive_)
            return;
        log(QString("MOBA click route completed: generation=%1").arg(generation));
        cancelMobaMovementGesture();
    });
}

void IntegratedView::cancelMobaMovementGesture()
{
    ++mobaMovementGestureGeneration_;
    mobaMovementPressPending_ = false;
    mobaMovementHoldActive_ = false;
    mobaMovementAutoActive_ = false;
    endMobaMovement();
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
        emit statusChanged("MOBA skill is not calibrated. Press F5, right-click it and calibrate it in Settings.");
        log(QString("MOBA skill ignored: index=%1 calibration missing").arg(index));
        return;
    }
    const int touchId = allocateTouchId();
    if (touchId < 0)
        return;
    const QPointF center(skill.x, skill.y);
    const QPointF downPoint = skill.artificialCenterEnabled
        ? QPointF(std::clamp(skill.artificialX, 0.0, 1.0),
                  std::clamp(skill.artificialY, 0.0, 1.0))
        : center;
    if (!sendTouchPoint(touchId, downPoint, Qt::TouchPointPressed))
        return;
    activeMobaSkillTouchIds_.insert(index, touchId);
    trackTouch(touchId, downPoint);
    const int gestureGeneration = ++nextMobaSkillGestureGeneration_;
    mobaSkillGestureGenerations_.insert(index, gestureGeneration);
    mobaSkillPointers_.insert(index, pointer);
    armingMobaSkills_.insert(index);

    // Every profile preserves two distinct Wayland frames: DOWN at the exact
    // button centre first, then MOVE events outwards. Only their spacing and
    // interpolation count change. Level 5 schedules its single MOVE on the
    // next event-loop turn, which is the minimum safe ordering without delay.
    int centreHoldMs = 12;
    int dragDurationMs = 18;
    int dragFrames = 3;
    switch (std::clamp(skill.speedLevel, 1, 5)) {
    case 1: centreHoldMs = 60; dragDurationMs = 60; dragFrames = 6; break;
    case 2: centreHoldMs = 30; dragDurationMs = 30; dragFrames = 5; break;
    case 3: centreHoldMs = 12; dragDurationMs = 18; dragFrames = 3; break;
    case 4: centreHoldMs = 2;  dragDurationMs = 8;  dragFrames = 2; break;
    case 5: centreHoldMs = 0;  dragDurationMs = 0;  dragFrames = 1; break;
    }
    const int approachFrames = skill.artificialCenterEnabled ? dragFrames : 0;
    const int approachDurationMs = skill.artificialCenterEnabled ? dragDurationMs : 0;
    for (int frame = 1; frame <= approachFrames; ++frame) {
        QTimer::singleShot(centreHoldMs + approachDurationMs * frame / approachFrames,
                           this, [this, index, downPoint, center, frame,
                                  approachFrames, gestureGeneration] {
            const auto active = activeMobaSkillTouchIds_.constFind(index);
            if (active == activeMobaSkillTouchIds_.cend()
                || !armingMobaSkills_.contains(index)
                || mobaSkillGestureGenerations_.value(index) != gestureGeneration)
                return;
            const double amount = frame / static_cast<double>(approachFrames);
            const QPointF touch = downPoint + (center - downPoint) * amount;
            if (sendTouchPoint(active.value(), touch, Qt::TouchPointMoved))
                updateTrackedTouch(active.value(), touch);
        });
    }
    const int aimStartMs = centreHoldMs + approachDurationMs;
    for (int frame = 1; frame <= dragFrames; ++frame) {
        QTimer::singleShot(aimStartMs + dragDurationMs * frame / dragFrames,
                           this, [this, index, center, frame, dragFrames,
                                  gestureGeneration] {
            const auto active = activeMobaSkillTouchIds_.constFind(index);
            if (active == activeMobaSkillTouchIds_.cend()
                || !armingMobaSkills_.contains(index)
                || mobaSkillGestureGenerations_.value(index) != gestureGeneration)
                return;
            const int id = active.value();
            const QPointF target = mobaSkillTouchForPointer(
                index, mobaSkillPointers_.value(index));
            const double amount = frame / static_cast<double>(dragFrames);
            const QPointF touch = center + (target - center) * amount;
            if (sendTouchPoint(id, touch, Qt::TouchPointMoved))
                updateTrackedTouch(id, touch);

            if (frame == dragFrames) {
                armingMobaSkills_.remove(index);
                log(QString("MOBA skill armed: index=%1 touch=%2")
                        .arg(index).arg(id));
                if (pendingMobaSkillReleases_.contains(index))
                    releaseMobaSkillNow(index);
            }
        });
    }
    log(QString("MOBA skill DOWN: index=%1 key=%2 touch=%3 physical=%4,%5 realCenter=%6,%7 speed=%8 totalDelay=%9ms")
            .arg(index).arg(keyName(skill.key)).arg(touchId)
            .arg(downPoint.x()).arg(downPoint.y()).arg(center.x()).arg(center.y())
            .arg(skill.speedLevel).arg(aimStartMs + dragDurationMs));
}

QPointF IntegratedView::mobaSkillTouchForPointer(
    int index, const QPointF &pointer) const
{
    if (index < 0 || index >= static_cast<int>(mobaSkills_.size()))
        return {};
    const MobaSkillControl &skill = mobaSkills_[static_cast<std::size_t>(index)];
    const QPointF vector = mobaSkillVectorForPointer(index, pointer);
    const double radiusPixels = skill.radius * std::min(androidWidth_, androidHeight_);
    return {
        std::clamp(skill.x + vector.x() * radiusPixels / androidWidth_, 0.0, 1.0),
        std::clamp(skill.y + vector.y() * radiusPixels / androidHeight_, 0.0, 1.0)
    };
}

void IntegratedView::updateMobaSkills(const QPointF &pointer)
{
    const QHash<int, int> active = activeMobaSkillTouchIds_;
    for (auto item = active.cbegin(); item != active.cend(); ++item) {
        const int index = item.key();
        const int touchId = item.value();
        if (index < 0 || index >= static_cast<int>(mobaSkills_.size()))
            continue;
        mobaSkillPointers_[index] = pointer;
        if (armingMobaSkills_.contains(index))
            continue;
        const QPointF touch = mobaSkillTouchForPointer(index, pointer);
        if (sendTouchPoint(touchId, touch, Qt::TouchPointMoved))
            updateTrackedTouch(touchId, touch);
    }
}

void IntegratedView::endMobaSkill(int index)
{
    if (armingMobaSkills_.contains(index)) {
        pendingMobaSkillReleases_.insert(index);
        log(QString("MOBA skill key released while arming; UP queued: index=%1")
                .arg(index));
        return;
    }
    releaseMobaSkillNow(index);
}

void IntegratedView::releaseMobaSkillNow(int index, bool cancelled)
{
    const auto active = activeMobaSkillTouchIds_.find(index);
    if (active == activeMobaSkillTouchIds_.end())
        return;
    const int touchId = active.value();
    const QPointF point = activeTapPoints_.value(touchId);
    sendTouchPoint(touchId, point, Qt::TouchPointReleased);
    activeMobaSkillTouchIds_.erase(active);
    forgetTouch(touchId);
    mobaSkillGestureGenerations_.remove(index);
    mobaSkillPointers_.remove(index);
    armingMobaSkills_.remove(index);
    pendingMobaSkillReleases_.remove(index);
    log(QString("MOBA skill %1: index=%2 touch=%3")
            .arg(cancelled ? "cancelled" : "cast").arg(index).arg(touchId));
}

void IntegratedView::cancelActiveMobaSkills()
{
    if (!skillCancel_.enabled || skillCancel_.key == 0) {
        emit statusChanged("MOBA skill cancellation needs a configured cancel control.");
        log("skill cancel ignored: cancel control is not ready");
        return;
    }
    const QList<int> indexes = activeMobaSkillTouchIds_.keys();
    if (indexes.isEmpty()) {
        log("skill cancel pressed with no active MOBA skill");
        return;
    }

    const QPointF cancelPoint(skillCancel_.x, skillCancel_.y);
    for (int index : indexes) {
        const auto active = activeMobaSkillTouchIds_.constFind(index);
        if (active == activeMobaSkillTouchIds_.cend())
            continue;
        const int touchId = active.value();
        armingMobaSkills_.remove(index);
        pendingMobaSkillReleases_.remove(index);
        if (sendTouchPoint(touchId, cancelPoint, Qt::TouchPointMoved))
            updateTrackedTouch(touchId, cancelPoint);
        releaseMobaSkillNow(index, true);
    }
    emit statusChanged("MOBA skill cancelled.");
    log(QString("skill cancel executed at %1,%2 activeSkills=%3")
            .arg(cancelPoint.x()).arg(cancelPoint.y()).arg(indexes.size()));
}

void IntegratedView::releaseAllMobaSkillTouches()
{
    const QList<int> indexes = activeMobaSkillTouchIds_.keys();
    for (int index : indexes)
        releaseMobaSkillNow(index);
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
    setProfileManagerVisible(false);
    if (calibrationActive())
        cancelMobaSkillCalibration();
    cancelMobaMovementGesture();
    setBusy(true);
    setReady(false);
    if (editMode_) {
        bindings_ = editSnapshot_;
        characterCenter_ = characterCenterSnapshot_;
        mobaMovement_ = mobaMovementSnapshot_;
        skillCancel_ = skillCancelSnapshot_;
        mobaSkills_ = mobaSkillsSnapshot_;
        editSnapshot_.clear();
        skillCancelSnapshot_ = {};
        mobaSkillsSnapshot_.clear();
        emit bindingsChanged();
        emit characterCenterChanged();
        emit mobaMovementChanged();
        emit skillCancelChanged();
        emit mobaSkillsChanged();
        log("mapper draft reverted because Waydroid is stopping");
    }
    setEditMode(false);
    setCursorLocked(false);
    // Mapper state is settled before any external Waydroid process is killed.
    // Accepted edits remain persisted; an unfinished draft is reverted exactly
    // as it was before hard shutdown support existed.
    saveBindings();
    setConfigurationUnlocked(false);
    setWindowVisible(false);
    waitingForSurface_ = false;
    emit statusChanged("Stopping Waydroid…");

    stopSession("user-requested stop", [this] {
        setConfigurationUnlocked(true);
        setBusy(false);
        emit statusChanged("Waydroid session and container were fully stopped. "
                           "Resolution is unlocked.");
        log("STATE: hard stop complete; configuration unlocked");
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
    QSettings sessionSettings;
    sessionSettings.setValue("session/lastWidth", width);
    sessionSettings.setValue("session/lastHeight", height);
    sessionSettings.sync();

    const bool resolutionChangedNow = androidWidth_ != width || androidHeight_ != height;
    androidWidth_ = width;
    androidHeight_ = height;
    MapperProfile *activeProfile = findProfile(activeProfileId_);
    const bool defaultNeedsResolution = activeProfile && activeProfile->isDefault
                                     && activeProfile->resolutions.isEmpty();
    if (defaultNeedsResolution) {
        const QString key = resolutionKey(width, height);
        activeProfile->resolutions.append(key);
        profileResolutionWidth_ = width;
        profileResolutionHeight_ = height;
        saveProfileMetadata(*activeProfile);
        saveBindings();
        log(QString("legacy Default pinned to %1x%2")
                .arg(width).arg(height));
    }
    const QString currentVariantKey = resolutionKey(width, height);
    if (resolutionChangedNow && activeProfile
        && activeProfile->resolutions.contains(currentVariantKey)
        && loadProfileVariant(activeProfile->id, currentVariantKey)) {
        profileResolutionWidth_ = width;
        profileResolutionHeight_ = height;
    }
    if (resolutionChangedNow) {
        setProfileManagerVisible(false);
        emit resolutionChanged();
        emitAllControlsChanged();
        emit profileChanged();
        emit profilesChanged();
        log(QString("active profile '%1' now viewed at %2x%3 compatible=%4")
                .arg(activeProfileName_).arg(width).arg(height)
                .arg(profileResolutionCompatible()));
    }
    if (defaultNeedsResolution) {
        emit profileChanged();
        emit profilesChanged();
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
    sessionRecoveryAttempts_ = 0;
    ensureContainerServiceRunning(purpose, [this, purpose, completed] {
        if (busy_)
            launchSessionProcess(purpose, completed);
    });
}

void IntegratedView::launchSessionProcess(const QString &purpose,
                                          const std::function<void()> &completed)
{
    ++sessionStartGeneration_;
    sessionStartPending_ = false;
    sessionStartCompleted_ = {};
    if (sessionProcess_->state() != QProcess::NotRunning) {
        log("old local session launcher still exists; terminating it before start");
        sessionProcess_->terminate();
        if (!sessionProcess_->waitForFinished(1500))
            sessionProcess_->kill();
    }

    const int generation = ++sessionStartGeneration_;
    sessionOutputBuffer_.clear();
    sessionStartPurpose_ = purpose;
    sessionStartCompleted_ = completed;
    sessionStartPending_ = true;
    log(QString("START session (%1), WAYLAND_DISPLAY=%2")
            .arg(purpose, QString::fromLatin1(NestedSocket)));
    sessionProcess_->setProcessEnvironment(nestedEnvironment());
    sessionProcess_->start("waydroid", {"session", "start"});
    if (!sessionProcess_->waitForStarted(3000)) {
        handleSessionStartFailure(
            purpose, "Could not start the Waydroid session process.");
        return;
    }

    emit statusChanged(QString("Waydroid %1 session started; waiting for Android services…")
                           .arg(purpose));
    QTimer::singleShot(SessionReadyTimeoutMs, this, [this, generation, purpose] {
        if (!sessionStartPending_ || generation != sessionStartGeneration_)
            return;
        handleSessionStartFailure(
            purpose,
            QString("Android services did not become ready for the %1 session "
                    "within 45 seconds.").arg(purpose));
    });
}

void IntegratedView::ensureContainerServiceRunning(
    const QString &purpose, const std::function<void()> &completed)
{
    emit statusChanged(QString("Ensuring the Waydroid container is running for %1…")
                           .arg(purpose));
    runHostCommand("systemctl", {"show", "--property=ActiveState", "--value",
                                  WaydroidContainerUnit},
                   [this, purpose, completed](int code, const QString &output) {
        const QString state = output.trimmed().toLower();
        log(QString("container pre-start state: code=%1 state='%2'")
                .arg(code).arg(state));
        if (code == 0 && state == "active") {
            completed();
            return;
        }

        emit statusChanged("Starting the Waydroid container service… "
                           "system authorization may be requested.");
        runHostCommand("systemctl", {"start", "--no-block", WaydroidContainerUnit},
                       [this, purpose, completed](int startCode,
                                                  const QString &startOutput) {
            log(QString("container start requested: code=%1 output='%2'")
                    .arg(startCode).arg(startOutput.trimmed()));
            if (startCode != 0) {
                failOperation("Could not start waydroid-container.service. "
                              "Authorization may have been cancelled; see console log.");
                return;
            }
            waitForContainerServiceRunning(purpose, 0, completed);
        }, 60000);
    }, 3000);
}

void IntegratedView::waitForContainerServiceRunning(
    const QString &purpose, int attempt, const std::function<void()> &completed)
{
    runHostCommand("systemctl", {"show", "--property=ActiveState", "--value",
                                  WaydroidContainerUnit},
                   [this, purpose, attempt, completed](int code,
                                                       const QString &output) {
        const QString state = output.trimmed().toLower();
        if (code == 0 && state == "active") {
            log(QString("container start confirmed (%1), probe=%2")
                    .arg(purpose).arg(attempt + 1));
            completed();
            return;
        }
        if (attempt + 1 >= ServicePollAttempts) {
            failOperation(QString("The Waydroid container did not become active for %1. "
                                  "See console log.").arg(purpose));
            return;
        }
        emit statusChanged(QString("Waiting for the Waydroid container to start… %1/%2")
                               .arg(attempt + 1).arg(ServicePollAttempts));
        QTimer::singleShot(ServicePollIntervalMs, this,
                           [this, purpose, attempt, completed] {
            if (busy_)
                waitForContainerServiceRunning(purpose, attempt + 1, completed);
        });
    }, 3000);
}

void IntegratedView::handleSessionOutput(const QString &channel,
                                         const QString &output)
{
    const QString trimmed = output.trimmed();
    if (!trimmed.isEmpty())
        log(QString("session %1: %2").arg(channel, trimmed));

    sessionOutputBuffer_ += output;
    if (sessionOutputBuffer_.size() > 32768)
        sessionOutputBuffer_ = sessionOutputBuffer_.right(32768);
    if (!sessionStartPending_
        || !sessionOutputBuffer_.contains("Android with user 0 is ready",
                                          Qt::CaseInsensitive))
        return;
    completeSessionStart(sessionStartGeneration_);
}

void IntegratedView::completeSessionStart(int generation)
{
    if (!sessionStartPending_ || generation != sessionStartGeneration_)
        return;
    sessionStartPending_ = false;
    const QString purpose = sessionStartPurpose_;
    const std::function<void()> completed = std::move(sessionStartCompleted_);
    sessionStartCompleted_ = {};
    log(QString("Android readiness confirmed by session output (%1)").arg(purpose));
    emit statusChanged(QString("Android services are ready for the %1 session.")
                           .arg(purpose));
    QTimer::singleShot(SessionReadySettleMs, this,
                       [this, generation, purpose, completed] {
        if (generation != sessionStartGeneration_ || !busy_)
            return;
        log(QString("post-ready settle complete (%1)").arg(purpose));
        completed();
    });
}

void IntegratedView::handleSessionStartFailure(const QString &purpose,
                                               const QString &reason)
{
    if (!sessionStartPending_ || !busy_)
        return;
    sessionStartPending_ = false;
    const std::function<void()> completed = std::move(sessionStartCompleted_);
    sessionStartCompleted_ = {};

    if (sessionRecoveryAttempts_ >= 1) {
        failOperation(reason + " Automatic hard recovery was already attempted. "
                      "See console log.");
        return;
    }

    ++sessionRecoveryAttempts_;
    log(QString("SESSION RECOVERY: purpose=%1 attempt=%2 reason='%3'")
            .arg(purpose).arg(sessionRecoveryAttempts_).arg(reason));
    emit statusChanged(reason + " Hard-resetting Waydroid and retrying once…");
    forceStopWaydroidRuntime("automatic start recovery",
                             [this, purpose, completed] {
        if (!busy_)
            return;
        ensureContainerServiceRunning(purpose, [this, purpose, completed] {
            if (busy_)
                launchSessionProcess(purpose, completed);
        });
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
    ++sessionStartGeneration_;
    sessionStartPending_ = false;
    sessionStartCompleted_ = {};
    log(QString("STOP session (%1)").arg(purpose));
    emit statusChanged("Giving Android a brief chance to stop cleanly…");
    runCommand({"session", "stop"}, [this, purpose, completed]
               (int code, const QString &output) {
        log(QString("stop command returned for %1: code=%2 output='%3'")
                .arg(purpose).arg(code).arg(output.trimmed()));
        // waydroid status talks to the same D-Bus manager that commonly hangs
        // during shutdown. The systemd unit is the final authority instead.
        forceStopWaydroidRuntime(purpose, completed);
    }, QProcessEnvironment::systemEnvironment(), 2500);
}

void IntegratedView::forceStopWaydroidRuntime(
    const QString &purpose, const std::function<void()> &completed)
{
    ++sessionStartGeneration_;
    sessionStartPending_ = false;
    sessionStartCompleted_ = {};
    waitingForSurface_ = false;
    if (sessionProcess_->state() != QProcess::NotRunning) {
        log("hard stop: killing the local waydroid session launcher");
        sessionProcess_->kill();
        sessionProcess_->waitForFinished(1000);
    }

    emit statusChanged("Hard stop: killing Waydroid launchers and its container…");
    killLocalWaydroidLaunchers([this, purpose, completed] {
        emit statusChanged("Hard stop: stopping waydroid-container.service… "
                           "system authorization may be requested.");
        runHostCommand("systemctl", {"stop", "--no-block", WaydroidContainerUnit},
                       [this, purpose, completed](int code, const QString &output) {
            log(QString("container hard-stop requested: code=%1 output='%2'")
                    .arg(code).arg(output.trimmed()));
            // A non-zero result can still mean that the unit is already absent
            // or inactive, so inspect ActiveState before failing.
            waitForContainerServiceStopped(purpose, 0, false, completed);
        }, 60000);
    });
}

void IntegratedView::waitForContainerServiceStopped(
    const QString &purpose, int attempt, bool sigkillIssued,
    const std::function<void()> &completed)
{
    runHostCommand("systemctl", {"show", "--property=ActiveState", "--value",
                                  WaydroidContainerUnit},
                   [this, purpose, attempt, sigkillIssued, completed]
                   (int code, const QString &output) {
        const QString state = output.trimmed().toLower();
        const bool stopped = state == "inactive" || state == "failed"
                          || state == "dead" || state == "unknown"
                          || (code != 0 && state.isEmpty());
        log(QString("container stop probe=%1 code=%2 state='%3' sigkill=%4")
                .arg(attempt + 1).arg(code).arg(state).arg(sigkillIssued));
        if (stopped) {
            log(QString("container hard stop confirmed (%1)").arg(purpose));
            completed();
            return;
        }

        if (attempt + 1 >= ServicePollAttempts && sigkillIssued) {
            failOperation(QString("Could not kill waydroid-container.service for %1. "
                                  "Authorization may have been cancelled; see console log.")
                              .arg(purpose));
            return;
        }

        if (attempt + 1 >= ServicePollAttempts) {
            emit statusChanged("Waydroid ignored stop. Sending SIGKILL to the entire "
                               "container service cgroup…");
            runHostCommand("systemctl", {"kill", "--kill-whom=all",
                                          "--signal=SIGKILL", WaydroidContainerUnit},
                           [this, purpose, completed](int killCode,
                                                      const QString &killOutput) {
                log(QString("container SIGKILL: code=%1 output='%2'")
                        .arg(killCode).arg(killOutput.trimmed()));
                runHostCommand("systemctl", {"stop", "--no-block",
                                              WaydroidContainerUnit},
                               [this, purpose, completed](int stopCode,
                                                          const QString &stopOutput) {
                    log(QString("post-SIGKILL stop: code=%1 output='%2'")
                            .arg(stopCode).arg(stopOutput.trimmed()));
                    waitForContainerServiceStopped(purpose, 0, true, completed);
                }, 60000);
            }, 60000);
            return;
        }

        emit statusChanged(QString("Waiting for the container cgroup to die… %1/%2")
                               .arg(attempt + 1).arg(ServicePollAttempts));
        QTimer::singleShot(ServicePollIntervalMs, this,
                           [this, purpose, attempt, sigkillIssued, completed] {
            if (busy_)
                waitForContainerServiceStopped(purpose, attempt + 1,
                                               sigkillIssued, completed);
        });
    }, 3000);
}

void IntegratedView::killLocalWaydroidLaunchers(
    const std::function<void()> &completed)
{
    const QString pattern = QStringLiteral(
        "(^|/)(python(3)?[[:space:]]+)?([^[:space:]]*/)?waydroid[[:space:]]+"
        "(session[[:space:]]+start|show-full-ui)([[:space:]]|$)");
    runHostCommand("pkill", {"--signal", "KILL", "--uid",
                              QString::number(getuid()), "--full", pattern},
                   [this, completed](int code, const QString &output) {
        // pkill returns 1 when there was simply nothing left to kill.
        log(QString("local Waydroid launcher cleanup: code=%1 output='%2'")
                .arg(code).arg(output.trimmed()));
        completed();
    }, 3000);
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
        ++mobaMovementGestureGeneration_;
        mobaMovementPressPending_ = false;
        mobaMovementHoldActive_ = false;
        mobaMovementAutoActive_ = false;
        mobaMovementActive_ = false;
        mobaMovementTouchId_ = -1;
        clearTrackedTouches();
        heldTapIdsByKey_.clear();
        activeMobaSkillTouchIds_.clear();
        mobaSkillGestureGenerations_.clear();
        mobaSkillPointers_.clear();
        armingMobaSkills_.clear();
        pendingMobaSkillReleases_.clear();
        if (calibrationActive()) {
            ++calibrationMotionGeneration_;
            const int index = calibrationSkillIndex_;
            if (hasCalibrationBackupSkill_ && index >= 0
                && index < static_cast<int>(mobaSkills_.size()))
                mobaSkills_[static_cast<std::size_t>(index)] = calibrationBackupSkill_;
            calibrationBackupSkill_ = {};
            hasCalibrationBackupSkill_ = false;
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
    setProfileManagerVisible(false);
    if (calibrationActive())
        cancelMobaSkillCalibration();
    cancelMobaMovementGesture();
    if (editMode_) {
        bindings_ = editSnapshot_;
        characterCenter_ = characterCenterSnapshot_;
        mobaMovement_ = mobaMovementSnapshot_;
        skillCancel_ = skillCancelSnapshot_;
        mobaSkills_ = mobaSkillsSnapshot_;
        editSnapshot_.clear();
        skillCancelSnapshot_ = {};
        mobaSkillsSnapshot_.clear();
        emit bindingsChanged();
        emit characterCenterChanged();
        emit mobaMovementChanged();
        emit skillCancelChanged();
        emit mobaSkillsChanged();
        log("mapper draft reverted because integrated window was hidden");
    }
    setEditMode(false);
    setWindowVisible(false);
}

void IntegratedView::runHostCommand(
    const QString &program, const QStringList &arguments,
    const std::function<void(int, const QString &)> &completed, int timeoutMs)
{
    auto *command = new QProcess(this);
    const QString printable = program + ' ' + arguments.join(' ');
    const auto finished = std::make_shared<bool>(false);
    log("HOST COMMAND start: " + printable);
    connect(command, &QProcess::errorOccurred, this,
            [this, command, printable, completed, finished]
            (QProcess::ProcessError error) {
        log(QString("HOST COMMAND error: %1 error=%2 message='%3'")
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
        log(QString("HOST COMMAND finish: %1 code=%2 status=%3 output='%4'")
                .arg(printable).arg(exitCode).arg(static_cast<int>(status))
                .arg(output.trimmed()));
        command->deleteLater();
        completed(exitCode, output);
    });
    command->start(program, arguments);
    QTimer::singleShot(timeoutMs, this,
                       [this, command, printable, finished, timeoutMs] {
        if (*finished)
            return;
        log(QString("HOST COMMAND timeout after %1ms, killing: %2")
                .arg(timeoutMs).arg(printable));
        command->kill();
    });
}

void IntegratedView::runCommand(const QStringList &arguments,
                                const std::function<void(int, const QString &)> &completed,
                                const QProcessEnvironment &environment,
                                int timeoutMs)
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
    QTimer::singleShot(timeoutMs, this, [this, command, printable, finished, timeoutMs] {
        if (*finished)
            return;
        log(QString("COMMAND timeout after %1ms, killing: %2")
                .arg(timeoutMs).arg(printable));
        command->kill();
    });
}

void IntegratedView::failOperation(const QString &status)
{
    log("FAIL: " + status);
    setProfileManagerVisible(false);
    if (calibrationActive())
        cancelMobaSkillCalibration();
    cancelMobaMovementGesture();
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
    if (!visible)
        setCursorLocked(false);
    if (windowVisible_ == visible)
        return;
    windowVisible_ = visible;
    emit windowVisibleChanged();
}
