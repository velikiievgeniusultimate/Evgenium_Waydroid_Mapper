#pragma once
#include <QWidget>

class MapperOverlay final : public QWidget
{
    Q_OBJECT
public:
    explicit MapperOverlay(QWidget *parent = nullptr);
signals:
    void keyCaptured(int key, bool pressed);
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
};

