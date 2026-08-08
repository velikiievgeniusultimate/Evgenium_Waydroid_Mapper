#include "MainWindow.h"
#include "MapperOverlay.h"
#include "IntegratedView.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), overlay_(new MapperOverlay()),
      integratedView_(new IntegratedView(this))
{
    setWindowTitle("Evgenium Waydroid Mapper");
    resize(620, 500);

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
        "Choose a resolution while Waydroid is stopped. Prepare the hidden Android "
        "session first; Integrated Android unlocks only after its surface is ready.", central);
    description->setWordWrap(true);

    statusLabel_ = new QLabel("Waydroid is not prepared yet.", central);
    statusLabel_->setWordWrap(true);
    eventLabel_ = new QLabel("Last input event: none", central);

    auto *resolutionRow = new QHBoxLayout();
    widthBox_ = new QSpinBox(central);
    widthBox_->setRange(320, 7680);
    widthBox_->setValue(1920);
    widthBox_->setSuffix(" px");
    heightBox_ = new QSpinBox(central);
    heightBox_->setRange(320, 7680);
    heightBox_->setValue(1080);
    heightBox_->setSuffix(" px");
    resolutionRow->addWidget(new QLabel("Resolution:", central));
    resolutionRow->addWidget(widthBox_);
    resolutionRow->addWidget(new QLabel("×", central));
    resolutionRow->addWidget(heightBox_);
    resolutionRow->addStretch();

    prepareButton_ = new QPushButton("1. Apply resolution and start Waydroid", central);
    integratedButton_ = new QPushButton("2. Open Integrated Android", central);
    stopButton_ = new QPushButton("Stop Waydroid and unlock resolution", central);
    auto *overlayButton = new QPushButton("Open input overlay", central);

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addSpacing(8);
    layout->addWidget(statusLabel_);
    layout->addLayout(resolutionRow);
    layout->addWidget(prepareButton_);
    layout->addWidget(integratedButton_);
    layout->addWidget(stopButton_);
    layout->addStretch();
    layout->addWidget(eventLabel_);
    layout->addWidget(overlayButton);
    setCentralWidget(central);

    connect(prepareButton_, &QPushButton::clicked, this, &MainWindow::prepareWaydroid);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopWaydroid);
    connect(integratedButton_, &QPushButton::clicked,
            this, &MainWindow::showIntegratedWaydroid);
    connect(overlayButton, &QPushButton::clicked, this, &MainWindow::showOverlay);
    connect(overlay_, &MapperOverlay::keyCaptured, this, &MainWindow::handleCapturedKey);
    connect(integratedView_, &IntegratedView::statusChanged, this,
            [this](const QString &status) {
        setStatus(status, integratedView_->ready());
    });
    connect(integratedView_, &IntegratedView::busyChanged,
            this, &MainWindow::updateControls);
    connect(integratedView_, &IntegratedView::readyChanged,
            this, &MainWindow::updateControls);

    updateControls();
}

void MainWindow::prepareWaydroid()
{
    integratedView_->prepareAndStart(widthBox_->value(), heightBox_->value());
}

void MainWindow::stopWaydroid()
{
    integratedView_->stopIntegratedSession();
}

void MainWindow::showIntegratedWaydroid()
{
    integratedView_->openIntegratedWindow();
}

void MainWindow::updateControls()
{
    const bool busy = integratedView_->busy();
    const bool ready = integratedView_->ready();
    widthBox_->setEnabled(!busy && !ready);
    heightBox_->setEnabled(!busy && !ready);
    prepareButton_->setEnabled(!busy && !ready);
    integratedButton_->setEnabled(!busy && ready);
    stopButton_->setEnabled(!busy);
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
