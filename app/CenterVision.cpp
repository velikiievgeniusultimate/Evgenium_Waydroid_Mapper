#include "CenterVision.h"

#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPen>
#include <QProcess>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QWaylandSurface>
#include <QWaylandSurfaceGrabber>
#include <QtConcurrent>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
constexpr int WorkingWidth = 960;
constexpr int MaximumDiagnosticFrames = 240;
constexpr int TrackingIntervalMs = 70;

QString safeComponent(QString value)
{
    for (QChar &character : value) {
        if (!character.isLetterOrNumber() && character != '-' && character != '_')
            character = '_';
    }
    return value.isEmpty() ? QStringLiteral("default") : value;
}

int luminance(QRgb pixel)
{
    return (qRed(pixel) * 77 + qGreen(pixel) * 150 + qBlue(pixel) * 29) >> 8;
}

QString rectText(const QRectF &rect)
{
    return QStringLiteral("%1,%2,%3,%4")
        .arg(rect.x(), 0, 'f', 6).arg(rect.y(), 0, 'f', 6)
        .arg(rect.width(), 0, 'f', 6).arg(rect.height(), 0, 'f', 6);
}

QString pointText(const QPointF &point)
{
    return QStringLiteral("%1,%2")
        .arg(point.x(), 0, 'f', 6).arg(point.y(), 0, 'f', 6);
}
}

CenterVision::CenterVision(QObject *parent)
    : QObject(parent)
{
    connect(&matchWatcher_, &QFutureWatcher<MatchResult>::finished,
            this, &CenterVision::handleMatchFinished);
}

CenterVision::~CenterVision()
{
    tracking_ = false;
    if (matchWatcher_.isRunning())
        matchWatcher_.waitForFinished();
    if (sessionLog_.isOpen())
        sessionLog_.close();
}

bool CenterVision::hasReference() const
{
    return !referenceFrame_.isNull() && !templateImage_.isNull()
        && anchorConfigured_;
}

bool CenterVision::frameFrozen() const
{
    return visible_ && (stage_ == SelectTemplate || stage_ == SelectAnchor
                        || stage_ == CorrectAnchor);
}

QString CenterVision::stageName() const
{
    switch (stage_) {
    case Idle: return QStringLiteral("IDLE");
    case SelectTemplate: return QStringLiteral("ОБВЕДИ HP");
    case SelectAnchor: return QStringLiteral("УКАЖИ ЦЕНТР");
    case Ready: return QStringLiteral("ГОТОВО");
    case Tracking: return QStringLiteral("СЛЕЖЕНИЕ");
    case CorrectAnchor: return QStringLiteral("ИСПРАВЬ ЦЕНТР");
    }
    return QStringLiteral("UNKNOWN");
}

void CenterVision::setSurface(QWaylandSurface *surface)
{
    if (surface_ == surface)
        return;
    stopTracking();
    ++contextGeneration_;
    ++grabRequestId_;
    grabInFlight_ = false;
    nativeFallbackStarted_ = false;
    surface_ = surface;
    if (visible_ || !sessionDirectory_.isEmpty()) {
        logEvent(QStringLiteral("SURFACE"), surface
            ? QStringLiteral("attached") : QStringLiteral("detached"));
    }
}

void CenterVision::setContext(const QString &profileId, int width, int height)
{
    const QString normalizedProfile = safeComponent(profileId);
    width = std::max(0, width);
    height = std::max(0, height);
    if (profileId_ == normalizedProfile && contextWidth_ == width
        && contextHeight_ == height)
        return;

    stopTracking();
    ++contextGeneration_;
    ++grabRequestId_;
    grabInFlight_ = false;
    nativeFallbackStarted_ = false;
    if (sessionLog_.isOpen())
        sessionLog_.close();
    sessionDirectory_.clear();
    profileId_ = normalizedProfile;
    contextWidth_ = width;
    contextHeight_ = height;
    referenceFrame_ = {};
    templateImage_ = {};
    templateRect_ = {};
    matchRect_ = {};
    anchorOffset_ = {};
    trackedCenter_ = {};
    rawCenter_ = {};
    correctionState_.clear();
    anchorConfigured_ = false;
    loadConfiguration();
    if (visible_) {
        logEvent(QStringLiteral("CONTEXT"),
                 QStringLiteral("profile=%1 resolution=%2x%3 loaded=%4")
                     .arg(profileId_).arg(contextWidth_).arg(contextHeight_)
                     .arg(hasReference()));
    }
    emit changed();
}

void CenterVision::toggle()
{
    visible_ ? close() : open();
}

void CenterVision::open()
{
    if (visible_)
        return;
    if (startNewSessionOnOpen_) {
        if (sessionLog_.isOpen())
            sessionLog_.close();
        sessionDirectory_.clear();
        startNewSessionOnOpen_ = false;
    }
    visible_ = true;
    ensureSession();
    if (hasReference()) {
        setStage(Ready, QStringLiteral(
            "Образец загружен. Запусти слежение или сними новый кадр."));
    } else {
        setStage(Idle, QStringLiteral(
            "Шаг 1: заморозь кадр, на котором хорошо видна полоска HP героя."));
    }
    logEvent(QStringLiteral("LAB_OPEN"));
    emit changed();
}

void CenterVision::close()
{
    if (!visible_)
        return;
    stopTracking();
    ++contextGeneration_;
    ++grabRequestId_;
    grabInFlight_ = false;
    nativeFallbackStarted_ = false;
    visible_ = false;
    referenceCapturePending_ = false;
    frameSource_ = QUrl();
    correctionState_.clear();
    setStage(hasReference() ? Ready : Idle,
             QStringLiteral("F2 — открыть экспериментальный поиск центра"));
    logEvent(QStringLiteral("LAB_CLOSE"));
    startNewSessionOnOpen_ = true;
    emit changed();
}

void CenterVision::captureReference()
{
    if (!visible_)
        return;
    stopTracking();
    correctionState_.clear();
    ensureSession();
    if (grabInFlight_ || matchWatcher_.isRunning()) {
        status_ = QStringLiteral(
            "Дожидаюсь завершения предыдущего кадра перед новой заморозкой…");
        emit changed();
        if (referenceCapturePending_)
            return;
        referenceCapturePending_ = true;
        QTimer::singleShot(60, this, [this] {
            referenceCapturePending_ = false;
            if (visible_ && !tracking_)
                captureReference();
        });
        return;
    }
    referenceCapturePending_ = false;
    status_ = QStringLiteral("Захватываю чистый кадр поверхности Waydroid…");
    emit changed();
    requestGrab(GrabPurpose::Reference);
}

