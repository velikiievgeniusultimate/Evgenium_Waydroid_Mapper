#include "IntegratedView.h"
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace {
constexpr double Tau = 6.2831853071795864769;
constexpr double RangeRadii[] = {1.0, .5, .25};
QString globalFile()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    return directory + "/global-calibration.ini";
}
double angleOf(const QPointF &p)
{
    double a = std::atan2(p.y(), p.x());
    return a < 0 ? a + Tau : a;
}
QJsonArray jsonPoints(const std::vector<QPointF> &points)
{
    QJsonArray result;
    for (const auto &p : points) result.append(QJsonArray{p.x(), p.y()});
    return result;
}
// Invert the measured screen-angle -> joystick-angle correspondence.
QPointF mappedDirection(const std::vector<QPointF> &directions, const QPointF &target)
{
    struct Node { double screen, joystick; };
    std::vector<Node> nodes;
    for (size_t i = 0; i < directions.size(); ++i)
        nodes.push_back({angleOf(directions[i]), i * Tau / directions.size()});
    if (nodes.empty()) return {};
    std::sort(nodes.begin(), nodes.end(), [](auto a, auto b) { return a.screen < b.screen; });
    const double angle = angleOf(target);
    for (size_t i = 0; i < nodes.size(); ++i) {
        auto left = nodes[i], right = nodes[(i+1) % nodes.size()];
        if (i+1 == nodes.size()) right.screen += Tau;
        double query = angle < left.screen ? angle + Tau : angle;
        if (query < left.screen || query > right.screen) continue;
        double delta = std::remainder(right.joystick - left.joystick, Tau);
        const double fraction = (query - left.screen) / std::max(1e-9, right.screen - left.screen);
        double a = left.joystick + delta * fraction;
        return {std::cos(a), std::sin(a)};
    }
    return {};
}
}

bool IntegratedView::movementAngleReady() const
{
    return !mobaMovement_.experimental || (globalDirections_.size() == DirectionalCalibrationSampleCount
        && globalResolutionKey_ == resolutionKey(androidWidth_, androidHeight_));
}

QPointF IntegratedView::movementDirection(const QPointF &screenVector) const
{
    if (!movementAngleReady()) return {};
    const double length = std::hypot(screenVector.x(), screenVector.y());
    if (length < .001) return {};
    return mobaMovement_.experimental ? mappedDirection(globalDirections_, screenVector) : screenVector / length;
}

QVariantMap IntegratedView::globalCalibration() const
{
    return {{"resolution", resolutionKey(androidWidth_, androidHeight_)},
        {"angleReady", globalDirections_.size() == DirectionalCalibrationSampleCount},
        {"angleCount", int(globalDirections_.size())},
        {"centerReady", characterCenter_.enabled},
        {"measurementsPath", QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + "/calibration-measurements.jsonl"}};
}

void IntegratedView::loadGlobalCalibration(bool migrateCenter)
{
    const auto legacyCenter = characterCenter_;
    globalResolutionKey_ = resolutionKey(androidWidth_, androidHeight_);
    QSettings store(globalFile(), QSettings::IniFormat);
    store.beginGroup(globalResolutionKey_);
    globalDirections_.clear();
    if (store.value("initialized", false).toBool()) {
        characterCenter_.enabled = store.value("centerEnabled", false).toBool();
        characterCenter_.x = std::clamp(store.value("centerX", .5).toDouble(), 0.0, 1.0);
        characterCenter_.y = std::clamp(store.value("centerY", .5).toDouble(), 0.0, 1.0);
        const int count = store.beginReadArray("directions");
        bool valid = count == DirectionalCalibrationSampleCount;
        for (int i = 0; i < count; ++i) {
            store.setArrayIndex(i);
            QPointF p(store.value("x").toDouble(), store.value("y").toDouble());
            const double length = std::hypot(p.x(), p.y());
            if (!std::isfinite(length) || length < .9 || length > 1.1) valid = false;
            globalDirections_.push_back(p);
        }
        store.endArray();
        if (!valid) globalDirections_.clear();
    } else {
        characterCenter_ = {};
        if (migrateCenter && legacyCenter.enabled) {
            characterCenter_ = legacyCenter;
            saveGlobalCalibration();
            log("existing centre migrated into global resolution calibration: " + globalResolutionKey_);
        }
    }
    if (legacyCenter.enabled && characterCenter_.enabled
        && std::hypot((legacyCenter.x-characterCenter_.x)*androidWidth_,
                      (legacyCenter.y-characterCenter_.y)*androidHeight_) > .5) {
        for (auto &skill : mobaSkills_) {
            if (!skill.experimental && !skill.calibrationPoints.empty())
                markMobaSkillCalibrationStale(skill, "Общий центр отличается от центра профиля — проверьте исходную калибровку.");
        }
    }
    emit calibrationChanged();
}

