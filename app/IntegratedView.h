#pragma once

#include <QObject>
#include <QProcessEnvironment>
#include <QStringList>
#include <functional>

class QQmlApplicationEngine;
class QProcess;

class IntegratedView final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool configurationUnlocked READ configurationUnlocked NOTIFY configurationUnlockedChanged)
    Q_PROPERTY(bool windowVisible READ windowVisible NOTIFY windowVisibleChanged)
public:
    explicit IntegratedView(QObject *parent = nullptr);

    bool busy() const { return busy_; }
    bool ready() const { return ready_; }
    bool configurationUnlocked() const { return configurationUnlocked_; }
    bool windowVisible() const { return windowVisible_; }

public slots:
    void prepareAndStart(int width, int height);
    void stopIntegratedSession();
    void openIntegratedWindow();
    void hideIntegratedWindow();
    void surfaceReady();

signals:
    void statusChanged(const QString &status);
    void busyChanged();
    void readyChanged();
    void configurationUnlockedChanged();
    void windowVisibleChanged();

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
    QProcessEnvironment nestedEnvironment() const;

    QQmlApplicationEngine *engine_ = nullptr;
    QProcess *sessionProcess_ = nullptr;
    bool busy_ = false;
    bool ready_ = false;
    bool configurationUnlocked_ = false;
    bool windowVisible_ = false;
    bool waitingForSurface_ = false;
};
