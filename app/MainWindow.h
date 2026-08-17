#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

class QAction;
class QActionGroup;
class QComboBox;
class QLabel;
class QMenu;
class IntegratedView;
class QPushButton;
class QProcess;
class QSpinBox;
class QToolButton;
class QWidget;
class DiagnosticsCollector;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void startWaydroid();
    void requestResolutionChange();
    void megaStopWaydroid();
    void updateControls();
    void saveResolutionSelection();
    void toggleFavoriteResolution();
    void applyFavoriteResolution(int index);
    void collectDiagnostics();
    void startUpdate();
    void selectDeviceProfile(QAction *action);

private:
    void setStatus(const QString &text, bool healthy);
    void setActivity(const QString &text, bool visible = true);
    void refreshFavoriteControls();
    QString currentResolutionKey() const;
    QString updaterScriptPath() const;

    QLabel *statusLabel_ = nullptr;
    QLabel *activityLabel_ = nullptr;
    QWidget *resolutionPanel_ = nullptr;
    QSpinBox *widthBox_ = nullptr;
    QSpinBox *heightBox_ = nullptr;
    QToolButton *favoriteButton_ = nullptr;
    QComboBox *favoriteBox_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *changeResolutionButton_ = nullptr;
    QPushButton *megaStopButton_ = nullptr;
    QToolButton *settingsButton_ = nullptr;
    QMenu *deviceProfileMenu_ = nullptr;
    QActionGroup *deviceProfileGroup_ = nullptr;
    QAction *diagnosticsAction_ = nullptr;
    QAction *updateAction_ = nullptr;
    IntegratedView *integratedView_ = nullptr;
    DiagnosticsCollector *diagnostics_ = nullptr;
    QProcess *updateProcess_ = nullptr;
    QString updateOutput_;
    QStringList favoriteResolutions_;
    bool diagnosticsAutoStarted_ = false;
    bool megaStopRequested_ = false;
};