void IntegratedView::saveGlobalCalibration() const
{
    // Never save an old-resolution angle map under a newly selected size.
    if (globalResolutionKey_ != resolutionKey(androidWidth_, androidHeight_)) return;
    QSettings store(globalFile(), QSettings::IniFormat);
    store.beginGroup(globalResolutionKey_);
    store.setValue("initialized", true);
    store.setValue("centerEnabled", characterCenter_.enabled);
    store.setValue("centerX", characterCenter_.x);
    store.setValue("centerY", characterCenter_.y);
    store.remove("directions");
    store.beginWriteArray("directions", int(globalDirections_.size()));
    for (int i = 0; i < int(globalDirections_.size()); ++i) {
        store.setArrayIndex(i);
        store.setValue("x", globalDirections_[i].x());
        store.setValue("y", globalDirections_[i].y());
    }
    store.endArray();
    store.endGroup();
    store.sync();
    if (store.status() != QSettings::NoError) qWarning() << "Cannot save global calibration:" << store.fileName();
}

void IntegratedView::setSelectedSkillExperimental(bool enabled)
{
    if (!editMode_ || calibrationActive() || selectedMobaSkillIndex_ < 0 || selectedMobaSkillIndex_ >= int(mobaSkills_.size())) return;
    auto &skill = mobaSkills_[selectedMobaSkillIndex_];
    if (skill.experimental == enabled) return;
    recordMapperUndo();
    skill.experimental = enabled;
    emit mobaSkillsChanged(); emit selectedMobaSkillChanged();
}

void IntegratedView::setSelectedSkillDirectional(bool enabled)
{
    if (!editMode_ || calibrationActive() || selectedMobaSkillIndex_ < 0 || selectedMobaSkillIndex_ >= int(mobaSkills_.size())) return;
    auto &skill = mobaSkills_[selectedMobaSkillIndex_];
    if (skill.directionalOnly == enabled) return;
    recordMapperUndo();
    skill.directionalOnly = enabled;
    emit mobaSkillsChanged(); emit selectedMobaSkillChanged();
}

void IntegratedView::setSelectedSkillRangeSamples(int count)
{
    if (count != 12 && count != 24 && count != 48) return;
    if (!editMode_ || calibrationActive() || selectedMobaSkillIndex_ < 0
        || selectedMobaSkillIndex_ >= int(mobaSkills_.size())) return;
    auto &skill = mobaSkills_[selectedMobaSkillIndex_];
    if (skill.rangeSampleCount == count) return;
    recordMapperUndo(); skill.rangeSampleCount = count;
    emit mobaSkillsChanged(); emit selectedMobaSkillChanged();
}

void IntegratedView::setSelectedSkillEdgeZone(int percent)
{
    if (!editMode_ || calibrationActive() || selectedMobaSkillIndex_ < 0
        || selectedMobaSkillIndex_ >= int(mobaSkills_.size())) return;
    auto &skill = mobaSkills_[selectedMobaSkillIndex_];
    percent = std::clamp(percent, 1, 30);
    if (skill.edgeZonePercent == percent) return;
    recordMapperUndo(); skill.edgeZonePercent = percent;
    emit mobaSkillsChanged(); emit selectedMobaSkillChanged();
}

void IntegratedView::skipOffscreenRangePoint()
{
    if (!rangeCalibrationActive() || !calibrationPointReady_) return;
    if (calibrationStep_ >= 2*rangeCalibrationDirections_) {
        auto &fraction = rangeCalibrationMinimumFractions_[calibrationStep_ % rangeCalibrationDirections_];
        if (fraction <= 1./64) {
            setEditorMessage("Даже при минимальном отклонении конец не виден. Отмените калибровку: прежние данные сохранятся.");
            return;
        }
        fraction *= .5;
        calibrationPointReady_ = false;
        emit calibrationChanged();
        moveCalibrationTouch();
        return;
    }
    rangeCalibrationMissingMask_ |= quint64(1) << calibrationStep_;
    recordMobaSkillCalibrationPoint(characterCenter_.x, characterCenter_.y);
}

