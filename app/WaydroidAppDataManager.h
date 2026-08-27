#pragma once

#include <QObject>
#include <QString>

class IntegratedView;
class QProcess;
class QProgressDialog;
class QWidget;

class WaydroidAppDataManager final : public QObject
{
    Q_OBJECT
public:
    explicit WaydroidAppDataManager(QWidget *mainWindow);
    void attachToSettingsMenu();

private:
    void openCopyDialog();
    void startCopy(const QString &source, const QString &target,
                   const QString &packageName);
    QString helperPath() const;
    bool mapperBusy() const;

    QWidget *mainWindow_ = nullptr;
    IntegratedView *integratedView_ = nullptr;
    QProcess *copyProcess_ = nullptr;
    QProgressDialog *progressDialog_ = nullptr;
    QString copyOutput_;
};
