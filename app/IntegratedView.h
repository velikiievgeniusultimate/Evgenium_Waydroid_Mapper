#pragma once

#include "CenterVision.h"

#include <QObject>
#include <QEvent>
#include <QHash>
#include <QPointF>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRectF>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <array>
#include <functional>
#include <memory>
#include <vector>

class QQmlApplicationEngine;
class QProcess;
class QSettings;
class QWindow;
class QWaylandCompositor;
class QWaylandSurface;
class WaylandPointerConfiner;

class IntegratedView final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool configurationUnlocked READ configurationUnlocked NOTIFY configurationUnlockedChanged)
    Q_PROPERTY(bool windowVisible READ windowVisible NOTIFY windowVisibleChanged)
    Q_PROPERTY(bool editMode READ editMode NOTIFY editModeChanged)
    Q_PROPERTY(bool waitingForKey READ waitingForKey NOTIFY waitingForKeyChanged)
    Q_PROPERTY(QString editorMessage READ editorMessage NOTIFY editorMessageChanged)
    Q_PROPERTY(QVariantList bindings READ bindings NOTIFY bindingsChanged)
    Q_PROPERTY(int selectedBindingIndex READ selectedBindingIndex NOTIFY selectedBindingChanged)
    Q_PROPERTY(QVariantMap selectedBinding READ selectedBinding NOTIFY selectedBindingChanged)
    Q_PROPERTY(QVariantMap characterCenter READ characterCenter NOTIFY characterCenterChanged)
    Q_PROPERTY(QVariantMap mobaMovement READ mobaMovement NOTIFY mobaMovementChanged)
    Q_PROPERTY(QVariantMap skillCancel READ skillCancel NOTIFY skillCancelChanged)
    Q_PROPERTY(QVariantList mobaSkills READ mobaSkills NOTIFY mobaSkillsChanged)
    Q_PROPERTY(int selectedMobaSkillIndex READ selectedMobaSkillIndex NOTIFY selectedMobaSkillChanged)
    Q_PROPERTY(QVariantMap selectedMobaSkill READ selectedMobaSkill NOTIFY selectedMobaSkillChanged)
    Q_PROPERTY(bool hasCharacterCenter READ hasCharacterCenter NOTIFY characterCenterChanged)
    Q_PROPERTY(bool hasMobaMovement READ hasMobaMovement NOTIFY mobaMovementChanged)
    Q_PROPERTY(bool hasSkillCancel READ hasSkillCancel NOTIFY skillCancelChanged)
    Q_PROPERTY(bool hasMobaSkills READ hasMobaSkills NOTIFY mobaSkillsChanged)
    Q_PROPERTY(bool calibrationActive READ calibrationActive NOTIFY calibrationChanged)
    Q_PROPERTY(int calibrationStep READ calibrationStep NOTIFY calibrationChanged)
    Q_PROPERTY(int calibrationTotal READ calibrationTotal NOTIFY calibrationChanged)
    Q_PROPERTY(bool calibrationPointReady READ calibrationPointReady NOTIFY calibrationChanged)
    Q_PROPERTY(QString calibrationInstruction READ calibrationInstruction NOTIFY calibrationChanged)
    Q_PROPERTY(QVariantList calibrationPoints READ calibrationPoints NOTIFY calibrationChanged)
    Q_PROPERTY(bool earlyPredictionActive READ earlyPredictionActive NOTIFY earlyPredictionChanged)
    Q_PROPERTY(QVariantMap earlyPrediction READ earlyPrediction NOTIFY earlyPredictionChanged)
    Q_PROPERTY(QVariantList baggageItems READ baggageItems NOTIFY baggageChanged)
    Q_PROPERTY(int androidWidth READ androidWidth NOTIFY resolutionChanged)
    Q_PROPERTY(int androidHeight READ androidHeight NOTIFY resolutionChanged)
    Q_PROPERTY(QString activeProfileName READ activeProfileName NOTIFY profileChanged)
    Q_PROPERTY(int profileResolutionWidth READ profileResolutionWidth NOTIFY profileChanged)
    Q_PROPERTY(int profileResolutionHeight READ profileResolutionHeight NOTIFY profileChanged)
    Q_PROPERTY(bool profileResolutionCompatible READ profileResolutionCompatible NOTIFY profileChanged)
    Q_PROPERTY(QString profileResolutionWarning READ profileResolutionWarning NOTIFY profileChanged)
    Q_PROPERTY(bool profileManagerVisible READ profileManagerVisible NOTIFY profileManagerVisibleChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY profileChanged)
    Q_PROPERTY(QVariantMap pendingProfile READ pendingProfile NOTIFY pendingProfileChanged)
    Q_PROPERTY(bool cursorLocked READ cursorLocked NOTIFY cursorLockedChanged)
    Q_PROPERTY(bool syntheticTouchActive READ syntheticTouchActive NOTIFY syntheticTouchActiveChanged)
    Q_PROPERTY(CenterVision *centerVision READ centerVision CONSTANT)