bool IntegratedView::experimentalRangeReady(const MobaSkillControl &skill) const
{
    const int directions = int(skill.rangePoints.size()) / 3;
    if ((directions != 4 && directions != 8 && directions != 16)
        || skill.rangeMinimumFractions.size() != size_t(directions)) return false;
    if (skill.rangeWidth != androidWidth_ || skill.rangeHeight != androidHeight_
        || skill.rangePoints.size() != size_t(directions*3) || skill.rangeRadius <= 0
        || std::abs(skill.radius-skill.rangeRadius)*std::min(androidWidth_,androidHeight_) > 2.0) return false;
    for (int direction = 0; direction < directions; ++direction) {
        if (!std::isfinite(skill.rangeMinimumFractions[direction])
            || skill.rangeMinimumFractions[direction] < 1./64
            || skill.rangeMinimumFractions[direction] > .25
            || (skill.rangeMissingMask & (quint64(1) << (2*directions+direction)))) return false;
        double previous = 0;
        for (int ring = 2; ring >= 0; --ring) {
            auto p = skill.rangePoints[ring*directions + direction];
            double distance = std::hypot(p.x()*androidWidth_, p.y()*androidHeight_);
            if (!std::isfinite(distance) || distance <= previous + .5) return false;
            previous = distance;
        }
    }
    return true;
}

bool IntegratedView::isSkillCalibrated(const MobaSkillControl &skill) const
{
    if (!skill.experimental) return isOriginalSkillCalibrated(skill);
    return globalDirections_.size() == DirectionalCalibrationSampleCount
        && (skill.directionalOnly || experimentalRangeReady(skill));
}

QPointF IntegratedView::experimentalCalibrationVector(int step) const
{
    const int n = rangeCalibrationDirections_;
    const double angle = (step % n) * Tau / n;
    const double radius = step >= 2*n ? rangeCalibrationMinimumFractions_[step % n]
        : RangeRadii[std::clamp(step / n, 0, 2)];
    return {std::cos(angle)*radius, std::sin(angle)*radius};
}

void IntegratedView::beginExperimentalCalibration(int index, bool angle)
{
    if (!editMode_ || calibrationActive() || autoGeometryActive() || waitingForKey_
        || !windowVisible_ || !inputSurface_ || !characterCenter_.enabled
        || index < 0 || index >= int(mobaSkills_.size())) return;
    if (!angle && globalDirections_.size() != DirectionalCalibrationSampleCount) {
        setEditorMessage("Сначала выполните общую калибровку угла для этого разрешения."); return;
    }
    rangeCalibrationMissingMask_ = 0;
    rangeCalibrationDirections_ = mobaSkills_[index].rangeSampleCount / 3;
    rangeCalibrationMinimumFractions_.assign(rangeCalibrationDirections_, .25);
    experimentalCalibrationKind_ = angle ? 1 : 2;
    beginMobaSkillCalibration(index, angle ? DirectionalCalibrationVersion : 4);
    if (!calibrationActive()) experimentalCalibrationKind_ = 0;
}