void CenterVision::setTemplateSelection(double x, double y, double width,
                                        double height)
{
    if (stage_ != SelectTemplate || referenceFrame_.isNull())
        return;
    QRectF selection(std::min(x, x + width), std::min(y, y + height),
                     std::abs(width), std::abs(height));
    selection = selection.intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    const QRect pixels = pixelRect(selection, referenceFrame_.size());
    if (pixels.width() < 20 || pixels.height() < 6) {
        status_ = QStringLiteral(
            "Область слишком маленькая. Обведи всю полоску HP вместе с рамкой.");
        logEvent(QStringLiteral("TEMPLATE_REJECT"),
                 QStringLiteral("rect=%1 pixels=%2x%3")
                     .arg(rectText(selection)).arg(pixels.width()).arg(pixels.height()));
        emit changed();
        return;
    }

    templateRect_ = selection;
    matchRect_ = selection;
    templateImage_ = referenceFrame_.copy(pixels).convertToFormat(QImage::Format_RGB32);
    anchorConfigured_ = false;
    const QString directory = configurationDirectory();
    QDir().mkpath(directory);
    templateImage_.save(directory + QStringLiteral("/template.png"), "PNG");
    templateImage_.save(sessionDirectory_ + QStringLiteral("/template.png"), "PNG");
    logEvent(QStringLiteral("TEMPLATE_ACCEPT"),
             QStringLiteral("rect=%1 pixels=%2x%3")
                 .arg(rectText(templateRect_)).arg(pixels.width()).arg(pixels.height()));
    setStage(SelectAnchor, QStringLiteral(
        "Шаг 3: кликни в настоящий центр персонажа — точку на земле под героем."));
}

void CenterVision::setAnchorPoint(double x, double y)
{
    if (stage_ != SelectAnchor || templateRect_.isEmpty())
        return;
    const QPointF anchor(std::clamp(x, 0.0, 1.0),
                         std::clamp(y, 0.0, 1.0));
    anchorOffset_ = anchor - templateRect_.topLeft();
    trackedCenter_ = anchor;
    rawCenter_ = anchor;
    anchorConfigured_ = true;
    saveConfiguration();
    logEvent(QStringLiteral("ANCHOR_ACCEPT"),
             QStringLiteral("anchor=%1 offset=%2")
                 .arg(pointText(anchor), pointText(anchorOffset_)));
    setStage(Ready, QStringLiteral(
        "Образец готов. Нажми «Начать слежение» и двигай героя/камеру."));
}

void CenterVision::startTracking()
{
    if (!visible_ || tracking_ || !hasReference()) {
        if (!hasReference()) {
            status_ = QStringLiteral(
                "Сначала заморозь кадр, обведи HP и укажи центр героя.");
            emit changed();
        }
        return;
    }
    if (!surface_) {
        status_ = QStringLiteral("Поверхность Android недоступна. Переоткрой Integrated Android.");
        emit changed();
        return;
    }

    resetTrackingState();
    correctionState_.clear();
    ++trackingGeneration_;
    tracking_ = true;
    stage_ = Tracking;
    trackingState_ = QStringLiteral("ACQUIRING");
    status_ = QStringLiteral(
        "Зрение запущено. Играй, а при ошибке нажми «Центр неверный». ");
    trackingTimer_.restart();
    logEvent(QStringLiteral("TRACK_START"),
             QStringLiteral("threshold=%1 template=%2 anchorOffset=%3")
                 .arg(threshold_, 0, 'f', 3).arg(rectText(templateRect_))
                 .arg(pointText(anchorOffset_)));
    emit changed();
    scheduleTrackingGrab(0);
}

void CenterVision::stopTracking()
{
    if (!tracking_ && stage_ != Tracking)
        return;
    tracking_ = false;
    ++trackingGeneration_;
    if (stage_ == Tracking)
        stage_ = hasReference() ? Ready : Idle;
    trackingState_ = QStringLiteral("IDLE");
    status_ = hasReference()
        ? QStringLiteral("Слежение остановлено. Образец и логи сохранены.")
        : QStringLiteral("Слежение остановлено.");
    logEvent(QStringLiteral("TRACK_STOP"),
             QStringLiteral("frames=%1 lost=%2").arg(frameNumber_).arg(lostFrames_));
    emit changed();
}

void CenterVision::markGood()
{
    if (lastFrame_.isNull())
        return;
    logEvent(QStringLiteral("USER_GOOD"),
             QStringLiteral("frame=%1 state=%2 score=%3 confidence=%4 center=%5")
                 .arg(frameNumber_).arg(trackingState_)
                 .arg(score_, 0, 'f', 4).arg(confidence_, 0, 'f', 4)
                 .arg(pointText(trackedCenter_)));
    saveDiagnosticFrame(QStringLiteral("user_good"), true);
    appendLabel(QStringLiteral("good"), trackedCenter_);
    status_ = QStringLiteral("Оценка «хорошо» записана вместе с кадром и координатами.");
    emit changed();
}

void CenterVision::beginCorrection()
{
    if (lastFrame_.isNull() || matchRect_.isEmpty()) {
        status_ = QStringLiteral("Пока нечего исправлять — дождись первого найденного кадра.");
        emit changed();
        return;
    }
    const bool wasTracking = tracking_;
    correctionState_ = trackingState_;
    stopTracking();
    ensureSession();
    const QString path = sessionDirectory_
        + QStringLiteral("/correction_%1.png").arg(frameNumber_, 6, 10, QLatin1Char('0'));
    lastFrame_.save(path, "PNG");
    updateFrameSource(path);
    stage_ = CorrectAnchor;
    status_ = QStringLiteral(
        "Кликни в правильный центр героя. Ошибка и кадр попадут в диагностику.");
    logEvent(QStringLiteral("CORRECTION_BEGIN"),
             QStringLiteral("frame=%1 wasTracking=%2 predicted=%3 match=%4")
                 .arg(frameNumber_).arg(wasTracking).arg(pointText(trackedCenter_))
                 .arg(rectText(matchRect_)));
    emit changed();
}

