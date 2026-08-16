#include "MainWindow.h"
#include "DiagnosticsCollector.h"
#include "IntegratedView.h"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

namespace {
constexpr auto UpdateFallbackCommand =
    "curl -fsSL "
    "https://raw.githubusercontent.com/velikiievgeniusultimate/"
    "Evgenium_Waydroid_Mapper/main/scripts/install.sh | bash";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), integratedView_(new IntegratedView(this)),
      diagnostics_(new DiagnosticsCollector(this)),
      updateProcess_(new QProcess(this))
{
    setWindowTitle("EWM");
    resize(620, 430);
    setMinimumSize(520, 360);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);

    auto *titleRow = new QHBoxLayout();
    auto *titleColumn = new QVBoxLayout();
    titleColumn->setSpacing(0);
    auto *title = new QLabel("EWM", central);
    QFont titleFont = title->font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    title->setFont(titleFont);
    auto *fullName = new QLabel("Evgenium Waydroid Mapper", central);
    fullName->setStyleSheet("color: palette(mid);");
    titleColumn->addWidget(title);
    titleColumn->addWidget(fullName);

    settingsButton_ = new QToolButton(central);
    settingsButton_->setText("⚙");
    settingsButton_->setToolTip("Настройки EWM");
    settingsButton_->setFixedSize(42, 42);
    settingsButton_->setPopupMode(QToolButton::InstantPopup);
    settingsButton_->setStyleSheet("font-size: 22px;");
    auto *settingsMenu = new QMenu(settingsButton_);
    diagnosticsAction_ = settingsMenu->addAction("Collect Waydroid MEGA-log");
    updateAction_ = settingsMenu->addAction("Обновиться");
    settingsButton_->setMenu(settingsMenu);

    titleRow->addLayout(titleColumn);
    titleRow->addStretch();
    titleRow->addWidget(settingsButton_, 0, Qt::AlignTop);

    statusLabel_ = new QLabel("Готов к запуску с последним разрешением.", central);
    statusLabel_->setWordWrap(true);
    activityLabel_ = new QLabel(central);
    activityLabel_->setWordWrap(true);
    activityLabel_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    activityLabel_->setOpenExternalLinks(true);
    activityLabel_->hide();

    startButton_ = new QPushButton("ЗАПУСТИТЬ", central);
    startButton_->setMinimumHeight(62);
    startButton_->setStyleSheet(
        "QPushButton { font-size: 19px; font-weight: 700; }"
        "QPushButton:disabled { font-weight: 600; }");

    auto *actionRow = new QHBoxLayout();
    changeResolutionButton_ = new QPushButton("Изменить разрешение", central);
    megaStopButton_ = new QPushButton("МЕГА СТОП", central);
    megaStopButton_->setToolTip(
        "Безусловно завершить сессию, LXC-контейнер и службу Waydroid");
    megaStopButton_->setStyleSheet(
        "QPushButton { color: #ffffff; background: #a62d2d; font-weight: 700; "
        "padding: 8px; border-radius: 4px; }"
        "QPushButton:hover { background: #c23939; }"
        "QPushButton:pressed { background: #812222; }");
    actionRow->addWidget(changeResolutionButton_, 1);
    actionRow->addWidget(megaStopButton_, 1);

    resolutionPanel_ = new QWidget(central);
    auto *resolutionRow = new QHBoxLayout(resolutionPanel_);
    resolutionRow->setContentsMargins(0, 4, 0, 0);
    resolutionRow->setSpacing(8);
    QSettings settings;
    widthBox_ = new QSpinBox(resolutionPanel_);
    widthBox_->setRange(320, 7680);
    widthBox_->setValue(std::clamp(
        settings.value("session/lastWidth", 1920).toInt(), 320, 7680));
    widthBox_->setSuffix(" px");
    heightBox_ = new QSpinBox(resolutionPanel_);
    heightBox_->setRange(320, 7680);
    heightBox_->setValue(std::clamp(
        settings.value("session/lastHeight", 1080).toInt(), 320, 7680));
    heightBox_->setSuffix(" px");
    favoriteButton_ = new QToolButton(resolutionPanel_);
    favoriteButton_->setText("☆");
    favoriteButton_->setToolTip("Добавить разрешение в избранное");
    favoriteButton_->setAutoRaise(false);
    favoriteBox_ = new QComboBox(resolutionPanel_);
    favoriteBox_->setMinimumContentsLength(15);
    resolutionRow->addWidget(new QLabel("Разрешение:", resolutionPanel_));
    resolutionRow->addWidget(widthBox_);
    resolutionRow->addWidget(new QLabel("×", resolutionPanel_));
    resolutionRow->addWidget(heightBox_);
    resolutionRow->addWidget(favoriteButton_);
    resolutionRow->addWidget(favoriteBox_, 1);
    resolutionPanel_->hide();

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

    layout->addLayout(titleRow);
    layout->addSpacing(6);
    layout->addWidget(statusLabel_);
    layout->addWidget(activityLabel_);
    layout->addStretch();
    layout->addWidget(startButton_);
    layout->addLayout(actionRow);
    layout->addWidget(resolutionPanel_);
    setCentralWidget(central);

    connect(startButton_, &QPushButton::clicked,
            this, &MainWindow::startWaydroid);
    connect(changeResolutionButton_, &QPushButton::clicked,
            this, &MainWindow::requestResolutionChange);
    connect(megaStopButton_, &QPushButton::clicked,
            this, &MainWindow::megaStopWaydroid);
    connect(diagnosticsAction_, &QAction::triggered,
            this, &MainWindow::collectDiagnostics);
    connect(updateAction_, &QAction::triggered,
            this, &MainWindow::startUpdate);

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
    connect(integratedView_, &IntegratedView::operationFailed, this,
            [this](const QString &) {
        if (diagnosticsAutoStarted_ || diagnostics_->running())
            return;
        diagnosticsAutoStarted_ = true;
        setActivity("Запуск не удался. Автоматически собираю MEGA-log…");
        collectDiagnostics();
    });

    connect(diagnostics_, &DiagnosticsCollector::runningChanged, this,
            [this](bool running) {
        diagnosticsAction_->setEnabled(!running);
        diagnosticsAction_->setText(running
            ? "MEGA-log собирается…"
            : "Collect Waydroid MEGA-log");
    });
    connect(diagnostics_, &DiagnosticsCollector::progressChanged, this,
            [this](const QString &message) {
        setActivity(message.toHtmlEscaped());
    });
    connect(diagnostics_, &DiagnosticsCollector::finished, this,
            [this](const QString &path, bool privileged, const QString &message) {
        const QString url = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
        setActivity(
            QString("%1<br><a href=\"%2\">%3</a>%4")
                .arg(message.toHtmlEscaped(), url, path.toHtmlEscaped(),
                     privileged ? QString()
                                : QStringLiteral(
                                      "<br><b>Root section is incomplete.</b>")));
    });

    updateProcess_->setProcessChannelMode(QProcess::MergedChannels);
    connect(updateProcess_, &QProcess::readyReadStandardOutput, this, [this] {
        updateOutput_ += QString::fromUtf8(updateProcess_->readAllStandardOutput());
        const QStringList lines = updateOutput_.split('\n', Qt::SkipEmptyParts);
        if (!lines.isEmpty())
            setActivity(QString("Обновление: %1")
                            .arg(lines.constLast().trimmed().toHtmlEscaped()));
    });
    connect(updateProcess_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        updateAction_->setEnabled(true);
        setActivity(QString("Не удалось запустить обновление: %1")
                        .arg(updateProcess_->errorString().toHtmlEscaped()));
    });
    connect(updateProcess_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        updateOutput_ += QString::fromUtf8(updateProcess_->readAllStandardOutput());
        updateAction_->setEnabled(true);
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            setActivity("Обновление установлено. Перезапустите EWM, чтобы применить его.");
            QMessageBox::information(
                this, "EWM обновлён",
                "Обновление установлено. Закройте и снова откройте EWM, "
                "чтобы запустить новую версию.");
            return;
        }

        const QString details = updateOutput_.right(5000).trimmed();
        setActivity("Обновление завершилось с ошибкой. Подробности показаны в окне.");
        QMessageBox::critical(
            this, "Ошибка обновления",
            QString("Установщик завершился с кодом %1.\n\n%2")
                .arg(exitCode).arg(details));
    });

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

