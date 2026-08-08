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
public:
    explicit IntegratedView(QObject *parent = nullptr);
    ~IntegratedView() override;

    void showAndStart();
    bool busy() const { return busy_; }

public slots:
    void restartAndroid();
    void stopIntegratedSession();
    void applyResolution(int width, int height);
    void surfaceReady();

signals:
    void statusChanged(const QString &status);
    void busyChanged();

private:
    void ensureWindow();
    void startSession(const std::function<void()> &completed);
    void showFullUi();
    void stopSession(const std::function<void()> &completed);
    void runCommand(const QStringList &arguments,
                    const std::function<void(int, const QString &)> &completed,
                    const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment());
    void waitForRunning(int attemptsLeft, const std::function<void()> &completed);
    void waitForStopped(int attemptsLeft, const std::function<void()> &completed);
    void finishOperation(const QString &status);
    void failOperation(const QString &status);
    void setBusy(bool busy);
    QProcessEnvironment nestedEnvironment() const;

    QQmlApplicationEngine *engine_ = nullptr;
    QProcess *sessionProcess_ = nullptr;
    bool busy_ = false;
    bool waitingForSurface_ = false;
};