void CenterVision::setCorrectionPoint(double x, double y)
{
    if (stage_ != CorrectAnchor || matchRect_.isEmpty())
        return;
    const QPointF corrected(std::clamp(x, 0.0, 1.0),
                            std::clamp(y, 0.0, 1.0));
    const QPointF previous = trackedCenter_;
    const double errorPixels = std::hypot(
        (corrected.x() - previous.x()) * std::max(1, contextWidth_),
        (corrected.y() - previous.y()) * std::max(1, contextHeight_));
    const double targetMismatchThreshold = std::max(
        140.0, std::min(contextWidth_, contextHeight_) * 0.18);
    if (errorPixels > targetMismatchThreshold) {
        appendLabel(QStringLiteral("wrong_target"), previous, corrected);
        logEvent(QStringLiteral("USER_WRONG_TARGET"),
                 QStringLiteral("frame=%1 predicted=%2 corrected=%3 errorPx=%4 "
                                "thresholdPx=%5 anchorKept=%6")
                     .arg(frameNumber_).arg(pointText(previous))
                     .arg(pointText(corrected)).arg(errorPixels, 0, 'f', 2)
                     .arg(targetMismatchThreshold, 0, 'f', 2)
                     .arg(pointText(anchorOffset_)));
        saveDiagnosticFrame(QStringLiteral("user_wrong_target"), true);
        correctionState_.clear();
        setStage(Ready, QStringLiteral(
            "Похоже, зрение выбрало чужую HP-полоску. Ошибка записана, "
            "а прежнее смещение центра сохранено. Запусти слежение снова."));
        return;
    }
    anchorOffset_ = corrected - matchRect_.topLeft();
    trackedCenter_ = corrected;
    rawCenter_ = corrected;
    anchorConfigured_ = true;
    saveConfiguration();
    appendLabel(QStringLiteral("bad"), previous, corrected);
    logEvent(QStringLiteral("USER_CORRECTION"),
             QStringLiteral("frame=%1 predicted=%2 corrected=%3 errorPx=%4 newOffset=%5")
                 .arg(frameNumber_).arg(pointText(previous)).arg(pointText(corrected))
                 .arg(errorPixels, 0, 'f', 2).arg(pointText(anchorOffset_)));
    saveDiagnosticFrame(QStringLiteral("user_correction"), true);
    correctionState_.clear();
    setStage(Ready, QStringLiteral(
        "Поправка сохранена. Запусти слежение снова и проверь результат."));
}

void CenterVision::setThreshold(double value)
{
    value = std::clamp(value, 0.35, 0.95);
    if (qFuzzyCompare(threshold_, value))
        return;
    threshold_ = value;
    saveConfiguration();
    logEvent(QStringLiteral("THRESHOLD"),
             QStringLiteral("value=%1").arg(threshold_, 0, 'f', 3));
    emit changed();
}

void CenterVision::openSessionFolder()
{
    ensureSession();
    QDesktopServices::openUrl(QUrl::fromLocalFile(sessionDirectory_));
    logEvent(QStringLiteral("OPEN_SESSION_FOLDER"));
}

void CenterVision::exportDiagnostics()
{
    ensureSession();
    if (sessionLog_.isOpen())
        sessionLog_.flush();
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString destination = QDir::homePath()
        + QStringLiteral("/ewm-center-vision-%1.tar.gz").arg(timestamp);
    const QFileInfo sessionInfo(sessionDirectory_);
    QProcess tar;
    tar.start(QStringLiteral("tar"), {
        QStringLiteral("-czf"), destination,
        QStringLiteral("-C"), sessionInfo.absolutePath(), sessionInfo.fileName()
    });
    if (!tar.waitForFinished(15000) || tar.exitCode() != 0) {
        status_ = QStringLiteral("Не удалось собрать архив. Папка с логами сохранена.");
        logEvent(QStringLiteral("EXPORT_FAIL"),
                 QString::fromUtf8(tar.readAllStandardError()).trimmed());
        emit changed();
        return;
    }
    status_ = QStringLiteral("Диагностический пакет: %1").arg(destination);
    logEvent(QStringLiteral("EXPORT_OK"), destination);
    emit diagnosticsExported(destination);
    emit changed();
}

std::vector<CenterVision::Feature> CenterVision::buildFeatures(
    const QImage &source, int maximumFeatures)
{
    const QImage image = source.convertToFormat(QImage::Format_RGB32);
    std::vector<Feature> features;
    if (image.width() < 5 || image.height() < 5)
        return features;

    for (int y = 2; y < image.height() - 2; y += 2) {
        for (int x = 2; x < image.width() - 2; x += 2) {
            const double nx = x / static_cast<double>(image.width());
            const double ny = y / static_cast<double>(image.height());
            // HP changes inside the bar. Prefer its frame, corners, text and
            // surrounding fixed pixels; only sample a small part of the fill.
            const bool stableBorder = nx < 0.24 || nx > 0.76
                || ny < 0.30 || ny > 0.70;
            if (!stableBorder && ((x + y) % 8 != 0))
                continue;
            const QRgb pixel = image.pixel(x, y);
            const int gradient = std::abs(luminance(image.pixel(x + 1, y))
                                           - luminance(image.pixel(x - 1, y)))
                + std::abs(luminance(image.pixel(x, y + 1))
                           - luminance(image.pixel(x, y - 1)));
            const int maximum = std::max({qRed(pixel), qGreen(pixel), qBlue(pixel)});
            const int minimum = std::min({qRed(pixel), qGreen(pixel), qBlue(pixel)});
            features.push_back({x, y, qRed(pixel), qGreen(pixel), qBlue(pixel),
                                gradient, gradient * 3 + maximum - minimum});
        }
    }
    std::sort(features.begin(), features.end(),
              [](const Feature &left, const Feature &right) {
        return left.quality > right.quality;
    });
    if (static_cast<int>(features.size()) > maximumFeatures)
        features.resize(static_cast<std::size_t>(maximumFeatures));
    return features;
}

double CenterVision::featureScore(const QImage &image, int left, int top,
                                  const std::vector<Feature> &features)
{
    if (features.empty())
        return 0.0;
    double difference = 0.0;
    for (const Feature &feature : features) {
        const int x = left + feature.x;
        const int y = top + feature.y;
        const QRgb pixel = image.pixel(x, y);
        const double colorDifference = (
            std::abs(qRed(pixel) - feature.red)
            + std::abs(qGreen(pixel) - feature.green)
            + std::abs(qBlue(pixel) - feature.blue)) / 765.0;
        const int gradient = std::abs(luminance(image.pixel(x + 1, y))
                                       - luminance(image.pixel(x - 1, y)))
            + std::abs(luminance(image.pixel(x, y + 1))
                       - luminance(image.pixel(x, y - 1)));
        const double gradientDifference = std::min(
            1.0, std::abs(gradient - feature.gradient) / 255.0);
        difference += colorDifference * 0.72 + gradientDifference * 0.28;
    }
    return std::clamp(1.0 - difference / features.size(), 0.0, 1.0);
}