void MainWindow::startWaydroid()
{
    diagnosticsAutoStarted_ = false;
    setActivity(QString("Запускаю EWM в разрешении %1 × %2…")
                    .arg(widthBox_->value()).arg(heightBox_->value()));
    resolutionPanel_->hide();
    integratedView_->startAndOpen(widthBox_->value(), heightBox_->value());
}

void MainWindow::requestResolutionChange()
{
    if (integratedView_->busy())
        return;

    const bool showEditor = !resolutionPanel_->isVisible();
    resolutionPanel_->setVisible(showEditor);
    if (!showEditor)
        return;

    if (!integratedView_->configurationUnlocked()) {
        setActivity("Останавливаю Waydroid перед изменением разрешения…");
        integratedView_->stopIntegratedSession();
    } else {
        setActivity("Выберите разрешение и нажмите «ЗАПУСТИТЬ».");
    }
}

void MainWindow::megaStopWaydroid()
{
    if (megaStopRequested_)
        return;
    megaStopRequested_ = true;
    megaStopButton_->setEnabled(false);
    setActivity("МЕГА СТОП: безусловно завершаю все компоненты Waydroid…");
    integratedView_->megaStopWaydroid();
}

void MainWindow::collectDiagnostics()
{
    diagnostics_->collect();
}

