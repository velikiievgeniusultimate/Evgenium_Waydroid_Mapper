#include "MainWindow.h"
#include "DiagnosticsCollector.h"
#include "IntegratedView.h"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QCloseEvent>
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
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
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
constexpr auto WaydroidSystemOta = "https://ota.waydro.id/system";
constexpr auto WaydroidVendorOta = "https://ota.waydro.id/vendor";

bool waydroidInitialized()
{
    // Match the artifacts produced at the *end* of `waydroid init`, rather
    // than trusting waydroid.cfg alone.  The config is written before image
    // extraction and LXC setup, so an interrupted download can leave it
    // behind even though Android is not bootable yet.
    static const QStringList requiredFiles = {
        "/var/lib/waydroid/waydroid.cfg",
        "/var/lib/waydroid/waydroid_base.prop",
        "/var/lib/waydroid/images/system.img",
        "/var/lib/waydroid/images/vendor.img",
        "/var/lib/waydroid/lxc/waydroid/config",
    };
    return std::all_of(requiredFiles.cbegin(), requiredFiles.cend(),
                       [](const QString &path) {
        const QFileInfo file(path);
        return file.exists() && file.isFile() && file.size() > 0;
    });
}

QString automaticWaydroidPlatform()
{
    if (!QStandardPaths::findExecutable("pacman").isEmpty())
        return QStringLiteral("Arch Linux");
    if (!QStandardPaths::findExecutable("dnf5").isEmpty()
        || !QStandardPaths::findExecutable("dnf").isEmpty())
        return QStringLiteral("Fedora");
    return {};
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), integratedView_(new IntegratedView(this)),
      diagnostics_(new DiagnosticsCollector(this)),
      updateProcess_(new QProcess(this)),
      waydroidInstallProcess_(new QProcess(this))
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
    deviceProfileMenu_ = settingsMenu->addMenu("Android-устройство");
    deviceProfileGroup_ = new QActionGroup(deviceProfileMenu_);
    deviceProfileGroup_->setExclusive(true);

    auto addDeviceProfile = [this](const QString &title, const QString &profileId,
                                   const QString &toolTip) {
        QAction *action = deviceProfileMenu_->addAction(title);
        action->setCheckable(true);
        action->setData(profileId);
        action->setToolTip(toolTip);
        deviceProfileGroup_->addAction(action);
        return action;
    };
    QAction *nativeDeviceAction = addDeviceProfile(
        "Настоящий Waydroid", "native",
        "Не изменять Android-идентификацию Waydroid");
    QAction *pocoDeviceAction = addDeviceProfile(
        "POCO F5 — Mobile Legends", "poco-f5",
        "Представлять Waydroid как POCO F5 (23049PCD8G), чтобы открыть "
        "поддерживаемые игрой графические режимы");
    deviceProfileMenu_->addSeparator();
    QAction *deviceProfileNote = deviceProfileMenu_->addAction(
        "Применяется при следующем запуске");
    deviceProfileNote->setEnabled(false);

    diagnosticsAction_ = settingsMenu->addAction("Collect Waydroid MEGA-log");
    updateAction_ = settingsMenu->addAction("Обновиться");
    installWaydroidAction_ = settingsMenu->addAction("Установить Waydroid");
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
    QString storedDeviceProfile =
        settings.value("session/deviceProfile", "native").toString();
    if (storedDeviceProfile != "poco-f5")
        storedDeviceProfile = "native";
    (storedDeviceProfile == "poco-f5" ? pocoDeviceAction : nativeDeviceAction)
        ->setChecked(true);
    integratedView_->setDeviceProfile(storedDeviceProfile);
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
    connect(installWaydroidAction_, &QAction::triggered,
            this, &MainWindow::installWaydroid);
    connect(deviceProfileGroup_, &QActionGroup::triggered,
            this, &MainWindow::selectDeviceProfile);

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

    waydroidInstallProcess_->setProcessChannelMode(QProcess::MergedChannels);
    connect(waydroidInstallProcess_, &QProcess::readyReadStandardOutput, this,
            [this] {
        const QString output = QString::fromUtf8(
            waydroidInstallProcess_->readAllStandardOutput()).trimmed();
        if (!output.isEmpty()) {
            waydroidInstallOutput_ += output + '\n';
            setActivity(QString("Установка Waydroid: %1")
                            .arg(output.split('\n').constLast().toHtmlEscaped()));
        }
    });
    connect(waydroidInstallProcess_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        waydroidSetupStage_ = WaydroidSetupStage::Idle;
        installWaydroidAction_->setEnabled(true);
        setActivity(QString("Не удалось запустить установку Waydroid: %1")
                        .arg(waydroidInstallProcess_->errorString().toHtmlEscaped()));
        updateControls();
        QMessageBox::warning(
            this, "Waydroid не установлен",
            "Не удалось открыть запрос системных прав. Проверьте, что в системе "
            "работает PolicyKit, затем повторите установку из меню EWM.");
    });
    connect(waydroidInstallProcess_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const WaydroidSetupStage completedStage = waydroidSetupStage_;
        waydroidSetupStage_ = WaydroidSetupStage::Idle;
        waydroidInstallOutput_ += QString::fromUtf8(
            waydroidInstallProcess_->readAllStandardOutput());

        if (exitStatus == QProcess::NormalExit && exitCode == 0
            && completedStage == WaydroidSetupStage::InstallingPackage
            && !QStandardPaths::findExecutable("waydroid").isEmpty()) {
            waydroidPackageInstalled_ = true;
            setStatus("Пакет установлен. Загружаю Android с Google Play…", false);
            setActivity("Waydroid установлен. Перехожу к загрузке и инициализации GAPPS-образов…");
            QTimer::singleShot(0, this, &MainWindow::initializeWaydroid);
            return;
        }

        if (exitStatus == QProcess::NormalExit && exitCode == 0
            && completedStage == WaydroidSetupStage::InitializingImages
            && waydroidInitialized()) {
            waydroidPackageInstalled_ = true;
            waydroidAvailable_ = true;
            installWaydroidAction_->setVisible(false);
            installWaydroidAction_->setEnabled(true);
            setStatus("Waydroid и Android с Google Play готовы к запуску.", true);
            setActivity("Полная установка Waydroid завершена. Теперь нажмите «ЗАПУСТИТЬ».");
            updateControls();
            QMessageBox::information(
                this, "Waydroid готов",
                "Waydroid установлен и инициализирован с Google Play.\n\n"
                "Теперь его можно запускать прямо из EWM.");
            return;
        }

        installWaydroidAction_->setEnabled(true);
        const bool incompleteAfterSuccessfulInit =
            completedStage == WaydroidSetupStage::InitializingImages
            && exitStatus == QProcess::NormalExit && exitCode == 0;
        setActivity(incompleteAfterSuccessfulInit
                        ? "Waydroid сообщил об успехе, но обязательные файлы Android не созданы. Можно безопасно повторить восстановление."
                        : (completedStage == WaydroidSetupStage::InitializingImages
                               ? "Инициализация Android завершилась с ошибкой."
                               : "Установка пакета Waydroid завершилась с ошибкой."));
        updateControls();
        const QString details = waydroidInstallOutput_.right(4000).trimmed();
        QMessageBox::warning(
            this, "Установка Waydroid не завершена",
            QString("Этап установки завершился с кодом %1, но Waydroid пока "
                    "не готов к запуску. Повторите восстановление через пункт "
                    "«Завершить установку Waydroid».%2")
                .arg(exitCode)
                .arg(details.isEmpty() ? QString()
                                       : QString("\n\nПоследний вывод:\n%1").arg(details)));
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
    QTimer::singleShot(0, this, &MainWindow::checkWaydroidAvailability);
}