CenterVision::MatchResult CenterVision::matchFrame(const MatchRequest &request)
{
    MatchResult result;
    result.generation = request.generation;
    QElapsedTimer timer;
    timer.start();
    if (request.frame.isNull() || request.reference.isNull()) {
        result.failure = QStringLiteral("empty frame or template");
        return result;
    }

    const double scale = std::min(
        1.0, WorkingWidth / static_cast<double>(request.frame.width()));
    QImage frame = request.frame.convertToFormat(QImage::Format_RGB32);
    QImage reference = request.reference.convertToFormat(QImage::Format_RGB32);
    if (scale < 0.999) {
        frame = frame.scaled(qRound(frame.width() * scale),
                             qRound(frame.height() * scale),
                             Qt::IgnoreAspectRatio, Qt::FastTransformation);
        reference = reference.scaled(
            std::max(5, qRound(reference.width() * scale)),
            std::max(5, qRound(reference.height() * scale)),
            Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (reference.width() + 4 >= frame.width()
        || reference.height() + 4 >= frame.height()) {
        result.failure = QStringLiteral("template is larger than working frame");
        return result;
    }

    const std::vector<Feature> allFeatures = buildFeatures(reference, 220);
    if (allFeatures.size() < 12) {
        result.failure = QStringLiteral("not enough stable template features");
        return result;
    }
    std::vector<Feature> coarseFeatures;
    for (std::size_t index = 0; index < allFeatures.size(); index += 3)
        coarseFeatures.push_back(allFeatures[index]);
    result.featureCount = static_cast<int>(allFeatures.size());

    const int maximumLeft = frame.width() - reference.width() - 2;
    const int maximumTop = frame.height() - reference.height() - 2;
    int left = 2;
    int right = maximumLeft;
    int top = 2;
    int bottom = maximumTop;
    int expectedLeft = 0;
    int expectedTop = 0;
    result.fullSearch = request.previousRect.isEmpty() || request.lostFrames >= 4;
    if (!result.fullSearch) {
        expectedLeft = qRound(request.previousRect.x() * frame.width());
        expectedTop = qRound(request.previousRect.y() * frame.height());
        const int horizontalRadius = std::max(150, reference.width() * 5);
        const int verticalRadius = std::max(110, reference.height() * 9);
        left = std::clamp(expectedLeft - horizontalRadius, 2, maximumLeft);
        right = std::clamp(expectedLeft + horizontalRadius, 2, maximumLeft);
        top = std::clamp(expectedTop - verticalRadius, 2, maximumTop);
        bottom = std::clamp(expectedTop + verticalRadius, 2, maximumTop);
    }

    struct Candidate {
        int x = 0;
        int y = 0;
        double score = 0.0;
    };
    std::vector<Candidate> coarse;
    const int coarseStep = result.fullSearch ? 8 : 5;
    const int candidateSeparation = std::max(18, reference.height() * 2);
    auto keepCandidate = [candidateSeparation](
                            std::vector<Candidate> &candidates,
                            const Candidate &candidate, int maximum) {
        for (Candidate &existing : candidates) {
            if (std::hypot(existing.x - candidate.x, existing.y - candidate.y)
                < candidateSeparation) {
                if (candidate.score > existing.score)
                    existing = candidate;
                std::sort(candidates.begin(), candidates.end(),
                          [](const Candidate &a, const Candidate &b) {
                    return a.score > b.score;
                });
                return;
            }
        }
        candidates.push_back(candidate);
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate &a, const Candidate &b) {
            return a.score > b.score;
        });
        if (static_cast<int>(candidates.size()) > maximum)
            candidates.resize(static_cast<std::size_t>(maximum));
    };

    for (int y = top; y <= bottom; y += coarseStep) {
        for (int x = left; x <= right; x += coarseStep) {
            double candidateScore = featureScore(frame, x, y, coarseFeatures);
            if (!result.fullSearch) {
                const double distance = std::hypot(
                    static_cast<double>(x - expectedLeft),
                    static_cast<double>(y - expectedTop));
                candidateScore -= std::min(0.06, distance / 280.0 * 0.035);
            }
            keepCandidate(coarse, {x, y, candidateScore}, 14);
            ++result.coarseCandidates;
        }
    }
    if (coarse.empty()) {
        result.failure = QStringLiteral("search rectangle has no candidates");
        return result;
    }

    std::vector<Candidate> refined;
    for (const Candidate &candidate : coarse) {
        for (int y = std::max(2, candidate.y - coarseStep);
             y <= std::min(maximumTop, candidate.y + coarseStep); ++y) {
            for (int x = std::max(2, candidate.x - coarseStep);
                 x <= std::min(maximumLeft, candidate.x + coarseStep); ++x) {
                double refinedScore = featureScore(frame, x, y, allFeatures);
                if (!result.fullSearch) {
                    const double distance = std::hypot(
                        static_cast<double>(x - expectedLeft),
                        static_cast<double>(y - expectedTop));
                    refinedScore -= std::min(0.06, distance / 280.0 * 0.035);
                }
                refined.push_back({x, y, refinedScore});
                ++result.refinedCandidates;
            }
        }
    }
    std::sort(refined.begin(), refined.end(),
              [](const Candidate &a, const Candidate &b) {
        return a.score > b.score;
    });
    const Candidate best = refined.front();
    double second = 0.0;
    const double separation = std::max(reference.width(), reference.height()) * 0.65;
    for (const Candidate &candidate : refined) {
        if (std::hypot(candidate.x - best.x, candidate.y - best.y) >= separation) {
            second = candidate.score;
            break;
        }
    }
    result.bestScore = best.score;
    result.secondScore = second;
    const double absoluteConfidence = std::clamp(
        (best.score - 0.42) / 0.48, 0.0, 1.0);
    const double marginConfidence = std::clamp(
        (best.score - second) / 0.10, 0.0, 1.0);
    result.confidence = absoluteConfidence * 0.82 + marginConfidence * 0.18;
    const double competitorMargin = second == 0.0 ? 1.0 : best.score - second;
    const bool unambiguous = !result.fullSearch || competitorMargin >= 0.018;
    result.valid = best.score >= request.threshold && unambiguous;
    if (!unambiguous)
        result.failure = QStringLiteral("ambiguous full-frame reacquisition");
    result.rect = QRectF(best.x / static_cast<double>(frame.width()),
                         best.y / static_cast<double>(frame.height()),
                         reference.width() / static_cast<double>(frame.width()),
                         reference.height() / static_cast<double>(frame.height()));
    result.analysisMs = static_cast<int>(timer.elapsed());
    return result;
}

void CenterVision::setStage(Stage stage, const QString &status)
{
    stage_ = stage;
    status_ = status;
    emit changed();
}