public:
    explicit IntegratedView(QObject *parent = nullptr);
    ~IntegratedView() override;

    bool busy() const { return busy_; }
    bool ready() const { return ready_; }
    bool configurationUnlocked() const { return configurationUnlocked_; }
    bool windowVisible() const { return windowVisible_; }
    bool editMode() const { return editMode_; }
    bool waitingForKey() const { return waitingForKey_; }
    QString editorMessage() const { return editorMessage_; }
    QVariantList bindings() const;
    int selectedBindingIndex() const { return selectedBindingIndex_; }
    QVariantMap selectedBinding() const;
    QVariantMap characterCenter() const;
    QVariantMap mobaMovement() const;
    QVariantMap skillCancel() const;
    QVariantList mobaSkills() const;
    int selectedMobaSkillIndex() const { return selectedMobaSkillIndex_; }
    QVariantMap selectedMobaSkill() const;
    bool hasCharacterCenter() const { return characterCenter_.enabled; }
    bool hasMobaMovement() const { return mobaMovement_.enabled; }
    bool hasSkillCancel() const { return skillCancel_.enabled; }
    bool hasMobaSkills() const { return !mobaSkills_.empty(); }
    bool calibrationActive() const { return calibrationSkillIndex_ >= 0; }
    int calibrationStep() const { return calibrationStep_; }
    int calibrationTotal() const;
    bool calibrationPointReady() const { return calibrationPointReady_; }
    QString calibrationInstruction() const;
    QVariantList calibrationPoints() const;
    bool earlyPredictionActive() const { return earlyPredictionSkillIndex_ >= 0; }
    QVariantMap earlyPrediction() const;
    QVariantList baggageItems() const;
    int androidWidth() const { return androidWidth_; }
    int androidHeight() const { return androidHeight_; }
    QString activeProfileName() const { return activeProfileName_; }
    int profileResolutionWidth() const { return profileResolutionWidth_; }
    int profileResolutionHeight() const { return profileResolutionHeight_; }
    bool profileResolutionCompatible() const;
    QString profileResolutionWarning() const;
    bool profileManagerVisible() const { return profileManagerVisible_; }
    QVariantList profiles() const;
    QString activeProfileId() const { return activeProfileId_; }
    QVariantMap pendingProfile() const;
    bool cursorLocked() const { return cursorLocked_; }
    bool syntheticTouchActive() const { return !activeTapPoints_.isEmpty(); }
    CenterVision *centerVision() const { return centerVision_; }

