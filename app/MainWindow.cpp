#include "MainWindow.h"
#include "MapperOverlay.h"
#include "IntegratedView.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

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

    QSettings settings;
    auto *resolutionRow = new QHBoxLayout();
    widthBox_ = new QSpinBox(central);
    widthBox_->setRange(320, 7680);
    widthBox_->setValue(std::clamp(
        settings.value("session/lastWidth", 1920).toInt(), 320, 7680));
    widthBox_->setSuffix(" px");
    heightBox_ = new QSpinBox(central);
    heightBox_->setRange(320, 7680);
    heightBox_->setValue(std::clamp(
        settings.value("session/lastHeight", 1080).toInt(), 320, 7680));
    heightBox_->setSuffix(" px");
    favoriteButton_ = new QToolButton(central);
    favoriteButton_->setText("☆");
    favoriteButton_->setToolTip("Add this resolution to favorites");
    favoriteButton_->setAutoRaise(false);
    favoriteBox_ = new QComboBox(central);
    favoriteBox_->setMinimumContentsLength(18);
    resolutionRow->addWidget(new QLabel("Resolution:", central));
    resolutionRow->addWidget(widthBox_);
    resolutionRow->addWidget(new QLabel("×", central));
    resolutionRow->addWidget(heightBox_);
    resolutionRow->addWidget(favoriteButton_);
    resolutionRow->addWidget(favoriteBox_, 1);

    const QStringList storedFavorites =
        settings.value("session/favoriteResolutions").toStringList();
    for (const QString &entry : storedFavorites) {
        const QStringList parts = entry.split('x');
        bool widthOk = false;
        bool heightOk = false;
        const int width = parts.value(0).toInt(&widthOk);
        const int height = parts.value(1).toInt(&heightOk);
        if (!widthOk || !heightOk || width < 320 || width > 7680
            || height < 320 || height > 7680)
            continue;
        const QString normalized = QString("%1x%2").arg(width).arg(height);
        if (!favoriteResolutions_.contains(normalized))
            favoriteResolutions_.append(normalized);
    }

    stopButton_ = new QPushButton("1. Stop Waydroid and unlock resolution", central);
    prepareButton_ = new QPushButton("2. Apply resolution and prepare Waydroid", central);
    integratedButton_ = new QPushButton("3. Open Integrated Android", central);
    auto *overlayButton = new QPushButton("Open input overlay", central);

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addSpacing(8);
    layout->addWidget(statusLabel_);
    layout->addWidget(stopButton_);
    layout->addLayout(resolutionRow);
    layout->addWidget(prepareButton_);
    layout->addWidget(integratedButton_);
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
    connect(integratedView_, &IntegratedView::configurationUnlockedChanged,
            this, &MainWindow::updateControls);
    connect(widthBox_, &QSpinBox::valueChanged,
            this, &MainWindow::saveResolutionSelection);
    connect(heightBox_, &QSpinBox::valueChanged,
            this, &MainWindow::saveResolutionSelection);
    connect(favoriteButton_, &QToolButton::clicked,
            this, &MainWindow::toggleFavoriteResolution);
    connect(favoriteBox_, &QComboBox::activated,
            this, &MainWindow::applyFavoriteResolution);

    refreshFavoriteControls();
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
    const bool unlocked = integratedView_->configurationUnlocked();
    widthBox_->setEnabled(!busy && unlocked);
    heightBox_->setEnabled(!busy && unlocked);
    favoriteButton_->setEnabled(!busy && unlocked);
    favoriteBox_->setEnabled(!busy && unlocked && !favoriteResolutions_.isEmpty());
    prepareButton_->setEnabled(!busy && unlocked);
    integratedButton_->setEnabled(!busy && ready);
    stopButton_->setEnabled(!busy);
}

QString MainWindow::currentResolutionKey() const
{
    return QString("%1x%2").arg(widthBox_->value()).arg(heightBox_->value());
}

void MainWindow::saveResolutionSelection()
{
    QSettings settings;
    settings.setValue("session/lastWidth", widthBox_->value());
    settings.setValue("session/lastHeight", heightBox_->value());
    settings.sync();
    refreshFavoriteControls();
}

void MainWindow::refreshFavoriteControls()
{
    const QSignalBlocker blocker(favoriteBox_);
    favoriteBox_->clear();
    favoriteBox_->addItem("Favorite resolutions…", QString());
    int selectedIndex = 0;
    const QString current = currentResolutionKey();
    for (const QString &entry : favoriteResolutions_) {
        const QStringList parts = entry.split('x');
        favoriteBox_->addItem(QString("%1 × %2").arg(parts.value(0), parts.value(1)),
                              entry);
        if (entry == current)
            selectedIndex = favoriteBox_->count() - 1;
    }
    favoriteBox_->setCurrentIndex(selectedIndex);
    const bool favorite = favoriteResolutions_.contains(current);
    favoriteButton_->setText(favorite ? "★" : "☆");
    favoriteButton_->setToolTip(favorite
        ? "Remove this resolution from favorites"
        : "Add this resolution to favorites");
}

void MainWindow::toggleFavoriteResolution()
{
    const QString current = currentResolutionKey();
    if (favoriteResolutions_.contains(current))
        favoriteResolutions_.removeAll(current);
    else
        favoriteResolutions_.append(current);

    QSettings settings;
    settings.setValue("session/favoriteResolutions", favoriteResolutions_);
    settings.sync();
    refreshFavoriteControls();
    updateControls();
}

void MainWindow::applyFavoriteResolution(int index)
{
    if (index <= 0)
        return;
    const QStringList parts = favoriteBox_->itemData(index).toString().split('x');
    bool widthOk = false;
    bool heightOk = false;
    const int width = parts.value(0).toInt(&widthOk);
    const int height = parts.value(1).toInt(&heightOk);
    if (!widthOk || !heightOk)
        return;
    widthBox_->setValue(width);
    heightBox_->setValue(height);
    saveResolutionSelection();
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