void CenterVision::requestGrab(GrabPurpose purpose)
{
    if (grabInFlight_ || matchWatcher_.isRunning())
        return;
    if (!surface_) {
        status_ = QStringLiteral("Нечего захватывать: Android surface отсутствует.");
        logEvent(QStringLiteral("GRAB_SKIP"), QStringLiteral("surface=null"));
        emit changed();
        return;
    }
    grabInFlight_ = true;
    nativeFallbackStarted_ = false;
    pendingGrabPurpose_ = purpose;
    const int requestId = ++grabRequestId_;
    captureTimer_.restart();
    logEvent(QStringLiteral("GRAB_REQUEST"),
             QStringLiteral("id=%1 purpose=%2 surface=%3")
                 .arg(requestId)
                 .arg(purpose == GrabPurpose::Reference ? QStringLiteral("reference")
                                                        : QStringLiteral("tracking"))
                 .arg(surface_ ? QStringLiteral("attached") : QStringLiteral("null")));
    emit renderedFrameRequested(requestId);

    // QML normally returns the texture actually drawn by ShellSurfaceItem.  If
    // the render grab cannot answer (backend/driver peculiarity), retain the
    // native Wayland grabber as a bounded fallback instead of hanging forever.
    QTimer::singleShot(1200, this, [this, requestId] {
        if (grabInFlight_ && requestId == grabRequestId_)
            requestNativeGrab(requestId);
    });
}

void CenterVision::submitRenderedFrame(int requestId, const QImage &image)
{
    if (!grabInFlight_ || requestId != grabRequestId_)
        return;
    acceptCapturedFrame(requestId, image, QStringLiteral("qml-render"));
}

void CenterVision::reportRenderedFrameFailure(int requestId,
                                               const QString &reason)
{
    if (!grabInFlight_ || requestId != grabRequestId_)
        return;
    logEvent(QStringLiteral("GRAB_RENDER_FAIL"),
             QStringLiteral("id=%1 reason=%2").arg(requestId).arg(reason));
    requestNativeGrab(requestId);
}

void CenterVision::requestNativeGrab(int requestId)
{
    if (!grabInFlight_ || requestId != grabRequestId_ || nativeFallbackStarted_)
        return;
    if (!surface_) {
        grabInFlight_ = false;
        handleGrabFailure(-2);
        return;
    }
    nativeFallbackStarted_ = true;
    logEvent(QStringLiteral("GRAB_NATIVE_FALLBACK"),
             QStringLiteral("id=%1").arg(requestId));
    const int contextGeneration = contextGeneration_;
    auto *grabber = new QWaylandSurfaceGrabber(surface_, this);
    connect(grabber, &QWaylandSurfaceGrabber::success, this,
            [this, grabber, requestId, contextGeneration](const QImage &image) {
        grabber->deleteLater();
        if (requestId != grabRequestId_)
            return;
        if (contextGeneration != contextGeneration_ || !visible_) {
            grabInFlight_ = false;
            if (tracking_)
                scheduleTrackingGrab(0);
            return;
        }
        acceptCapturedFrame(requestId, image, QStringLiteral("wayland-native"));
    });
    connect(grabber, &QWaylandSurfaceGrabber::failed, this,
            [this, grabber, requestId, contextGeneration](QWaylandSurfaceGrabber::Error error) {
        grabber->deleteLater();
        if (requestId != grabRequestId_)
            return;
        grabInFlight_ = false;
        if (contextGeneration != contextGeneration_ || !visible_) {
            if (tracking_)
                scheduleTrackingGrab(0);
            return;
        }
        handleGrabFailure(static_cast<int>(error));
    });
    grabber->grab();
}

bool CenterVision::validateCapturedFrame(const QImage &source, QString *metrics,
                                         QString *reason) const
{
    if (source.isNull() || source.width() < 32 || source.height() < 32) {
        if (metrics)
            *metrics = QStringLiteral("null-or-small size=%1x%2")
                .arg(source.width()).arg(source.height());
        if (reason)
            *reason = QStringLiteral("empty image");
        return false;
    }

    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int stepX = std::max(1, image.width() / 96);
    const int stepY = std::max(1, image.height() / 96);
    qint64 sum = 0;
    qint64 sumSquares = 0;
    int count = 0;
    int visible = 0;
    int nonBlack = 0;
    int minimum = 255;
    int maximum = 0;
    for (int y = stepY / 2; y < image.height(); y += stepY) {
        for (int x = stepX / 2; x < image.width(); x += stepX) {
            const QRgb pixel = image.pixel(x, y);
            const int light = luminance(pixel);
            ++count;
            visible += qAlpha(pixel) >= 16;
            nonBlack += light >= 8;
            minimum = std::min(minimum, light);
            maximum = std::max(maximum, light);
            sum += light;
            sumSquares += static_cast<qint64>(light) * light;
        }
    }
    const double mean = count ? sum / static_cast<double>(count) : 0.0;
    const double variance = count
        ? std::max(0.0, sumSquares / static_cast<double>(count) - mean * mean)
        : 0.0;
    const double deviation = std::sqrt(variance);
    const double visibleRatio = count ? visible / static_cast<double>(count) : 0.0;
    const double nonBlackRatio = count ? nonBlack / static_cast<double>(count) : 0.0;
    if (metrics) {
        *metrics = QStringLiteral(
            "size=%1x%2 format=%3 dpr=%4 samples=%5 alpha=%6 mean=%7 stddev=%8 range=%9-%10 nonblack=%11")
            .arg(image.width()).arg(image.height()).arg(static_cast<int>(source.format()))
            .arg(source.devicePixelRatio(), 0, 'f', 2).arg(count)
            .arg(visibleRatio, 0, 'f', 4).arg(mean, 0, 'f', 2)
            .arg(deviation, 0, 'f', 2).arg(minimum).arg(maximum)
            .arg(nonBlackRatio, 0, 'f', 4);
    }
    const bool valid = visibleRatio > 0.10
        && !(mean < 4.0 && deviation < 3.0)
        && (maximum - minimum >= 5 || deviation >= 2.5)
        && nonBlackRatio > 0.01;
    if (!valid && reason)
        *reason = QStringLiteral("blank/black compositor buffer");
    return valid;
}

