#pragma once

#include <QObject>
#include <QString>
#include <functional>

class QProcess;
class QTimer;

class DiagnosticsCollector final : public QObject
{
    Q_OBJECT
public:
    explicit DiagnosticsCollector(QObject *parent = nullptr);

    bool running() const { return running_; }
    QString lastPath() const { return lastPath_; }

public slots:
    void collect();

signals:
    void runningChanged(bool running);
    void progressChanged(const QString &message);
    void finished(const QString &path, bool privilegedDataCollected,
                  const QString &message);

private:
    void startUserCollection();
    void startRootCollection();
    void runSection(const QString &name, const QString &program,
                    const QStringList &arguments, int timeoutMs,
                    const std::function<void(int, const QString &)> &completed);
    void appendSection(const QString &title, const QString &text);
    void finishCollection(bool privilegedDataCollected, const QString &message);

    bool running_ = false;
    QString lastPath_;
    QProcess *activeProcess_ = nullptr;
    QTimer *activeTimeout_ = nullptr;
};