QString MainWindow::updaterScriptPath() const
{
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    const QString bundled =
        applicationDirectory.absoluteFilePath("../scripts/update.sh");
    return QFileInfo::exists(bundled) ? QDir::cleanPath(bundled) : QString();
}

void MainWindow::startUpdate()
{
    if (updateProcess_->state() != QProcess::NotRunning)
        return;

    updateOutput_.clear();
    updateAction_->setEnabled(false);
    setActivity("Проверяю и устанавливаю последнюю версию EWM…");
    const QString updater = updaterScriptPath();
    updateProcess_->setProgram("/bin/bash");
    if (!updater.isEmpty())
        updateProcess_->setArguments({updater});
    else
        updateProcess_->setArguments({"-c", QString::fromLatin1(UpdateFallbackCommand)});
    updateProcess_->start();
}

void MainWindow::updateControls()
{
    const bool busy = integratedView_->busy();
    const bool unlocked = integratedView_->configurationUnlocked();
    if (!busy)
        megaStopRequested_ = false;

    startButton_->setEnabled(!busy);
    startButton_->setText(busy ? "ВЫПОЛНЯЕТСЯ…" : "ЗАПУСТИТЬ");
    changeResolutionButton_->setEnabled(!busy);
    megaStopButton_->setEnabled(!megaStopRequested_);
    widthBox_->setEnabled(!busy && unlocked);
    heightBox_->setEnabled(!busy && unlocked);
    favoriteButton_->setEnabled(!busy && unlocked);
    favoriteBox_->setEnabled(!busy && unlocked && !favoriteResolutions_.isEmpty());
    if (!busy && unlocked && resolutionPanel_->isVisible())
        setActivity("Выберите разрешение и нажмите «ЗАПУСТИТЬ».");
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
    favoriteBox_->addItem("Избранные разрешения…", QString());
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
        ? "Удалить разрешение из избранного"
        : "Добавить разрешение в избранное");
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

void MainWindow::setActivity(const QString &text, bool visible)
{
    activityLabel_->setText(text);
    activityLabel_->setVisible(visible && !text.isEmpty());
}

void MainWindow::setStatus(const QString &text, bool healthy)
{
    statusLabel_->setText(QString("<b>Waydroid:</b> %1").arg(text.toHtmlEscaped()));
    statusLabel_->setStyleSheet(healthy ? "color: #2e9b56" : "color: #b56b18");
}