void CenterVision::acceptCapturedFrame(int requestId, const QImage &image,
                                       const QString &source)
{
    if (!grabInFlight_ || requestId != grabRequestId_)
        return;
    QString metrics;
    QString reason;
    const bool valid = validateCapturedFrame(image, &metrics, &reason);
    logEvent(valid ? QStringLiteral("GRAB_VALID") : QStringLiteral("GRAB_INVALID"),
             QStringLiteral("id=%1 source=%2 %3 reason=%4")
                 .arg(requestId).arg(source).arg(metrics).arg(reason));
    if (!valid) {
        if (!image.isNull()) {
            ensureSession();
            const QString invalidPath = sessionDirectory_
                + QStringLiteral("/invalid-grab_%1_%2.png")
                      .arg(source)
                      .arg(QDateTime::currentDateTime().toString("HHmmss-zzz"));
            const bool saved = image.save(invalidPath, "PNG");
            logEvent(QStringLiteral("GRAB_INVALID_SAVED"),
                     QStringLiteral("saved=%1 path=%2 bytes=%3")
                         .arg(saved).arg(invalidPath)
                         .arg(QFileInfo(invalidPath).size()));
        }
        if (source != QStringLiteral("wayland-native")) {
            requestNativeGrab(requestId);
            return;
        }
        grabInFlight_ = false;
        status_ = QStringLiteral(
            "Qt вернул пустой/чёрный кадр. Живой экран оставлен видимым; нажми «Собрать диагностику зрения».");
        emit changed();
        if (tracking_)
            scheduleTrackingGrab(250);
        return;
    }

    grabInFlight_ = false;
    const int captureMs = static_cast<int>(captureTimer_.elapsed());
    handleGrabbedFrame(pendingGrabPurpose_, image, captureMs);
}

void CenterVision::handleGrabbedFrame(GrabPurpose purpose, const QImage &image,
                                      int captureMs)
{
    if (image.isNull()) {
        handleGrabFailure(-1);
        return;
    }
    lastFrame_ = image.convertToFormat(QImage::Format_RGB32);
    if (purpose == GrabPurpose::Reference) {
        referenceFrame_ = lastFrame_;
        ensureSession();
        const QString sessionPath = sessionDirectory_
            + QStringLiteral("/reference_%1.png")
                  .arg(QDateTime::currentDateTime().toString("HHmmss-zzz"));
        const QString directory = configurationDirectory();
        QDir().mkpath(directory);
        const bool sessionSaved = referenceFrame_.save(sessionPath, "PNG");
        const QString persistentPath = directory + QStringLiteral("/reference.png");
        const bool persistentSaved = referenceFrame_.save(persistentPath, "PNG");
        logEvent(QStringLiteral("REFERENCE_SAVE"),
                 QStringLiteral("session=%1 persistent=%2 sessionExists=%3 sessionBytes=%4 path=%5")
                     .arg(sessionSaved).arg(persistentSaved)
                     .arg(QFileInfo::exists(sessionPath))
                     .arg(QFileInfo(sessionPath).size()).arg(sessionPath));
        if (!sessionSaved || !QFileInfo::exists(sessionPath)) {
            referenceFrame_ = {};
            status_ = QStringLiteral(
                "Кадр получен, но PNG не удалось сохранить. Нажми «Диагностика» в шапке.");
            emit changed();
            return;
        }
        updateFrameSource(sessionPath);
        templateImage_ = {};
        templateRect_ = {};
        matchRect_ = {};
        anchorConfigured_ = false;
        logEvent(QStringLiteral("REFERENCE_CAPTURED"),
                 QStringLiteral("captureMs=%1 size=%2x%3 path=%4")
                     .arg(captureMs).arg(image.width()).arg(image.height())
                     .arg(sessionPath));
        setStage(SelectTemplate, QStringLiteral(
            "Шаг 2: зажми ЛКМ и точно обведи полоску HP вместе с её рамкой."));
        return;
    }

    if (!tracking_)
        return;
    MatchRequest request;
    request.frame = lastFrame_;
    request.reference = templateImage_;
    request.previousRect = matchRect_;
    request.generation = trackingGeneration_;
    request.lostFrames = lostFrames_;
    request.threshold = threshold_;
    logEvent(QStringLiteral("FRAME_CAPTURE"),
             QStringLiteral("frame=%1 captureMs=%2 size=%3x%4 previous=%5 lost=%6")
                 .arg(frameNumber_ + 1).arg(captureMs).arg(image.width())
                 .arg(image.height()).arg(rectText(matchRect_)).arg(lostFrames_));
    matchWatcher_.setFuture(QtConcurrent::run(&CenterVision::matchFrame, request));
}

void CenterVision::handleGrabFailure(int error)
{
    logEvent(QStringLiteral("GRAB_FAIL"),
             QStringLiteral("error=%1 tracking=%2").arg(error).arg(tracking_));
    status_ = QStringLiteral("Захват кадра не удался (Qt error %1).").arg(error);
    emit changed();
    if (tracking_)
        scheduleTrackingGrab(250);
}

void CenterVision::scheduleTrackingGrab(int delayMs)
{
    QTimer::singleShot(delayMs, this, [this] {
        if (tracking_ && visible_ && !grabInFlight_ && !matchWatcher_.isRunning())
            requestGrab(GrabPurpose::Tracking);
    });
}

void CenterVision::handleMatchFinished()
{
    if (!tracking_)
        return;
    const MatchResult result = matchWatcher_.result();
    if (result.generation != trackingGeneration_) {
        logEvent(QStringLiteral("MATCH_STALE"),
                 QStringLiteral("resultGeneration=%1 activeGeneration=%2")
                     .arg(result.generation).arg(trackingGeneration_));
        scheduleTrackingGrab(0);
        return;
    }
    applyMatch(result);
    scheduleTrackingGrab(TrackingIntervalMs);
}

