#pragma once
#include <QMainWindow>
#include <QStringList>

class QComboBox;
class QLabel;
class MapperOverlay;
class IntegratedView;
class QPushButton;
class QSpinBox;
class QToolButton;
class DiagnosticsCollector;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void prepareWaydroid();
    void stopWaydroid();
    void showIntegratedWaydroid();
    void showOverlay();
    void handleCapturedKey(int key, bool pressed);
    void updateControls();
    void saveResolutionSelection();
    void toggleFavoriteResolution();
    void applyFavoriteResolution(int index);
    void collectDiagnostics();

private:
    void setStatus(const QString &text, bool healthy);
    void refreshFavoriteControls();
    QString currentResolutionKey() const;

    QLabel *statusLabel_ = nullptr;
    QLabel *eventLabel_ = nullptr;
    QSpinBox *widthBox_ = nullptr;
    QSpinBox *heightBox_ = nullptr;
    QToolButton *favoriteButton_ = nullptr;
    QComboBox *favoriteBox_ = nullptr;
    QPushButton *prepareButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QPushButton *integratedButton_ = nullptr;
    QPushButton *diagnosticsButton_ = nullptr;
    QLabel *diagnosticsLabel_ = nullptr;
    MapperOverlay *overlay_ = nullptr;
    IntegratedView *integratedView_ = nullptr;
    DiagnosticsCollector *diagnostics_ = nullptr;
    QStringList favoriteResolutions_;
    bool diagnosticsAutoStarted_ = false;
};
