#include "MapperOverlay.h"
#include <QKeyEvent>
#include <QPainter>

MapperOverlay::MapperOverlay(QWidget *parent) : QWidget(parent)
{
    setWindowTitle("Evgenium input overlay");
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
}

void MapperOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return;
    }
    if (!event->isAutoRepeat())
        emit keyCaptured(event->key(), true);
    event->accept();
}

void MapperOverlay::keyReleaseEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat())
        emit keyCaptured(event->key(), false);
    event->accept();
}

void MapperOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(8, 12, 18, 120));
    painter.setPen(Qt::white);
    QFont heading = painter.font();
    heading.setPointSize(22);
    heading.setBold(true);
    painter.setFont(heading);
    painter.drawText(rect().adjusted(40, 40, -40, -40),
                     Qt::AlignTop | Qt::AlignHCenter, "Input overlay is active");
    QFont body = painter.font();
    body.setPointSize(13);
    body.setBold(false);
    painter.setFont(body);
    painter.drawText(rect(), Qt::AlignCenter,
                     "Press and release any key.\nPress Esc to return to the control window.");
}

