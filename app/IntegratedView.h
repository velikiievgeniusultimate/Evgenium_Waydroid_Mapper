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
    Q_PROPERTY(bool windowVisible READ windowVisible NOTIFY windowVisibleChanged)
public:
    explicit IntegratedView(QObject *parent = nullptr);

    bool busy() const { return busy_; }
    bool ready() const { return ready_; }
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
    void windowVisibleChanged();

private:
    void ensureCompositor();
    void writeResolution(int width, int height);
    void startSession(const std::function<void()> &completed);
    void requestSurface();
    void stopSession(const std::function<void()> &completed);
    void runCommand(const QStringList &arguments,
                    const std::function<void(int, const QString &)> &completed,
                    const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment());
    void waitForRunning(int attemptsLeft, const std::function<void()> &completed);
    void waitForStopped(int attemptsLeft, int confirmations,
                        const std::function<void()> &completed);
    void failOperation(const QString &status);
    void setBusy(bool busy);
    void setReady(bool ready);
    void setWindowVisible(bool visible);
    QProcessEnvironment nestedEnvironment() const;

    QQmlApplicationEngine *engine_ = nullptr;
    QProcess *sessionProcess_ = nullptr;
    bool busy_ = false;
    bool ready_ = false;
    bool windowVisible_ = false;
    bool waitingForSurface_ = false;
};