public slots:
    void setDeviceProfile(const QString &profileId);
    void startAndOpen(int width, int height);
    void prepareAndStart(int width, int height);
    void stopIntegratedSession();
    void megaStopWaydroid();
    void openIntegratedWindow();
    void hideIntegratedWindow();
    void surfaceReady(QObject *surfaceObject);
    void toggleEditMode();
    void addTapAt(double normalizedX, double normalizedY);
    void addCharacterCenterAt(double normalizedX, double normalizedY);
    void moveCharacterCenter(double normalizedX, double normalizedY);
    void setCharacterCenterPosition(int pixelX, int pixelY);
    void addMobaMovementAt(double normalizedX, double normalizedY);
    void moveMobaMovement(double normalizedX, double normalizedY);
    void resizeMobaMovement(double normalizedRadius);
    void setMobaMovementPosition(int pixelX, int pixelY);
    void setMobaMovementHoldThreshold(int milliseconds);
    void setMobaMovementDistanceModifier(int percent);
    void addSkillCancelAt(double normalizedX, double normalizedY);
    void moveSkillCancel(double normalizedX, double normalizedY);
    void setSkillCancelPosition(int pixelX, int pixelY);
    void beginRebindSkillCancel();
    void removeSkillCancel();
    void addMobaSkillAt(double normalizedX, double normalizedY);
    void moveMobaSkill(int index, double normalizedX, double normalizedY);
    void resizeMobaSkill(int index, double normalizedRadius);
    void selectMobaSkill(int index);
    void setSelectedMobaSkillPosition(int pixelX, int pixelY);
    void setSelectedMobaSkillDiameter(int diameterPixels);
    void setSelectedMobaSkillMode(int mode);
    void setSelectedMobaSkillStartSpeedMs(int milliseconds);
    void setSelectedMobaSkillEarlyPredictionEnabled(bool enabled);
    void setSelectedMobaSkillEarlyPredictionStyle(int style);
    void setSelectedMobaSkillCancellable(bool enabled);
    void setSelectedMobaSkillCancelReaction(int level);
    void setSelectedMobaSkillArtificialCenterEnabled(bool enabled);
    void setSelectedMobaSkillArtificialCenterPosition(int pixelX, int pixelY);
    void moveMobaSkillArtificialCenter(int index, double normalizedX,
                                       double normalizedY);
    void acceptSelectedMobaSkillCalibration();
    void restoreSelectedMobaSkillCalibration();
    void beginRebindSelectedMobaSkill();
    void duplicateMobaSkill(int index);
    void removeMobaSkill(int index);
    void beginMobaSkillCalibration(int index, int calibrationVersion);
    void moveCalibrationCharacterCenter(double normalizedX, double normalizedY);
    void recordMobaSkillCalibrationPoint(double normalizedX, double normalizedY);
    void undoMobaSkillCalibrationPoint();
    void cancelMobaSkillCalibration();
    void storeControlInBaggage(const QString &type, int index,
                               const QString &name);
    void insertBaggageItem(const QString &itemId, double normalizedX,
                           double normalizedY);
    void deleteBaggageItem(const QString &itemId);
    void removeCharacterCenter();
    void removeMobaMovement();
    void moveBinding(int index, double normalizedX, double normalizedY);
    void selectBinding(int index);
    void setSelectedBindingPosition(int pixelX, int pixelY);
    void setSelectedBindingMode(int mode);
    void beginRebindSelected();
    void cancelKeyCapture(bool clickedOutside = false);
    void duplicateBinding(int index);
    void removeBinding(int index);
    void toggleProfileManager();
    void closeProfileManager();
    void createProfile();
    void duplicateProfile(const QString &profileId);
    void deleteProfile(const QString &profileId);
    void selectProfile(const QString &profileId);
    void renameProfile(const QString &profileId, const QString &name);
    void setProfileImage(const QString &profileId, const QUrl &sourceUrl);
    void adaptPendingProfileAutomatically();
    void createPendingProfileFromScratch();
    void cancelPendingProfileSwitch();
    void toggleCursorLock();

