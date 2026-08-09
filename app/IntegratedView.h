#pragma once

#include <QObject>
#include <QEvent>
#include <QHash>
#include <QPointF>
#include <QPointer>
#include <QProcessEnvironment>
#include <QStringList>
#include <QVariantList>
#include <functional>
#include <vector>

class QQmlApplicationEngine;
class QProcess;
class QWindow;
class QWaylandCompositor;
class QWaylandSurface;

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
    Q_PROPERTY(QVariantList mobaSkills READ mobaSkills NOTIFY mobaSkillsChanged)
    Q_PROPERTY(int selectedMobaSkillIndex READ selectedMobaSkillIndex NOTIFY selectedMobaSkillChanged)
    Q_PROPERTY(QVariantMap selectedMobaSkill READ selectedMobaSkill NOTIFY selectedMobaSkillChanged)
    Q_PROPERTY(bool hasCharacterCenter READ hasCharacterCenter NOTIFY characterCenterChanged)
    Q_PROPERTY(bool hasMobaMovement READ hasMobaMovement NOTIFY mobaMovementChanged)
    Q_PROPERTY(bool hasMobaSkills READ hasMobaSkills NOTIFY mobaSkillsChanged)
    Q_PROPERTY(bool calibrationActive READ calibrationActive NOTIFY calibrationChanged)
    Q_PROPERTY(int calibrationStep READ calibrationStep NOTIFY calibrationChanged)
    Q_PROPERTY(int calibrationTotal READ calibrationTotal CONSTANT)
    Q_PROPERTY(bool calibrationPointReady READ calibrationPointReady NOTIFY calibrationChanged)
    Q_PROPERTY(QString calibrationInstruction READ calibrationInstruction NOTIFY calibrationChanged)
    Q_PROPERTY(QVariantList calibrationPoints READ calibrationPoints NOTIFY calibrationChanged)
    Q_PROPERTY(int androidWidth READ androidWidth NOTIFY resolutionChanged)
    Q_PROPERTY(int androidHeight READ androidHeight NOTIFY resolutionChanged)
public:
    explicit IntegratedView(QObject *parent = nullptr);

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
    QVariantList mobaSkills() const;
    int selectedMobaSkillIndex() const { return selectedMobaSkillIndex_; }
    QVariantMap selectedMobaSkill() const;
    bool hasCharacterCenter() const { return characterCenter_.enabled; }
    bool hasMobaMovement() const { return mobaMovement_.enabled; }
    bool hasMobaSkills() const { return !mobaSkills_.empty(); }
    bool calibrationActive() const { return calibrationSkillIndex_ >= 0; }
    int calibrationStep() const { return calibrationStep_; }
    int calibrationTotal() const { return CalibrationSampleCount; }
    bool calibrationPointReady() const { return calibrationPointReady_; }
    QString calibrationInstruction() const;
    QVariantList calibrationPoints() const;
    int androidWidth() const { return androidWidth_; }
    int androidHeight() const { return androidHeight_; }

public slots:
    void prepareAndStart(int width, int height);
    void stopIntegratedSession();
    void openIntegratedWindow();
    void hideIntegratedWindow();
    void surfaceReady(QObject *surfaceObject);
    void toggleEditMode();
    void addTapAt(double normalizedX, double normalizedY);
    void addCharacterCenterAt(double normalizedX, double normalizedY);
    void moveCharacterCenter(double normalizedX, double normalizedY);
    void addMobaMovementAt(double normalizedX, double normalizedY);
    void moveMobaMovement(double normalizedX, double normalizedY);
    void resizeMobaMovement(double normalizedRadius);
    void addMobaSkillAt(double normalizedX, double normalizedY);
    void moveMobaSkill(int index, double normalizedX, double normalizedY);
    void resizeMobaSkill(int index, double normalizedRadius);
    void selectMobaSkill(int index);
    void setSelectedMobaSkillPosition(int pixelX, int pixelY);
    void setSelectedMobaSkillDiameter(int diameterPixels);
    void setSelectedMobaSkillMode(int mode);
    void beginRebindSelectedMobaSkill();
    void removeMobaSkill(int index);
    void beginMobaSkillCalibration(int index);
    void recordMobaSkillCalibrationPoint(double normalizedX, double normalizedY);
    void undoMobaSkillCalibrationPoint();
    void cancelMobaSkillCalibration();
    void removeCharacterCenter();
    void removeMobaMovement();
    void moveBinding(int index, double normalizedX, double normalizedY);
    void selectBinding(int index);
    void setSelectedBindingPosition(int pixelX, int pixelY);
    void setSelectedBindingMode(int mode);
    void beginRebindSelected();
    void removeBinding(int index);