bool IntegratedView::commitExperimentalCalibration(int index)
{
    auto &skill = mobaSkills_[index];
    const auto measured = skill.calibrationPoints;
    const QPointF center(characterCenter_.x, characterCenter_.y);
    std::vector<QPointF> offsets;
    for (auto point : measured) offsets.push_back(point - center);
    std::vector<QPointF> directions;
    const bool angle = experimentalCalibrationKind_ == 1;
    if (angle) {
        if (measured.size() != DirectionalCalibrationSampleCount) return false;
        for (auto p : offsets) {
            QPointF pixels(p.x()*androidWidth_, p.y()*androidHeight_);
            const double distance = std::hypot(pixels.x(), pixels.y());
            if (!std::isfinite(distance) || distance < 5) return false;
            directions.push_back(pixels / distance);
        }
        double sum = 0;
        for (size_t i = 0; i < directions.size(); ++i) {
            const double delta = std::remainder(angleOf(directions[(i+1)%directions.size()])
                - angleOf(directions[i]), Tau);
            if (delta <= .0001 || delta > Tau/6) return false;
            sum += delta;
        }
        if (std::abs(sum - Tau) > .01) return false;
    } else {
        // Reconstruct only the unavailable outer samples. Keep a mask so these
        // estimates never masquerade as observations in the journal or mapping.
        const int n = rangeCalibrationDirections_;
        if (measured.size() != size_t(n*3)) return false;
        for (int d = 0; d < n; ++d) {
            int visible = -1;
            for (int ring = 0; ring < 3; ++ring) {
                int i = ring * n + d;
                if (!(rangeCalibrationMissingMask_ & (quint64(1) << i))) { visible = i; break; }
            }
            if (visible < 0) return false;
            // A farther sample cannot be visible if a nearer one was skipped.
            for (int ring = visible / n; ring < 3; ++ring)
                if (rangeCalibrationMissingMask_ & (quint64(1) << (ring*n+d))) return false;
            for (int ring = 0; ring < visible / n; ++ring)
                offsets[ring*n+d] = offsets[visible] * (RangeRadii[ring]
                    / (visible >= 2*n ? rangeCalibrationMinimumFractions_[d] : RangeRadii[visible/n]));
        }
        auto probe = calibrationBackupSkill_;
        probe.rangePoints = offsets;
        probe.rangeMinimumFractions = rangeCalibrationMinimumFractions_;
        probe.rangeMissingMask = rangeCalibrationMissingMask_;
        probe.rangeWidth = androidWidth_; probe.rangeHeight = androidHeight_;
        probe.rangeRadius = skill.radius;
        if (!experimentalRangeReady(probe)) return false;
    }
    // Record the accepted result against the untouched original mode, not the
    // temporary wizard buffer. One undo restores both the skill and global angle.
    skill = calibrationBackupSkill_;
    recordMapperUndo();
    if (angle) globalDirections_ = std::move(directions);
    else {
        skill.rangeMinimumFractions = rangeCalibrationMinimumFractions_;
        skill.rangeMissingMask = rangeCalibrationMissingMask_;
        skill.rangePoints = std::move(offsets);
        skill.rangeWidth = androidWidth_; skill.rangeHeight = androidHeight_;
        skill.rangeRadius = skill.radius;
    }
    appendCalibrationMeasurement(angle ? "angle" : "range", index, measured);
    calibrationSummary_ = angle
        ? QString("Общий угол сохранён для %1. Доступен всем профилям этого разрешения. 64 точки.").arg(globalResolutionKey_)
        : QStringLiteral("Дальность сохранена. У обрезанных краёв включён плавный разгон. Измерения и пропуски записаны в журнал.");
    return true;
}

QPointF IntegratedView::experimentalSkillVector(int index, const QPointF &pointer) const
{
    const auto &skill = mobaSkills_[index];
    if (!isSkillCalibrated(skill)) return {};
    const QPointF target((pointer.x()-characterCenter_.x)*androidWidth_,
                         (pointer.y()-characterCenter_.y)*androidHeight_);
    const double distance = std::hypot(target.x(), target.y());
    if (distance <= DirectionalCenterDeadzonePixels)
        return skill.directionalOnly ? mobaSkillLastDirectionalVectors_.value(index, QPointF(1,0)) : QPointF();
    const QPointF direction = mappedDirection(globalDirections_, target);
    mobaSkillLastDirectionalVectors_.insert(index, direction);
    if (skill.directionalOnly) return direction;
    const int n = int(skill.rangePoints.size()) / 3;
    const double sector = angleOf(direction) * n / Tau;
    const int left = int(std::floor(sector)) % n, right = (left+1)%n;
    const double fraction = sector - std::floor(sector);
    auto length = [&](int ring, int d) { auto p=skill.rangePoints[ring*n+d];
        return std::hypot(p.x()*androidWidth_, p.y()*androidHeight_); };
    auto contour = [&](int ring) {
        return length(ring,left)*(1-fraction) + length(ring,right)*fraction;
    };
    auto inverse = [&](double query) {
        double previousDistance = 0, previousRadius = 0;
        for (int ring = 2; ring >= 0; --ring) {
            const double measured = contour(ring);
            const double measuredRadius = ring == 2
                ? skill.rangeMinimumFractions[left]*(1-fraction)+skill.rangeMinimumFractions[right]*fraction
                : RangeRadii[ring];
            if (query <= measured)
                return std::clamp(previousRadius + (measuredRadius-previousRadius)
                    * (query-previousDistance)/std::max(.5, measured-previousDistance), 0.0, 1.0);
            previousDistance=measured; previousRadius=measuredRadius;
        }
        return 1.0;
    };
    double radius = inverse(distance);
    if (skill.rangeMissingMask) {
        const QPointF ray = target / distance;
        const double cx = characterCenter_.x * androidWidth_, cy = characterCenter_.y * androidHeight_;
        double boundary = 1e30;
        if (std::abs(ray.x()) > 1e-9)
            boundary = std::min(boundary, (ray.x()>0 ? androidWidth_-cx : -cx)/ray.x());
        if (std::abs(ray.y()) > 1e-9)
            boundary = std::min(boundary, (ray.y()>0 ? androidHeight_-cy : -cy)/ray.y());
        auto lastVisibleRing = [&](int d) {
            int ring=0;
            while (ring<2 && (skill.rangeMissingMask & (quint64(1) << (ring*n+d)))) ++ring;
            return ring;
        };
        const int l=lastVisibleRing(left), r=lastVisibleRing(right);
        const double coverage = (l ? 1-fraction : 0) + (r ? fraction : 0);
        // Visible full-range sectors retain their original response. Interpolate
        // the clipping influence continuously across neighbouring directions.
        if (coverage > 0 && boundary > 0 && contour(0) > boundary) {
            const double visible = length(l,left)*(1-fraction)+length(r,right)*fraction;
            const double zone = skill.edgeZonePercent*.01*std::min(androidWidth_,androidHeight_);
            const double baseStart = std::max(0.0, std::min(boundary-zone, visible));
            const double start = boundary - (boundary-baseStart)*coverage;
            const double t = std::clamp((distance-start)/std::max(1.0,boundary-start),0.0,1.0);
            const double smooth = t*t*(3-2*t);
            radius += (1-radius)*smooth;
        }
    }
    return direction * radius;
}

