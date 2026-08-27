#include "WaydroidAppDataManager.h"
#include "IntegratedView.h"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <unistd.h>

namespace {
constexpr auto RegistryPath = "/var/lib/ewm-waydroid/registry.json";

QString instanceLabel(const QJsonObject &item, bool active)
{
    QString label = QString("Android %1 · Lineage %2 · %3 · %4")
                        .arg(item.value("android").toString("?"),
                             item.value("lineage").toString("?"),
                             item.value("variant").toString("?"),
                             item.value("build").toString("?"));
    if (active)
        label += " · АКТИВЕН";
    return label;
}
}

WaydroidAppDataManager::WaydroidAppDataManager(QWidget *mainWindow)
    : QObject(mainWindow), mainWindow_(mainWindow)
{
    integratedView_ = mainWindow_->findChild<IntegratedView *>();
    copyProcess_ = new QProcess(this);
    copyProcess_->setProcessChannelMode(QProcess::MergedChannels);

    connect(copyProcess_, &QProcess::readyReadStandardOutput, this, [this] {
        copyOutput_ += QString::fromUtf8(copyProcess_->readAllStandardOutput());
        if (!progressDialog_)
            return;
        const QStringList lines = copyOutput_.split('\n', Qt::SkipEmptyParts);
        if (!lines.isEmpty())
            progressDialog_->setLabelText(lines.constLast().trimmed());
    });

    connect(copyProcess_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        copyOutput_ += QString::fromUtf8(copyProcess_->readAllStandardOutput());
        if (progressDialog_) {
            progressDialog_->close();
            progressDialog_->deleteLater();
            progressDialog_ = nullptr;
        }

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            const QStringList lines = copyOutput_.split('\n', Qt::SkipEmptyParts);
            const QString tail = lines.isEmpty() ? QString() : lines.constLast().trimmed();
            QMessageBox::information(
                mainWindow_, "Данные приложения скопированы",
                QString("Перенос завершён.%1\n\n"
                        "Теперь можно активировать целевой Android и запустить игру.")
                    .arg(tail.isEmpty() ? QString()
                                        : QString("\n\n%1").arg(tail)));
            return;
        }

        QMessageBox::warning(
            mainWindow_, "Не удалось скопировать данные",
            QString("Копирование завершилось с кодом %1.\n\n%2")
                .arg(exitCode)
                .arg(copyOutput_.right(6000).trimmed()));
    });

    connect(copyProcess_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (progressDialog_) {
            progressDialog_->close();
            progressDialog_->deleteLater();
            progressDialog_ = nullptr;
        }
        QMessageBox::warning(
            mainWindow_, "Не удалось запустить копирование",
            copyProcess_->errorString());
    });
}

void WaydroidAppDataManager::attachToSettingsMenu()
{
    const auto buttons = mainWindow_->findChildren<QToolButton *>();
    for (QToolButton *button : buttons) {
        if (!button->menu() || button->text() != QStringLiteral("⚙"))
            continue;
        QMenu *menu = button->menu();
        QAction *action =
            menu->addAction("Копировать данные приложения между Android…");
        action->setToolTip(
            "Перенести данные игры или приложения между установленными версиями Android");
        connect(action, &QAction::triggered,
                this, &WaydroidAppDataManager::openCopyDialog);
        return;
    }
}

QString WaydroidAppDataManager::helperPath() const
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString installed =
        QDir::cleanPath(appDir.absoluteFilePath("../scripts/waydroid-app-data.py"));
    if (QFileInfo::exists(installed))
        return installed;

    const QString sourceTree =
        QDir::cleanPath(appDir.absoluteFilePath("../../scripts/waydroid-app-data.py"));
    return QFileInfo::exists(sourceTree) ? sourceTree : QString();
}

bool WaydroidAppDataManager::mapperBusy() const
{
    return integratedView_ && integratedView_->busy();
}

