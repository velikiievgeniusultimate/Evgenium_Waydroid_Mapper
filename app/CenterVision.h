#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <deque>
#include <vector>

class QWaylandSurface;

class CenterVision final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(bool tracking READ tracking NOTIFY changed)
    Q_PROPERTY(bool hasReference READ hasReference NOTIFY changed)
    Q_PROPERTY(bool frameFrozen READ frameFrozen NOTIFY changed)
    Q_PROPERTY(int stage READ stage NOTIFY changed)
    Q_PROPERTY(QString stageName READ stageName NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString trackingState READ trackingState NOTIFY changed)
    Q_PROPERTY(QUrl frameSource READ frameSource NOTIFY changed)
    Q_PROPERTY(QRectF templateRect READ templateRect NOTIFY changed)
    Q_PROPERTY(QRectF matchRect READ matchRect NOTIFY changed)
    Q_PROPERTY(QPointF trackedCenter READ trackedCenter NOTIFY changed)
    Q_PROPERTY(QPointF rawCenter READ rawCenter NOTIFY changed)
    Q_PROPERTY(double score READ score NOTIFY changed)
    Q_PROPERTY(double confidence READ confidence NOTIFY changed)
    Q_PROPERTY(double heroGradientScore READ heroGradientScore NOTIFY changed)
    Q_PROPERTY(double threshold READ threshold WRITE setThreshold NOTIFY changed)
    Q_PROPERTY(double analysisFps READ analysisFps NOTIFY changed)
    Q_PROPERTY(int frameNumber READ frameNumber NOTIFY changed)
    Q_PROPERTY(QString sessionDirectory READ sessionDirectory NOTIFY changed)
    Q_PROPERTY(bool autotestRunning READ autotestRunning NOTIFY changed)
    Q_PROPERTY(int autotestElapsedSeconds READ autotestElapsedSeconds NOTIFY changed)
    Q_PROPERTY(int autotestDurationSeconds READ autotestDurationSeconds CONSTANT)
    Q_PROPERTY(double autotestProgress READ autotestProgress NOTIFY changed)
    Q_PROPERTY(QString autotestResult READ autotestResult NOTIFY changed)
    Q_PROPERTY(QString autotestSummary READ autotestSummary NOTIFY changed)
    Q_PROPERTY(bool syntheticPreflightPassed READ syntheticPreflightPassed NOTIFY changed)

public:
    enum Stage {
        Idle = 0,
        SelectTemplate = 1,
        SelectAnchor = 2,
        Ready = 3,
        Tracking = 4,
        CorrectAnchor = 5
    };
    Q_ENUM(Stage)

    explicit CenterVision(QObject *parent = nullptr);
    ~CenterVision() override;

    bool visible() const { return visible_; }
    bool tracking() const { return tracking_; }
    bool hasReference() const;
    bool frameFrozen() const;
    int stage() const { return static_cast<int>(stage_); }
    QString stageName() const;
    QString status() const { return status_; }
    QString trackingState() const { return trackingState_; }
    QUrl frameSource() const { return frameSource_; }
    QRectF templateRect() const { return templateRect_; }
    QRectF matchRect() const { return matchRect_; }
    QPointF trackedCenter() const { return trackedCenter_; }
    QPointF rawCenter() const { return rawCenter_; }
    double score() const { return score_; }
    double confidence() const { return confidence_; }
    double heroGradientScore() const { return heroGradientScore_; }
    double threshold() const { return threshold_; }
    double analysisFps() const { return analysisFps_; }
    int frameNumber() const { return frameNumber_; }
    QString sessionDirectory() const { return sessionDirectory_; }
    bool autotestRunning() const { return autotestRunning_; }
    int autotestElapsedSeconds() const { return autotestElapsedSeconds_; }
    int autotestDurationSeconds() const { return 600; }
    double autotestProgress() const;
    QString autotestResult() const { return autotestResult_; }
    QString autotestSummary() const { return autotestSummary_; }
    bool syntheticPreflightPassed() const { return syntheticPreflightPassed_; }

    void setSurface(QWaylandSurface *surface);
    void setContext(const QString &profileId, int width, int height);

public slots:
    void toggle();
    void open();
    void close();
    void captureReference();
    void setTemplateSelection(double x, double y, double width, double height);
    void setAnchorPoint(double x, double y);
    void startTracking();
    void stopTracking();
    void markGood();
    void beginCorrection();
    void setCorrectionPoint(double x, double y);
    void setThreshold(double value);
    void openSessionFolder();
    void exportDiagnostics();
    void submitRenderedFrame(int requestId, const QImage &image);
    void reportRenderedFrameFailure(int requestId, const QString &reason);
    void startAutotest();
    void finishAutotestEarly();

signals:
    void changed();
    void diagnosticsExported(const QString &path);
    void renderedFrameRequested(int requestId);

