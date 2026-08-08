#pragma once
#include <QMainWindow>

class QLabel;
class MapperOverlay;
class IntegratedView;
class QPushButton;
class QSpinBox;

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

private:
    void setStatus(const QString &text, bool healthy);

    QLabel *statusLabel_ = nullptr;
    QLabel *eventLabel_ = nullptr;
    QSpinBox *widthBox_ = nullptr;
    QSpinBox *heightBox_ = nullptr;
    QPushButton *prepareButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QPushButton *integratedButton_ = nullptr;
    MapperOverlay *overlay_ = nullptr;
    IntegratedView *integratedView_ = nullptr;
};