void WaydroidAppDataManager::openCopyDialog()
{
    if (copyProcess_->state() != QProcess::NotRunning) {
        QMessageBox::information(mainWindow_, "Копирование уже выполняется",
                                 "Дождитесь завершения текущего переноса данных.");
        return;
    }
    if (mapperBusy()) {
        QMessageBox::information(
            mainWindow_, "Waydroid запущен",
            "Сначала остановите интегрированную сессию EWM. "
            "Копировать живые данные работающего Android небезопасно.");
        return;
    }

    QFile registryFile(QString::fromLatin1(RegistryPath));
    if (!registryFile.open(QIODevice::ReadOnly)) {
        QMessageBox::information(
            mainWindow_, "Нет нескольких Android",
            "Сначала откройте «Версии Android / Waydroid…» и добавьте вторую "
            "версию Android. После этого данные приложений можно переносить между ними.");
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(registryFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(mainWindow_, "Повреждён реестр Waydroid",
                             "EWM не смог прочитать registry.json: "
                                 + parseError.errorString());
        return;
    }

    const QJsonObject registry = document.object();
    const QJsonObject instances = registry.value("instances").toObject();
    const QString activeId = registry.value("active").toString();
    if (instances.size() < 2) {
        QMessageBox::information(
            mainWindow_, "Нужны две версии Android",
            "Для копирования должны быть установлены как минимум две версии Android.");
        return;
    }

    QDialog dialog(mainWindow_);
    dialog.setWindowTitle("Копировать данные приложения");
    dialog.setMinimumWidth(610);
    auto *layout = new QVBoxLayout(&dialog);

    auto *intro = new QLabel(
        "EWM перенесёт private data приложения, Android/data, OBB и Android/media. "
        "Приложение должно быть установлено на обоих Android: так EWM сможет "
        "правильно заменить UID/GID файлов на целевой версии.", &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout();
    auto *sourceBox = new QComboBox(&dialog);
    auto *targetBox = new QComboBox(&dialog);

    int activeIndex = 0;
    int index = 0;
    for (auto it = instances.begin(); it != instances.end(); ++it, ++index) {
        const QJsonObject item = it.value().toObject();
        const bool active = it.key() == activeId;
        const QString label = instanceLabel(item, active);
        sourceBox->addItem(label, it.key());
        targetBox->addItem(label, it.key());
        if (active)
            activeIndex = index;
    }
    sourceBox->setCurrentIndex(activeIndex);
    const int targetIndex = activeIndex == 0 ? 1 : 0;
    targetBox->setCurrentIndex(targetIndex);

    auto *packageEdit = new QLineEdit("com.mobile.legends", &dialog);
    packageEdit->setPlaceholderText("например: com.mobile.legends");

    form->addRow("Откуда:", sourceBox);
    form->addRow("Куда:", targetBox);
    form->addRow("Android package:", packageEdit);
    layout->addLayout(form);

    auto *warning = new QLabel(
        "<b>Важно:</b> данные на целевом Android будут заменены, но EWM сначала "
        "сохранит их резервную копию. Android Keystore и системные секреты "
        "приложения не переносятся; некоторые логины или данные игры могут быть "
        "несовместимы между разными версиями Android/самой игры.", &dialog);
    warning->setWordWrap(true);
    layout->addWidget(warning);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Копировать");
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString source = sourceBox->currentData().toString();
    const QString target = targetBox->currentData().toString();
    const QString packageName = packageEdit->text().trimmed();
    if (source.isEmpty() || target.isEmpty() || source == target) {
        QMessageBox::warning(mainWindow_, "Неверный выбор",
                             "Источник и цель должны быть разными версиями Android.");
        return;
    }
    if (packageName.isEmpty()) {
        QMessageBox::warning(mainWindow_, "Не указано приложение",
                             "Укажите package name игры или приложения.");
        return;
    }

    const auto answer = QMessageBox::warning(
        mainWindow_, "Перенести данные приложения?",
        QString("Скопировать данные:\n\n%1\n\nиз:\n%2\n\nв:\n%3\n\n"
                "Waydroid будет остановлен. Текущие данные приложения на цели "
                "будут сохранены в backup перед заменой.")
            .arg(packageName, sourceBox->currentText(), targetBox->currentText()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    startCopy(source, target, packageName);
}

void WaydroidAppDataManager::startCopy(
    const QString &source, const QString &target, const QString &packageName)
{
    const QString pkexec = QStandardPaths::findExecutable("pkexec");
    const QString helper = helperPath();
    if (pkexec.isEmpty()) {
        QMessageBox::warning(mainWindow_, "PolicyKit не найден",
                             "Для доступа к Android private data требуется pkexec.");
        return;
    }
    if (helper.isEmpty()) {
        QMessageBox::warning(mainWindow_, "Helper отсутствует",
                             "В установке EWM отсутствует waydroid-app-data.py.");
        return;
    }

    QString dataHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty())
        dataHome = QDir::homePath() + "/.local/share";

    QStringList arguments = {
        "/usr/bin/python3", helper,
        "--data-home", dataHome,
        "--uid", QString::number(static_cast<qulonglong>(getuid())),
        "--gid", QString::number(static_cast<qulonglong>(getgid())),
        "copy",
        "--source", source,
        "--target", target,
        "--package", packageName,
    };

    copyOutput_.clear();
    progressDialog_ = new QProgressDialog(
        "Подготавливаю перенос данных приложения…", QString(), 0, 0, mainWindow_);
    progressDialog_->setWindowTitle("Копирование данных Android");
    progressDialog_->setWindowModality(Qt::ApplicationModal);
    progressDialog_->setCancelButton(nullptr);
    progressDialog_->setMinimumDuration(0);
    progressDialog_->show();

    copyProcess_->start(pkexec, arguments);
}
