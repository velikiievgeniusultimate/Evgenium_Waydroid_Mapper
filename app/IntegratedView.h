#pragma once

#include <QObject>
#include <QProcessEnvironment>

class QQmlApplicationEngine;
class QProcess;

class IntegratedView final : public QObject
{
    Q_OBJECT
public:
    explicit IntegratedView(QObject *parent = nullptr);
    ~IntegratedView() override;

    void showAndStart();

public slots:
    void restartAndroid();
    void stopIntegratedSession();

signals:
    void statusChanged(const QString &status);

private:
    void ensureWindow();
    void startSession();
    void showFullUi();
    QProcessEnvironment nestedEnvironment() const;

    QQmlApplicationEngine *engine_ = nullptr;
    QProcess *process_ = nullptr;
    bool stoppingForRestart_ = false;
};