void MainWindow::checkWaydroidAvailability()
{
    waydroidPackageInstalled_ =
        !QStandardPaths::findExecutable("waydroid").isEmpty();
    waydroidAvailable_ = waydroidPackageInstalled_ && waydroidInitialized();
    installWaydroidAction_->setVisible(!waydroidAvailable_);
    installWaydroidAction_->setText(waydroidPackageInstalled_
        ? "Завершить установку Waydroid"
        : "Установить Waydroid");
    if (waydroidAvailable_) {
        setStatus("Waydroid установлен и инициализирован. Готов к запуску.", true);
        return;
    }
    if (waydroidPackageInstalled_) {
        setStatus("Пакет Waydroid установлен, но Android ещё не загружен.", false);
        setActivity("Нужно завершить установку: EWM загрузит GAPPS-образы и выполнит waydroid init.");
    } else {
        setStatus("Waydroid не установлен.", false);
        setActivity("Для запуска EWM сначала установите Waydroid.");
    }
    offerWaydroidInstallation();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (waydroidInstallProcess_->state() != QProcess::NotRunning
        || waydroidSetupStage_ != WaydroidSetupStage::Idle) {
        QMessageBox::information(
            this, "Установка Waydroid выполняется",
            "Сейчас загружаются или распаковываются системные файлы Android. "
            "Дождитесь завершения установки — её прерывание оставит Waydroid "
            "в неполном состоянии.");
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::offerWaydroidInstallation()
{
    if (waydroidAvailable_ || waydroidInstallProcess_->state() != QProcess::NotRunning)
        return;
    const auto answer = QMessageBox::question(
        this, waydroidPackageInstalled_ ? "Завершить установку Waydroid?"
                                       : "Установить Waydroid?",
        waydroidPackageInstalled_
            ? "Пакет Waydroid уже установлен, но Android-образы ещё не загружены.\n\n"
              "Загрузить и инициализировать версию с Google Play сейчас?"
            : QString("EWM установит официальный пакет Waydroid для %1, затем "
                      "автоматически выберет образ для архитектуры компьютера "
                      "и инициализирует Android с Google Play.\n\nПродолжить?")
                  .arg(automaticWaydroidPlatform().isEmpty()
                           ? QStringLiteral("этой системы")
                           : automaticWaydroidPlatform()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes)
        installWaydroid();
}

void MainWindow::installWaydroid()
{
    if (waydroidAvailable_ || waydroidInstallProcess_->state() != QProcess::NotRunning)
        return;
    if (waydroidPackageInstalled_) {
        initializeWaydroid();
        return;
    }
    const QString pkexec = QStandardPaths::findExecutable("pkexec");
    const QString pacman = QStandardPaths::findExecutable("pacman");
    QString dnf = QStandardPaths::findExecutable("dnf5");
    if (dnf.isEmpty())
        dnf = QStandardPaths::findExecutable("dnf");
    if (pkexec.isEmpty() || (pacman.isEmpty() && dnf.isEmpty())) {
        QMessageBox::information(
            this, "Установка Waydroid",
            "EWM не нашёл PolicyKit либо поддерживаемый пакетный менеджер. "
            "Автоматическая установка поддерживается на Arch Linux и Fedora.");
        return;
    }
    waydroidInstallOutput_.clear();
    waydroidSetupStage_ = WaydroidSetupStage::InstallingPackage;
    if (!pacman.isEmpty()) {
        waydroidInstallProcess_->start(
            pkexec, {pacman, "-S", "--needed", "--noconfirm", "waydroid"});
    } else {
        // Fedora ships both Waydroid and its SELinux policy in the official
        // repositories.  Installing the policy explicitly avoids a container
        // start that only fails later under enforcing SELinux.
        waydroidInstallProcess_->start(
            pkexec, {dnf, "-y", "install", "waydroid", "waydroid-selinux"});
    }
    if (!waydroidInstallProcess_->waitForStarted(1000)) {
        waydroidSetupStage_ = WaydroidSetupStage::Idle;
        return;
    }
    installWaydroidAction_->setEnabled(false);
    setStatus("Устанавливаю пакет Waydroid…", false);
    setActivity(QString("Подтвердите системные права. Устанавливаю Waydroid для %1; "
                        "после пакета EWM автоматически загрузит %2 GAPPS-образ.")
                    .arg(automaticWaydroidPlatform())
                    .arg(QSysInfo::currentCpuArchitecture()));
    updateControls();
}

void MainWindow::initializeWaydroid()
{
    if (waydroidAvailable_ || waydroidInstallProcess_->state() != QProcess::NotRunning)
        return;
    const QString pkexec = QStandardPaths::findExecutable("pkexec");
    const QString waydroid = QStandardPaths::findExecutable("waydroid");
    if (pkexec.isEmpty() || waydroid.isEmpty()) {
        setActivity("Не удалось начать инициализацию: pkexec или waydroid не найден.");
        updateControls();
        return;
    }

    waydroidInstallOutput_.clear();
    waydroidSetupStage_ = WaydroidSetupStage::InitializingImages;
    // -f is intentional: a cancelled first initialization can leave
    // waydroid.cfg/rootfs behind.  Upstream otherwise treats those two
    // artifacts as "Already initialized" and never repairs the missing base
    // properties, images or LXC configuration.  Fedora's Waydroid package does
    // not preconfigure OTA endpoints; without explicit -c/-v the CLI logs an
    // error but may still exit with code 0.  Generic official endpoints let
    // upstream select x86_64, arm64 or arm64_only after CPU detection.
    waydroidInstallProcess_->start(
        pkexec, {waydroid, "init", "-f",
                 "-c", WaydroidSystemOta,
                 "-v", WaydroidVendorOta,
                 "-s", "GAPPS"});
    if (!waydroidInstallProcess_->waitForStarted(1000)) {
        waydroidSetupStage_ = WaydroidSetupStage::Idle;
        return;
    }
    installWaydroidAction_->setEnabled(false);
    setStatus("Загружаю и подготавливаю Android с Google Play…", false);
    setActivity("Восстанавливаю и проверяю полную установку Waydroid. Загрузка образов может занять заметное время; не закрывайте EWM.");
    updateControls();
}

void MainWindow::startWaydroid()
{
    if (!waydroidAvailable_) {
        offerWaydroidInstallation();
        return;
    }
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

void MainWindow::selectDeviceProfile(QAction *action)
{
    if (!action)
        return;
    const QString profileId = action->data().toString();
    if (profileId != "native" && profileId != "poco-f5")
        return;

    QSettings settings;
    settings.setValue("session/deviceProfile", profileId);
    settings.sync();
    integratedView_->setDeviceProfile(profileId);

    setActivity(profileId == "poco-f5"
        ? "Выбран профиль POCO F5 для Mobile Legends. Он применится при следующем запуске Waydroid."
        : "Выбрано настоящее устройство Waydroid. Подмена будет снята при следующем запуске.");
}

void MainWindow::updateControls()
{
    const bool busy = integratedView_->busy();
    const bool unlocked = integratedView_->configurationUnlocked();
    if (!busy)
        megaStopRequested_ = false;

    const bool installing = waydroidInstallProcess_->state() != QProcess::NotRunning
                            || waydroidSetupStage_ != WaydroidSetupStage::Idle;
    startButton_->setEnabled(!busy && !installing && waydroidAvailable_);
    startButton_->setText(installing ? "УСТАНАВЛИВАЕТСЯ…"
                                     : (busy ? "ВЫПОЛНЯЕТСЯ…" : "ЗАПУСТИТЬ"));
    changeResolutionButton_->setEnabled(!busy && !installing);
    // MEGA STOP is a runtime recovery tool.  Killing `waydroid init` while it
    // extracts images creates exactly the half-installed state we repair
    // above, so keep it unavailable during setup.
    megaStopButton_->setEnabled(!megaStopRequested_ && !installing);
    widthBox_->setEnabled(!busy && unlocked);
    heightBox_->setEnabled(!busy && unlocked);
    favoriteButton_->setEnabled(!busy && unlocked);
    favoriteBox_->setEnabled(!busy && unlocked && !favoriteResolutions_.isEmpty());
    deviceProfileMenu_->setEnabled(!busy && waydroidAvailable_);
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