void CenterVision::applyMatch(const MatchResult &result)
{
    ++frameNumber_;
    const QString previousState = trackingState_;
    score_ = result.bestScore;
    confidence_ = result.confidence;

    if (result.valid) {
        lostFrames_ = 0;
        trackingState_ = QStringLiteral("LOCKED");
        matchRect_ = result.rect;
        rawCenter_ = matchRect_.topLeft() + anchorOffset_;
        if (trackedCenter_.isNull()) {
            trackedCenter_ = rawCenter_;
            velocity_ = {};
        } else {
            const QPointF previous = trackedCenter_;
            const QPointF predicted = trackedCenter_ + velocity_;
            trackedCenter_ = predicted * 0.24 + rawCenter_ * 0.76;
            const QPointF measuredVelocity = trackedCenter_ - previous;
            velocity_ = velocity_ * 0.62 + measuredVelocity * 0.38;
        }
    } else {
        ++lostFrames_;
        if (lostFrames_ <= 5 && !trackedCenter_.isNull()) {
            trackingState_ = QStringLiteral("COASTING");
            trackedCenter_ += velocity_;
            matchRect_.translate(velocity_);
            velocity_ *= 0.82;
        } else {
            trackingState_ = QStringLiteral("LOST");
        }
    }

    const qint64 elapsed = std::max<qint64>(1, trackingTimer_.elapsed());
    analysisFps_ = frameNumber_ * 1000.0 / elapsed;
    status_ = trackingState_ == QStringLiteral("LOCKED")
        ? QStringLiteral("Цель удерживается. Оцени рамку и крест центра.")
        : (trackingState_ == QStringLiteral("COASTING")
           ? QStringLiteral("Полоска временно потеряна — работает прогноз движения.")
           : QStringLiteral("Цель потеряна. Поиск расширен на весь кадр."));

    logEvent(QStringLiteral("MATCH"),
             QStringLiteral("frame=%1 mode=%2 valid=%3 score=%4 second=%5 margin=%6 "
                            "confidence=%7 state=%8 lost=%9 rect=%10 rawCenter=%11 "
                            "smoothCenter=%12 velocity=%13 features=%14 coarse=%15 "
                            "refined=%16 analysisMs=%17 fps=%18 failure='%19'")
                 .arg(frameNumber_).arg(result.fullSearch ? "FULL" : "LOCAL")
                 .arg(result.valid).arg(result.bestScore, 0, 'f', 5)
                 .arg(result.secondScore, 0, 'f', 5)
                 .arg(result.bestScore - result.secondScore, 0, 'f', 5)
                 .arg(result.confidence, 0, 'f', 5).arg(trackingState_)
                 .arg(lostFrames_).arg(rectText(matchRect_))
                 .arg(pointText(rawCenter_)).arg(pointText(trackedCenter_))
                 .arg(pointText(velocity_)).arg(result.featureCount)
                 .arg(result.coarseCandidates).arg(result.refinedCandidates)
                 .arg(result.analysisMs).arg(analysisFps_, 0, 'f', 2)
                 .arg(result.failure));

    const bool transition = previousState != trackingState_;
    if (transition || frameNumber_ % 20 == 0)
        saveDiagnosticFrame(transition
            ? QStringLiteral("state_%1_to_%2").arg(previousState, trackingState_)
            : QStringLiteral("periodic"), transition);
    emit changed();
}

void CenterVision::saveConfiguration()
{
    if (contextWidth_ <= 0 || contextHeight_ <= 0)
        return;
    const QString directory = configurationDirectory();
    QDir().mkpath(directory);
    if (!referenceFrame_.isNull())
        referenceFrame_.save(directory + QStringLiteral("/reference.png"), "PNG");
    if (!templateImage_.isNull())
        templateImage_.save(directory + QStringLiteral("/template.png"), "PNG");

    QJsonObject object{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("profile"), profileId_},
        {QStringLiteral("width"), contextWidth_},
        {QStringLiteral("height"), contextHeight_},
        {QStringLiteral("templateX"), templateRect_.x()},
        {QStringLiteral("templateY"), templateRect_.y()},
        {QStringLiteral("templateWidth"), templateRect_.width()},
        {QStringLiteral("templateHeight"), templateRect_.height()},
        {QStringLiteral("anchorOffsetX"), anchorOffset_.x()},
        {QStringLiteral("anchorOffsetY"), anchorOffset_.y()},
        {QStringLiteral("threshold"), threshold_},
        {QStringLiteral("updated"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs)}
    };
    const QByteArray configuration =
        QJsonDocument(object).toJson(QJsonDocument::Indented);
    QSaveFile file(directory + QStringLiteral("/config.json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(configuration);
        file.commit();
    }
    if (!sessionDirectory_.isEmpty()) {
        QSaveFile snapshot(sessionDirectory_ + QStringLiteral("/config-current.json"));
        if (snapshot.open(QIODevice::WriteOnly)) {
            snapshot.write(configuration);
            snapshot.commit();
        }
    }
}