signals:
    void statusChanged(const QString &status);
    void operationFailed(const QString &status);
    void busyChanged();
    void readyChanged();
    void configurationUnlockedChanged();
    void windowVisibleChanged();
    void editModeChanged();
    void waitingForKeyChanged();
    void editorMessageChanged();
    void bindingsChanged();
    void selectedBindingChanged();
    void characterCenterChanged();
    void mobaMovementChanged();
    void skillCancelChanged();
    void mobaSkillsChanged();
    void selectedMobaSkillChanged();
    void calibrationChanged();
    void earlyPredictionChanged();
    void baggageChanged();
    void mobaSkillCalibrationCompleted(int index);
    void resolutionChanged();
    void profileChanged();
    void profileManagerVisibleChanged();
    void profilesChanged();
    void pendingProfileChanged();
    void profileAdaptationRequested();
    void cursorLockedChanged();
    void syntheticTouchActiveChanged();

private:
    struct MobaSkillControl;

    void ensureCompositor();
    void applyDeviceProfile(const std::function<void()> &completed);
    QString deviceProfileScriptPath() const;
    void settleMapperForStop();
    void verifyMegaStop(int attempt);
    void startSession(const QString &purpose, const std::function<void()> &completed);
    void launchSessionProcess(const QString &purpose,
                              const std::function<void()> &completed);
    void ensureContainerServiceRunning(const QString &purpose,
                                       const std::function<void()> &completed);
    void waitForContainerServiceRunning(const QString &purpose, int attempt,
                                        const std::function<void()> &completed);
    void handleSessionOutput(const QString &channel, const QString &output);
    void completeSessionStart(int generation);
    void handleSessionStartFailure(const QString &purpose, const QString &reason);
    void writeResolution(int width, int height);
    void requestSurface();
    void stopSession(const QString &purpose, const std::function<void()> &completed);
    void forceStopWaydroidRuntime(const QString &purpose,
                                  const std::function<void()> &completed);
    void waitForContainerManagerResponsive(const QString &purpose, int attempt,
                                           const std::function<void()> &completed);
    void forceStopContainerService(const QString &purpose,
                                   const std::function<void()> &completed);
    void waitForContainerServiceStopped(const QString &purpose, int attempt,
                                        bool sigkillIssued,
                                        const std::function<void()> &completed);
    void killLocalWaydroidLaunchers(const std::function<void()> &completed);
    void runCommand(const QStringList &arguments,
                    const std::function<void(int, const QString &)> &completed,
                    const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment(),
                    int timeoutMs = 30000);
    void runHostCommand(const QString &program, const QStringList &arguments,
                        const std::function<void(int, const QString &)> &completed,
                        int timeoutMs = 30000);
    void failOperation(const QString &status);
    void log(const QString &message) const;
    void setBusy(bool busy);
    void setReady(bool ready);
    void setConfigurationUnlocked(bool unlocked);
    void setWindowVisible(bool visible);
    void setEditMode(bool enabled);
    void setWaitingForKey(bool enabled);
    void setEditorMessage(const QString &message);
    void captureSelectedKey(int key);
    void triggerQuickTap(double normalizedX, double normalizedY);
    void beginHeldTap(int key, double normalizedX, double normalizedY);
    void endHeldTap(int key);
    int allocateTouchId();
    void trackTouch(int id, const QPointF &point);
    void updateTrackedTouch(int id, const QPointF &point);
    void forgetTouch(int id);
    void clearTrackedTouches();
    bool sendTouchPoint(int id, const QPointF &normalized,
                        Qt::TouchPointState state);
    void releaseAllTapTouches();
    QWaylandCompositor *waylandCompositor() const;
    QWindow *integratedWindow() const;
    bool windowToNormalized(QWindow *target, const QPointF &local,
                            QPointF *normalized, bool clampToSurface = false) const;
    QRectF androidSurfaceRect(QWindow *target) const;
    void setCursorLocked(bool locked);
    void updateCursorConfinement(QWindow *target);
    void beginMobaMovement(const QPointF &pointer);
    void updateMobaMovement(const QPointF &pointer);
    void endMobaMovement();
    void beginMobaMovementPress(const QPointF &pointer);
    void updateMobaMovementPress(const QPointF &pointer);
    void finishMobaMovementPress(const QPointF &pointer);
    void startMobaAutoMovement(const QPointF &pointer);
    void cancelMobaMovementGesture();
    void beginMobaSkill(int index, const QPointF &pointer);
    void beginEarlyPrediction(int index, const QPointF &pointer);
    void updateEarlyPrediction(const QPointF &pointer);
    void finishEarlyPrediction(int index);
    void cancelEarlyPrediction();
    void castEarlyPrediction(int index, const QPointF &pointer);
    void updateMobaSkills(const QPointF &pointer);
    void endMobaSkill(int index);
    void releaseMobaSkillNow(int index, bool cancelled = false);
    void cancelActiveMobaSkills();
    void animateMobaSkillCancellation(int index, int touchId,
                                      const QPointF &from, const QPointF &to,
                                      int gestureGeneration, int frame,
                                      int totalFrames, int intervalMs);
    void releaseAllMobaSkillTouches();
    QPointF mobaSkillTouchForPointer(int index, const QPointF &pointer) const;
    QPointF mobaSkillVectorForPointer(int index, const QPointF &pointer) const;
    QPointF legacyMobaSkillVectorForPointer(int index,
                                            const QPointF &pointer) const;
    QPointF megaMobaSkillVectorForPointer(int index,
                                          const QPointF &pointer) const;
    QPointF directionalMobaSkillVectorForPointer(int index,
                                                 const QPointF &pointer) const;
    QPointF calibrationVector(int step) const;
    QPointF directionalCalibrationVector(int step) const;
    QPointF legacyCalibrationVector(int step) const;
    bool megaCalibrationStep(int step, int *ring, int *direction) const;
    bool isSkillCalibrated(const MobaSkillControl &skill) const;
    int expectedCalibrationCount(const MobaSkillControl &skill) const;
    QPointF safeCalibrationTouch(const QPointF &point) const;
    void startCalibrationTouch();
    void moveCalibrationTouch();
    void animateCalibrationTouch(const QPointF &from, const QPointF &to,
                                 int durationMs, int generation,
                                 const std::function<void()> &completed);
    void finishMobaSkillCalibration();
    void markMobaSkillCalibrationStale(MobaSkillControl &skill,
                                       const QString &reason);
    void markAllMobaSkillCalibrationsStale(const QString &reason);
    void loadBindings();
    void saveBindings() const;
    void loadBaggage();
    void saveBaggage() const;
    void loadControls(QSettings &settings);
    void saveControls(QSettings &settings) const;
    void clearControls();
    void emitAllControlsChanged();
    QString resolutionKey(int width, int height) const;
    bool parseResolutionKey(const QString &key, int *width, int *height) const;
    struct MapperProfile;
    MapperProfile *findProfile(const QString &profileId);
    const MapperProfile *findProfile(const QString &profileId) const;
    bool profileSupportsResolution(const MapperProfile &profile,
                                   int width, int height) const;
    QString closestProfileResolution(const MapperProfile &profile,
                                     int width, int height) const;
    bool loadProfileVariant(const QString &profileId, const QString &variantKey);
    void saveProfileMetadata(const MapperProfile &profile) const;
    void activateProfileVariant(MapperProfile &profile, const QString &variantKey,
                                bool persistVariant);
    void setProfileManagerVisible(bool visible);
    QString keyName(int key) const;
    bool eventFilter(QObject *watched, QEvent *event) override;
    QProcessEnvironment nestedEnvironment() const;

    struct TapBinding {
        enum Mode {
            Quick = 0,
            HoldUntilKeyRelease = 1
        };
        double x = 0.0;
        double y = 0.0;
        int key = 0;
        Mode mode = HoldUntilKeyRelease;
    };

    struct PositionControl {
        bool enabled = false;
        double x = 0.5;
        double y = 0.5;
    };

    struct MobaMovementControl {
        bool enabled = false;
        double x = 0.18;
        double y = 0.78;
        // Radius as a fraction of the shorter Android screen side.
        double radius = 0.09;
        int holdThresholdMs = 120;
        double clickDistanceModifier = 1.0;
    };

    struct SkillCancelControl {
        bool enabled = false;
        double x = 0.88;
        double y = 0.18;
        int key = 0;
    };

    struct MobaSkillControl {
        enum Mode {
            FollowCursorReleaseToCast = 0
        };
        double x = 0.82;
        double y = 0.76;
        // Radius as a fraction of the shorter Android screen side.
        double radius = 0.055;
        int key = 0;
        Mode mode = FollowCursorReleaseToCast;
        // Total centre-to-target travel time in milliseconds.
        int startSpeedMs = 10;
        // Optional host-side preview shown before the Android gesture exists.
        // Style 0 is the translucent linear stick.
        bool earlyPredictionEnabled = false;
        int earlyPredictionStyle = 0;
        // Cancellation is opt-out per skill. Reaction level controls the
        // duration of the existing finger's MOVE into the cancel target.
        bool cancellable = true;
        int cancelReactionLevel = 3;
        // Optional physical button location. The finger presses here first,
        // then moves to x/y (the real virtual joystick centre) before aiming.
        bool artificialCenterEnabled = false;
        double artificialX = 0.82;
        double artificialY = 0.76;
        // 1 = legacy 8 x 3 triangular grid, 2 = MEGA polar contour grid.
        int calibrationVersion = 0;
        std::vector<QPointF> calibrationPoints;
        bool calibrationStale = false;
        bool recoveryValid = false;
        double recoveryX = 0.82;
        double recoveryY = 0.76;
        double recoveryRadius = 0.055;
        bool recoveryArtificialCenterEnabled = false;
        double recoveryArtificialX = 0.82;
        double recoveryArtificialY = 0.76;
        bool recoveryCharacterCenterEnabled = false;
        double recoveryCharacterCenterX = 0.5;
        double recoveryCharacterCenterY = 0.5;
        int recoveryCalibrationVersion = 0;
        std::vector<QPointF> recoveryCalibrationPoints;
    };

    struct BaggageItem {
        enum Kind {
            Tap = 0,
            CharacterCenter = 1,
            MobaMovement = 2,
            MobaSkill = 3,
            SkillCancel = 4
        };
        QString id;
        QString name;
        Kind kind = Tap;
        int sourceWidth = 0;
        int sourceHeight = 0;
        TapBinding tap;
        PositionControl characterCenter;
        MobaMovementControl movement;
        MobaSkillControl skill;
        SkillCancelControl cancel;
    };

    struct MapperProfile {
        QString id;
        QString name;
        QString imagePath;
        bool isDefault = false;
        int order = 0;
        QStringList resolutions;
    };

    enum class KeyCaptureTarget {
        None,
        TapBinding,
        MobaSkill,
        SkillCancel
    };

    static constexpr int CalibrationDirections = 8;
    static constexpr int CalibrationRings = 3;
    static constexpr int CalibrationSampleCount = CalibrationDirections * CalibrationRings;
    static constexpr int MegaCalibrationVersion = 2;
    static constexpr int MegaCalibrationRingCount = 6;
    static constexpr std::array<int, MegaCalibrationRingCount>
        MegaCalibrationDirections = {16, 14, 12, 10, 8, 6};
    static constexpr std::array<double, MegaCalibrationRingCount>
        MegaCalibrationRadii = {1.0, 0.82, 0.64, 0.46, 0.28, 0.12};
    static constexpr int MegaCalibrationSampleCount = 66;
    static constexpr int DirectionalCalibrationVersion = 3;
    static constexpr int DirectionalCalibrationSampleCount = 64;
    static constexpr double DirectionalCenterDeadzonePixels = 3.0;

    QQmlApplicationEngine *engine_ = nullptr;
    CenterVision *centerVision_ = nullptr;
    QProcess *sessionProcess_ = nullptr;
    int lifecycleGeneration_ = 0;
    int sessionStartGeneration_ = 0;
    int sessionRecoveryAttempts_ = 0;
    bool sessionStartPending_ = false;
    QString sessionOutputBuffer_;
    QString sessionStartPurpose_;
    std::function<void()> sessionStartCompleted_;
    QPointer<QWaylandSurface> inputSurface_;
    bool busy_ = false;
    bool ready_ = false;
    bool configurationUnlocked_ = false;
    bool windowVisible_ = false;
    bool waitingForSurface_ = false;
    bool startAfterStop_ = false;
    bool autoOpenWhenReady_ = false;
    bool megaStopInProgress_ = false;
    int pendingStartWidth_ = 1920;
    int pendingStartHeight_ = 1080;
    bool editMode_ = false;
    bool waitingForKey_ = false;
    bool clearBindingOnCancel_ = false;
    QString editorMessage_ = "F5 — open mapper editor";
    std::vector<TapBinding> bindings_;
    std::vector<TapBinding> editSnapshot_;
    PositionControl characterCenter_;
    PositionControl characterCenterSnapshot_;
    MobaMovementControl mobaMovement_;
    MobaMovementControl mobaMovementSnapshot_;
    SkillCancelControl skillCancel_;
    SkillCancelControl skillCancelSnapshot_;
    std::vector<MobaSkillControl> mobaSkills_;
    std::vector<MobaSkillControl> mobaSkillsSnapshot_;
    bool mobaMovementActive_ = false;
    bool mobaMovementPressPending_ = false;
    bool mobaMovementHoldActive_ = false;
    bool mobaMovementAutoActive_ = false;
    int mobaMovementGestureGeneration_ = 0;
    int mobaMovementTouchId_ = -1;
    QPointF mobaLastPointer_;
    QPointF mobaLastTouch_;
    QHash<int, QPointF> activeTapPoints_;
    QHash<int, int> quickTapGenerations_;
    int nextQuickTapGeneration_ = 0;
    QHash<int, int> heldTapIdsByKey_;
    QHash<int, int> activeMobaSkillTouchIds_;
    QHash<int, int> mobaSkillGestureGenerations_;
    QHash<int, QPointF> mobaSkillPointers_;
    int earlyPredictionSkillIndex_ = -1;
    QPointF earlyPredictionPointer_;
    mutable QHash<int, QPointF> mobaSkillLastDirectionalVectors_;
    QSet<int> armingMobaSkills_;
    QSet<int> pendingMobaSkillReleases_;
    QSet<int> cancellingMobaSkills_;
    int nextMobaSkillGestureGeneration_ = 0;
    int selectedBindingIndex_ = -1;
    int selectedMobaSkillIndex_ = -1;
    KeyCaptureTarget keyCaptureTarget_ = KeyCaptureTarget::None;
    int calibrationSkillIndex_ = -1;
    int calibrationStep_ = 0;
    int calibrationTouchId_ = -1;
    bool calibrationPointReady_ = false;
    int calibrationMotionGeneration_ = 0;
    QPointF calibrationLastTouch_;
    MobaSkillControl calibrationBackupSkill_;
    PositionControl calibrationBackupCharacterCenter_;
    bool hasCalibrationBackupSkill_ = false;
    std::vector<BaggageItem> baggageItems_;
    QString activeProfileId_ = "default";
    QString activeProfileName_ = "Default";
    int profileResolutionWidth_ = 0;
    int profileResolutionHeight_ = 0;
    std::vector<MapperProfile> profiles_;
    bool profileManagerVisible_ = false;
    QString pendingProfileId_;
    int pendingProfileSourceWidth_ = 0;
    int pendingProfileSourceHeight_ = 0;
    bool cursorLocked_ = false;
    std::unique_ptr<WaylandPointerConfiner> pointerConfiner_;
    QString deviceProfile_ = "native";
    bool deviceProfileDirty_ = false;
    int androidWidth_ = 1920;
    int androidHeight_ = 1080;
};
