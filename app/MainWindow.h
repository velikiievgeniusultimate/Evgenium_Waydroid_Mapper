#pragma once
#include <QMainWindow>

class QLabel;
class MapperOverlay;
class QProcess;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
private slots:
    void refreshWaydroidStatus();
    void showOverlay();
    void handleCapturedKey(int key, bool pressed);
private:
    void setStatus(const QString &text, bool healthy);
    QLabel *statusLabel_ = nullptr;
    QLabel *eventLabel_ = nullptr;
    QProcess *statusProcess_ = nullptr;
    MapperOverlay *overlay_ = nullptr;
};

