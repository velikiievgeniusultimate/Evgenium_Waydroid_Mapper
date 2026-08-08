#include "MainWindow.h"
#include "MapperOverlay.h"
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), statusProcess_(new QProcess(this)), overlay_(new MapperOverlay())
{
    setWindowTitle("Evgenium Waydroid Mapper");
    resize(560, 330);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(14);
    auto *title = new QLabel("Evgenium Waydroid Mapper", central);
    QFont titleFont = title->font();
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    title->setFont(titleFont);
    auto *description = new QLabel(
        "First native prototype: detect Waydroid and verify that a fullscreen overlay "
        "can own keyboard input before Android touch injection is connected.", central);
    description->setWordWrap(true);
    statusLabel_ = new QLabel("Waydroid status has not been checked yet.", central);
    statusLabel_->setWordWrap(true);
    eventLabel_ = new QLabel("Last input event: none", central);
    auto *refreshButton = new QPushButton("Refresh Waydroid status", central);
    auto *overlayButton = new QPushButton("Open input overlay", central);
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addSpacing(8);
    layout->addWidget(statusLabel_);
    layout->addWidget(eventLabel_);
    layout->addStretch();
    layout->addWidget(refreshButton);
    layout->addWidget(overlayButton);
    setCentralWidget(central);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshWaydroidStatus);
    connect(overlayButton, &QPushButton::clicked, this, &MainWindow::showOverlay);
    connect(overlay_, &MapperOverlay::keyCaptured, this, &MainWindow::handleCapturedKey);
    connect(statusProcess_, &QProcess::finished, this, [this](int exitCode) {
        const QString output = QString::fromUtf8(statusProcess_->readAllStandardOutput())
            + QString::fromUtf8(statusProcess_->readAllStandardError());
        const bool running = exitCode == 0 && output.contains("RUNNING", Qt::CaseInsensitive);
        setStatus(output.trimmed().isEmpty() ? "No response from Waydroid." : output.trimmed(), running);
    });
    refreshWaydroidStatus();
}

void MainWindow::refreshWaydroidStatus()
{
    if (statusProcess_->state() != QProcess::NotRunning)
        return;
    setStatus("Checking Waydroid…", false);
    statusProcess_->start("waydroid", {"status"});
}

void MainWindow::showOverlay()
{
    overlay_->showFullScreen();
    overlay_->raise();
    overlay_->activateWindow();
    overlay_->setFocus();
}

void MainWindow::handleCapturedKey(int key, bool pressed)
{
    eventLabel_->setText(QString("Last input event: key %1 %2")
        .arg(key).arg(pressed ? "DOWN" : "UP"));
}

void MainWindow::setStatus(const QString &text, bool healthy)
{
    statusLabel_->setText(QString("<b>Waydroid:</b> %1").arg(text.toHtmlEscaped()));
    statusLabel_->setStyleSheet(healthy ? "color: #2e9b56" : "color: #b56b18");
}