void IntegratedView::appendCalibrationMeasurement(const QString &kind, int index,
                                                   const std::vector<QPointF> &points) const
{
    const auto &skill = mobaSkills_[index];
    QJsonArray inputs;
    for (int i=0; i<int(points.size()); ++i) {
        const QPointF v = kind == "angle" ? directionalCalibrationVector(i) : experimentalCalibrationVector(i);
        inputs.append(QJsonArray{v.x(),v.y()});
    }
    QJsonArray rawPoints, minimumFractions;
    for (int i=0; i<int(points.size()); ++i)
        rawPoints.append(kind == "range" && (skill.rangeMissingMask & (quint64(1) << i))
            ? QJsonValue(QJsonValue::Null) : QJsonValue(QJsonArray{points[i].x(), points[i].y()}));
    for (double fraction : skill.rangeMinimumFractions) minimumFractions.append(fraction);
    const QJsonObject record{{"schema",2},{"type",kind},
        {"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"resolution",globalResolutionKey_},{"width",androidWidth_},{"height",androidHeight_},
        {"profileId",activeProfileId_},{"profileName",activeProfileName_},
        {"skillIndex",index},{"key",keyName(skill.key)},
        {"center",QJsonArray{characterCenter_.x,characterCenter_.y}},
        {"joystickCenter",QJsonArray{skill.x,skill.y}},{"joystickRadius",skill.radius},
        {"artificialCenterEnabled",skill.artificialCenterEnabled},
        {"artificialCenter",QJsonArray{skill.artificialX,skill.artificialY}},
        {"screenPoints",rawPoints},{"inputVectors",inputs},
        {"missingPointMask",qint64(kind == "range" ? skill.rangeMissingMask : 0)},
        {"edgeZonePercent",skill.edgeZonePercent},{"rangeMinimumFractions",minimumFractions},{"rangeDirections",int(skill.rangePoints.size())/3},
        {"globalDirections",jsonPoints(globalDirections_)},
        {"directionalOnly",skill.directionalOnly},{"rangeRadii",QJsonArray{1.0,.5,.25}},
        {"screenPointUnits","normalized Android coordinates"},
        {"globalDirectionUnits","unit vectors in Android pixel space"}};
    const auto directory=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(directory);
    QFile journal(directory+"/calibration-measurements.jsonl");
    if (!journal.open(QIODevice::WriteOnly|QIODevice::Append)
        || journal.write(QJsonDocument(record).toJson(QJsonDocument::Compact)+'\n') < 0 || !journal.flush())
        qWarning() << "Cannot record calibration measurements:" << journal.fileName() << journal.errorString();
}
