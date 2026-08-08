#pragma once

#include <QObject>
#include <QProcessEnvironment>
#include <QStringList>
#include <QVariantList>
#include <functional>
#include <vector>

class QQmlApplicationEngine;
class QProcess;

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
    int androidWidth() const { return androidWidth_; }
    int androidHeight() const { return androidHeight_; }

public slots:
    void prepareAndStart(int width, int height);
    void stopIntegratedSession();
    void openIntegratedWindow();
    void hideIntegratedWindow();
    void surfaceReady();
    void toggleEditMode();
    void addTapAt(double normalizedX, double normalizedY);
    void moveBinding(int index, double normalizedX, double normalizedY);
    void selectBinding(int index);
    void setSelectedBindingPosition(int pixelX, int pixelY);
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
    void captureBindingKey(int key);
    void injectTap(double normalizedX, double normalizedY);
    void loadBindings();
    void saveBindings() const;
    QString keyName(int key) const;
    bool eventFilter(QObject *watched, QEvent *event) override;
    QProcessEnvironment nestedEnvironment() const;

    struct TapBinding {
        double x = 0.0;
        double y = 0.0;
        int key = 0;
    };

    QQmlApplicationEngine *engine_ = nullptr;
    QProcess *sessionProcess_ = nullptr;
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
    int selectedBindingIndex_ = -1;
    int androidWidth_ = 1920;
    int androidHeight_ = 1080;
};