void CenterVision::loadConfiguration()
{
    const QString directory = configurationDirectory();
    QFile file(directory + QStringLiteral("/config.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        stage_ = Idle;
        return;
    }
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    referenceFrame_.load(directory + QStringLiteral("/reference.png"));
    templateImage_.load(directory + QStringLiteral("/template.png"));
    templateRect_ = QRectF(object.value(QStringLiteral("templateX")).toDouble(),
                           object.value(QStringLiteral("templateY")).toDouble(),
                           object.value(QStringLiteral("templateWidth")).toDouble(),
                           object.value(QStringLiteral("templateHeight")).toDouble());
    matchRect_ = templateRect_;
    anchorOffset_ = QPointF(
        object.value(QStringLiteral("anchorOffsetX")).toDouble(),
        object.value(QStringLiteral("anchorOffsetY")).toDouble());
    threshold_ = std::clamp(
        object.value(QStringLiteral("threshold")).toDouble(0.68), 0.35, 0.95);
    anchorConfigured_ = !templateRect_.isEmpty() && !templateImage_.isNull()
        && !referenceFrame_.isNull();
    if (anchorConfigured_) {
        trackedCenter_ = templateRect_.topLeft() + anchorOffset_;
        rawCenter_ = trackedCenter_;
        stage_ = Ready;
        updateFrameSource(directory + QStringLiteral("/reference.png"));
    } else {
        stage_ = Idle;
    }
}

QString CenterVision::configurationDirectory() const
{
    return rootDirectory() + QStringLiteral("/configs/%1/%2x%3")
        .arg(safeComponent(profileId_)).arg(contextWidth_).arg(contextHeight_);
}

QString CenterVision::rootDirectory() const
{
    const QByteArray configured = qgetenv("XDG_STATE_HOME");
    const QString state = configured.isEmpty()
        ? QDir::homePath() + QStringLiteral("/.local/state")
        : QString::fromLocal8Bit(configured);
    return state + QStringLiteral("/evgenium-waydroid-mapper/center-vision");
}

void CenterVision::ensureSession()
{
    if (!sessionDirectory_.isEmpty())
        return;
    sessionDirectory_ = rootDirectory() + QStringLiteral("/sessions/")
        + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
    QDir().mkpath(sessionDirectory_ + QStringLiteral("/frames"));
    QDir().mkpath(sessionDirectory_ + QStringLiteral("/labels"));
    sessionLog_.setFileName(sessionDirectory_ + QStringLiteral("/vision.log"));
    sessionLog_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    const QString configuration = configurationDirectory();
    const auto snapshot = [this, &configuration](const QString &name) {
        const QString source = configuration + '/' + name;
        if (QFileInfo::exists(source))
            QFile::copy(source, sessionDirectory_ + QStringLiteral("/config-") + name);
    };
    snapshot(QStringLiteral("config.json"));
    snapshot(QStringLiteral("reference.png"));
    snapshot(QStringLiteral("template.png"));
    logEvent(QStringLiteral("SESSION_BEGIN"),
             QStringLiteral("profile=%1 resolution=%2x%3 qtThreads=%4")
                 .arg(profileId_).arg(contextWidth_).arg(contextHeight_)
                 .arg(QThread::idealThreadCount()));
    emit changed();
}

void CenterVision::logEvent(const QString &event, const QString &details)
{
    ensureSession();
    const QString line = QStringLiteral("[%1] event=%2 %3\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
             event, details);
    if (sessionLog_.isOpen()) {
        sessionLog_.write(line.toUtf8());
        sessionLog_.flush();
    }
    qInfo().noquote() << QStringLiteral("[EWM VISION] %1 %2").arg(event, details);
}

void CenterVision::appendLabel(const QString &verdict, const QPointF &predicted,
                               const QPointF &corrected)
{
    if (lastFrame_.isNull())
        return;
    ensureSession();
    const QString imageName = QStringLiteral("frame-%1-%2.png")
        .arg(frameNumber_, 6, 10, QLatin1Char('0')).arg(safeComponent(verdict));
    lastFrame_.save(sessionDirectory_ + QStringLiteral("/labels/") + imageName,
                    "PNG");
    QJsonObject label{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("timestamp"),
         QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
        {QStringLiteral("verdict"), verdict},
        {QStringLiteral("image"), QStringLiteral("labels/") + imageName},
        {QStringLiteral("frame"), frameNumber_},
        {QStringLiteral("profile"), profileId_},
        {QStringLiteral("width"), contextWidth_},
        {QStringLiteral("height"), contextHeight_},
        {QStringLiteral("state"), correctionState_.isEmpty()
             ? trackingState_ : correctionState_},
        {QStringLiteral("score"), score_},
        {QStringLiteral("confidence"), confidence_},
        {QStringLiteral("matchX"), matchRect_.x()},
        {QStringLiteral("matchY"), matchRect_.y()},
        {QStringLiteral("matchWidth"), matchRect_.width()},
        {QStringLiteral("matchHeight"), matchRect_.height()},
        {QStringLiteral("predictedX"), predicted.x()},
        {QStringLiteral("predictedY"), predicted.y()}
    };
    if (corrected.x() >= 0.0 && corrected.y() >= 0.0) {
        label.insert(QStringLiteral("correctedX"), corrected.x());
        label.insert(QStringLiteral("correctedY"), corrected.y());
        label.insert(QStringLiteral("errorPixels"), std::hypot(
            (corrected.x() - predicted.x()) * std::max(1, contextWidth_),
            (corrected.y() - predicted.y()) * std::max(1, contextHeight_)));
    }
    QFile file(sessionDirectory_ + QStringLiteral("/labels.jsonl"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(QJsonDocument(label).toJson(QJsonDocument::Compact));
        file.write("\n");
    }
    logEvent(QStringLiteral("LABEL_SAVED"),
             QStringLiteral("verdict=%1 image=%2").arg(verdict, imageName));
}

void CenterVision::saveDiagnosticFrame(const QString &reason, bool force)
{
    if (lastFrame_.isNull() || diagnosticFrames_ >= MaximumDiagnosticFrames)
        return;
    if (!force && frameNumber_ % 20 != 0)
        return;
    ensureSession();
    ++diagnosticFrames_;
    const QString path = sessionDirectory_ + QStringLiteral("/frames/%1_%2_%3.jpg")
        .arg(frameNumber_, 6, 10, QLatin1Char('0'))
        .arg(safeComponent(reason))
        .arg(score_, 0, 'f', 3);
    QImage diagnostic = lastFrame_;
    if (diagnostic.width() > 1280)
        diagnostic = diagnostic.scaledToWidth(1280, Qt::SmoothTransformation);
    QPainter painter(&diagnostic);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF match(matchRect_.x() * diagnostic.width(),
                       matchRect_.y() * diagnostic.height(),
                       matchRect_.width() * diagnostic.width(),
                       matchRect_.height() * diagnostic.height());
    painter.setPen(QPen(trackingState_ == QStringLiteral("LOCKED")
                            ? QColor("#35ef91") : QColor("#ffb64a"), 3));
    painter.drawRect(match);
    const QPointF raw(rawCenter_.x() * diagnostic.width(),
                      rawCenter_.y() * diagnostic.height());
    painter.setPen(QPen(QColor("#ff9f43"), 2));
    painter.drawEllipse(raw, 6, 6);
    const QPointF center(trackedCenter_.x() * diagnostic.width(),
                         trackedCenter_.y() * diagnostic.height());
    painter.setPen(QPen(QColor("#45f39c"), 3));
    painter.drawLine(center + QPointF(-18, 0), center + QPointF(18, 0));
    painter.drawLine(center + QPointF(0, -18), center + QPointF(0, 18));
    painter.fillRect(QRectF(8, 8, std::min(620, diagnostic.width() - 16), 34),
                     QColor(8, 15, 21, 210));
    painter.setPen(Qt::white);
    painter.drawText(QRectF(16, 8, diagnostic.width() - 32, 34),
                     Qt::AlignVCenter,
                     QStringLiteral("frame=%1 state=%2 score=%3 confidence=%4 reason=%5")
                         .arg(frameNumber_).arg(trackingState_)
                         .arg(score_, 0, 'f', 4).arg(confidence_, 0, 'f', 4)
                         .arg(reason));
    painter.end();
    diagnostic.save(path, "JPG", 78);
    logEvent(QStringLiteral("FRAME_SAVED"),
             QStringLiteral("reason=%1 path=%2 count=%3")
                 .arg(reason, path).arg(diagnosticFrames_));
}

void CenterVision::updateFrameSource(const QString &path)
{
    frameSource_ = QUrl::fromLocalFile(path);
}

QRect CenterVision::pixelRect(const QRectF &normalized, const QSize &size) const
{
    const int left = std::clamp(qFloor(normalized.left() * size.width()),
                                0, std::max(0, size.width() - 1));
    const int top = std::clamp(qFloor(normalized.top() * size.height()),
                               0, std::max(0, size.height() - 1));
    const int right = std::clamp(qCeil(normalized.right() * size.width()),
                                 left + 1, size.width());
    const int bottom = std::clamp(qCeil(normalized.bottom() * size.height()),
                                  top + 1, size.height());
    return QRect(left, top, right - left, bottom - top);
}

void CenterVision::resetTrackingState()
{
    frameNumber_ = 0;
    lostFrames_ = 0;
    diagnosticFrames_ = 0;
    score_ = 0.0;
    confidence_ = 0.0;
    analysisFps_ = 0.0;
    velocity_ = {};
    matchRect_ = templateRect_;
    trackedCenter_ = templateRect_.topLeft() + anchorOffset_;
    rawCenter_ = trackedCenter_;
}