signals:
    void statusChanged(const QString &status);
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
    void mobaSkillsChanged();
    void selectedMobaSkillChanged();
    void calibrationChanged();
    void mobaSkillCalibrationCompleted(int index);
    void resolutionChanged();

private:
    void ensureCompositor();
    void startSession(const QString &purpose, const std::function<void()> &completed);
    void writeResolution(int width, int height);
    void requestSurface();
    void stopSession(const QString &purpose, const std::function<void()> &completed);
    void runCommand(const QStringList &arguments,
                    const std::function<void(int, const QString &)> &completed,
                    const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment());
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
    bool sendTouchPoint(int id, const QPointF &normalized,
                        Qt::TouchPointState state);
    void releaseAllTapTouches();
    QWaylandCompositor *waylandCompositor() const;
    QWindow *integratedWindow() const;
    bool windowToNormalized(QWindow *target, const QPointF &local,
                            QPointF *normalized, bool clampToSurface = false) const;
    void beginMobaMovement(const QPointF &pointer);
    void updateMobaMovement(const QPointF &pointer);
    void endMobaMovement();
    void beginMobaSkill(int index, const QPointF &pointer);
    void updateMobaSkills(const QPointF &pointer);
    void endMobaSkill(int index);
    void releaseAllMobaSkillTouches();
    QPointF mobaSkillVectorForPointer(int index, const QPointF &pointer) const;
    QPointF calibrationVector(int step) const;
    void startCalibrationTouch();
    void moveCalibrationTouch();
    void animateCalibrationTouch(const QPointF &from, const QPointF &to,
                                 int durationMs, int generation,
                                 const std::function<void()> &completed);
    void finishMobaSkillCalibration();
    void invalidateMobaSkillCalibrations(const QString &reason);
    void loadBindings();
    void saveBindings() const;
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
        Mode mode = Quick;
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
        std::vector<QPointF> calibrationPoints;
    };

    enum class KeyCaptureTarget {
        None,
        TapBinding,
        MobaSkill
    };

    static constexpr int CalibrationDirections = 8;
    static constexpr int CalibrationRings = 3;
    static constexpr int CalibrationSampleCount = CalibrationDirections * CalibrationRings;

    QQmlApplicationEngine *engine_ = nullptr;
    QProcess *sessionProcess_ = nullptr;
    QPointer<QWaylandSurface> inputSurface_;
    bool busy_ = false;
    bool ready_ = false;
    bool configurationUnlocked_ = false;
    bool windowVisible_ = false;
    bool waitingForSurface_ = false;
    bool editMode_ = false;
    bool waitingForKey_ = false;
    QString editorMessage_ = "F5 — open mapper editor";
    std::vector<TapBinding> bindings_;
    std::vector<TapBinding> editSnapshot_;
    PositionControl characterCenter_;
    PositionControl characterCenterSnapshot_;
    MobaMovementControl mobaMovement_;
    MobaMovementControl mobaMovementSnapshot_;
    std::vector<MobaSkillControl> mobaSkills_;
    std::vector<MobaSkillControl> mobaSkillsSnapshot_;
    bool mobaMovementActive_ = false;
    QPointF mobaLastPointer_;
    QPointF mobaLastTouch_;
    QHash<int, QPointF> activeTapPoints_;
    QHash<int, int> heldTapIdsByKey_;
    QHash<int, int> activeMobaSkillTouchIds_;
    int nextTouchId_ = 1;
    int selectedBindingIndex_ = -1;
    int selectedMobaSkillIndex_ = -1;
    KeyCaptureTarget keyCaptureTarget_ = KeyCaptureTarget::None;
    int calibrationSkillIndex_ = -1;
    int calibrationStep_ = 0;
    int calibrationTouchId_ = -1;
    bool calibrationPointReady_ = false;
    int calibrationMotionGeneration_ = 0;
    QPointF calibrationLastTouch_;
    std::vector<QPointF> calibrationBackupPoints_;
    int androidWidth_ = 1920;
    int androidHeight_ = 1080;
};