private:
    enum class GrabPurpose {
        Reference,
        Tracking
    };

    struct Feature {
        int x = 0;
        int y = 0;
        int red = 0;
        int green = 0;
        int blue = 0;
        int gradient = 0;
        int quality = 0;
    };

    struct MatchRequest {
        QImage frame;
        QImage reference;
        QRectF previousRect;
        int generation = 0;
        int lostFrames = 0;
        double threshold = 0.68;
        bool requireIdentity = true;
    };

    struct MatchResult {
        bool valid = false;
        bool fullSearch = false;
        int generation = 0;
        QRectF rect;
        double bestScore = 0.0;
        double secondScore = 0.0;
        double confidence = 0.0;
        int coarseCandidates = 0;
        int refinedCandidates = 0;
        int featureCount = 0;
        int heroGradientPixels = 0;
        double heroGradientScore = 0.0;
        double geometryScore = 0.0;
        bool identityPresent = false;
        int analysisMs = 0;
        QString failure;
    };

    static MatchResult matchFrame(const MatchRequest &request);
    static std::vector<Feature> buildFeatures(const QImage &image,
                                              int maximumFeatures);
    static double featureScore(const QImage &image, int left, int top,
                               const std::vector<Feature> &features);

    void setStage(Stage stage, const QString &status);
    void requestGrab(GrabPurpose purpose);
    void requestNativeGrab(int requestId);
    void acceptCapturedFrame(int requestId, const QImage &image,
                             const QString &source);
    bool validateCapturedFrame(const QImage &image, QString *metrics,
                               QString *reason) const;
    void handleGrabbedFrame(GrabPurpose purpose, const QImage &image,
                            int captureMs);
    void handleGrabFailure(int error);
    void scheduleTrackingGrab(int delayMs = 70);
    void handleMatchFinished();
    void applyMatch(const MatchResult &result);
    void saveConfiguration();
    void loadConfiguration();
    QString configurationDirectory() const;
    QString rootDirectory() const;
    void ensureSession();
    void logEvent(const QString &event, const QString &details = {});
    void appendLabel(const QString &verdict, const QPointF &predicted,
                     const QPointF &corrected = QPointF(-1.0, -1.0));
    void saveDiagnosticFrame(const QString &reason, bool force = false);
    void updateFrameSource(const QString &path);
    QRect pixelRect(const QRectF &normalized, const QSize &size) const;
    void resetTrackingState();
    bool runSyntheticPreflight(QStringList *details);
    void resetAutotestMetrics();
    void recordAutotestFrame(const MatchResult &result, double innovationPixels,
                             bool jumpBlocked);
    void finishAutotest(bool completed);
    void writeAutotestSummary(bool completed);
    void saveAutotestAnomaly(const QString &reason);

    QPointer<QWaylandSurface> surface_;
    QFutureWatcher<MatchResult> matchWatcher_;
    QImage referenceFrame_;
    QImage templateImage_;
    QImage lastFrame_;
    QRectF templateRect_;
    QRectF matchRect_;
    QPointF anchorOffset_;
    QPointF trackedCenter_;
    QPointF rawCenter_;
    QPointF velocity_;
    QUrl frameSource_;
    QString profileId_ = QStringLiteral("default");
    QString status_ = QStringLiteral("F2 — open the experimental centre finder");
    QString trackingState_ = QStringLiteral("IDLE");
    QString correctionState_;
    QString sessionDirectory_;
    QFile sessionLog_;
    QElapsedTimer captureTimer_;
    QElapsedTimer trackingTimer_;
    QElapsedTimer autotestElapsedTimer_;
    QTimer autotestTimer_;
    Stage stage_ = Idle;
    bool visible_ = false;
    bool tracking_ = false;
    bool grabInFlight_ = false;
    bool referenceCapturePending_ = false;
    bool nativeFallbackStarted_ = false;
    bool startNewSessionOnOpen_ = false;
    bool anchorConfigured_ = false;
    int contextWidth_ = 0;
    int contextHeight_ = 0;
    int frameNumber_ = 0;
    int contextGeneration_ = 0;
    int grabRequestId_ = 0;
    GrabPurpose pendingGrabPurpose_ = GrabPurpose::Reference;
    int trackingGeneration_ = 0;
    int lostFrames_ = 0;
    int diagnosticFrames_ = 0;
    int confirmFrames_ = 0;
    int identityMissingFrames_ = 0;
    int predictionFrames_ = 0;
    int blockedJumpFrames_ = 0;
    QRectF confirmRect_;
    double score_ = 0.0;
    double confidence_ = 0.0;
    double heroGradientScore_ = 0.0;
    double threshold_ = 0.68;
    double analysisFps_ = 0.0;
    bool autotestRunning_ = false;
    bool syntheticPreflightPassed_ = false;
    int autotestElapsedSeconds_ = 0;
    QString autotestResult_ = QStringLiteral("NOT RUN");
    QString autotestSummary_ = QStringLiteral("Автотест ещё не запускался.");
    int autotestLockedFrames_ = 0;
    int autotestPredictFrames_ = 0;
    int autotestLostFrames_ = 0;
    int autotestConfirmFrames_ = 0;
    int autotestLossEvents_ = 0;
    int autotestReacquisitions_ = 0;
    int autotestBlockedJumps_ = 0;
    int autotestAnomalyGroups_ = 0;
    int autotestAcceptedCriticalJumps_ = 0;
    int autotestIdentityMissingFrames_ = 0;
    int autotestMaximumReacquireMs_ = 0;
    int autotestCurrentLostSinceMs_ = -1;
    double autotestMaximumInnovationPixels_ = 0.0;
    qint64 autotestAnalysisMsTotal_ = 0;
    int autotestAnalysisSamples_ = 0;
    std::deque<QImage> recentAutotestFrames_;
};
