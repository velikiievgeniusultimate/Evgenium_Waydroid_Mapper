#include "WaydroidVersionManager.h"
#include "IntegratedView.h"

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <unistd.h>

namespace {
QString humanBytes(qint64 bytes)
{
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gib >= 1.0)
        return QString::number(gib, 'f', 1) + " GiB";
    const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return QString::number(mib, 'f', 0) + " MiB";
}
}

WaydroidVersionManager::WaydroidVersionManager(QWidget *mainWindow)
    : QObject(mainWindow), mainWindow_(mainWindow)
{
    integratedView_ = mainWindow_->findChild<IntegratedView *>();
    queryProcess_ = new QProcess(this);
    operationProcess_ = new QProcess(this);
    queryProcess_->setProcessChannelMode(QProcess::MergedChannels);
    operationProcess_->setProcessChannelMode(QProcess::MergedChannels);

    connect(queryProcess_, &QProcess::readyReadStandardOutput, this, [this] {
        queryOutput_ += QString::fromUtf8(queryProcess_->readAllStandardOutput());
    });
    connect(queryProcess_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus) {
        queryOutput_ += QString::fromUtf8(queryProcess_->readAllStandardOutput());
        handleQueryFinished(exitCode);
    });
    connect(queryProcess_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            queryStage_ = QueryStage::Idle;
            setProgress("Не удалось запустить менеджер версий: "
                            + queryProcess_->errorString(), true);
            updateButtons();
        }
    });

    connect(operationProcess_, &QProcess::readyReadStandardOutput, this, [this] {
        operationOutput_ += QString::fromUtf8(operationProcess_->readAllStandardOutput());
        const QStringList lines = operationOutput_.split('\n', Qt::SkipEmptyParts);
        if (!lines.isEmpty())
            setProgress(lines.constLast().trimmed());
    });
    connect(operationProcess_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        operationOutput_ += QString::fromUtf8(operationProcess_->readAllStandardOutput());
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            setProgress("Готово. Обновляю список версий…");
            refresh();
            return;
        }
        const QString details = operationOutput_.right(5000).trimmed();
        setProgress("Операция с Waydroid завершилась ошибкой.", true);
        QMessageBox::warning(dialog_, "Менеджер Waydroid",
                             QString("Операция завершилась с кодом %1.%2")
                                 .arg(exitCode)
                                 .arg(details.isEmpty() ? QString()
                                                        : QString("\n\n%1").arg(details)));
        updateButtons();
    });
    connect(operationProcess_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setProgress("Не удалось открыть запрос системных прав: "
                            + operationProcess_->errorString(), true);
            updateButtons();
        }
    });
}

void WaydroidVersionManager::attachToSettingsMenu()
{
    const auto buttons = mainWindow_->findChildren<QToolButton *>();
    for (QToolButton *button : buttons) {
        if (!button->menu() || button->text() != QStringLiteral("⚙"))
            continue;
        QMenu *menu = button->menu();
        menu->addSeparator();
        QAction *action = menu->addAction("Версии Android / Waydroid…");
        action->setToolTip("Хранить несколько Android-образов и переключаться между ними");
        connect(action, &QAction::triggered, this, &WaydroidVersionManager::openDialog);
        return;
    }
}

QString WaydroidVersionManager::helperPath() const
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString installed = QDir::cleanPath(
        appDir.absoluteFilePath("../scripts/waydroid-instance-manager.py"));
    if (QFileInfo::exists(installed))
        return installed;

    const QString sourceTree = QDir::cleanPath(
        appDir.absoluteFilePath("../../scripts/waydroid-instance-manager.py"));
    return QFileInfo::exists(sourceTree) ? sourceTree : QString();
}

QStringList WaydroidVersionManager::helperBaseArguments() const
{
    QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty())
        dataHome = QDir::homePath() + "/.local/share";
    return {
        helperPath(),
        "--data-home", dataHome,
        "--uid", QString::number(static_cast<qulonglong>(getuid())),
        "--gid", QString::number(static_cast<qulonglong>(getgid())),
    };
}

void WaydroidVersionManager::openDialog()
{
    if (!dialog_)
        buildDialog();
    dialog_->show();
    dialog_->raise();
    dialog_->activateWindow();
    refresh();
}

void WaydroidVersionManager::buildDialog()
{
    dialog_ = new QDialog(mainWindow_);
    dialog_->setWindowTitle("Версии Android / Waydroid");
    dialog_->resize(790, 540);
    dialog_->setMinimumSize(680, 460);

    auto *layout = new QVBoxLayout(dialog_);
    auto *intro = new QLabel(
        "EWM может хранить несколько независимых установок Waydroid. "
        "У каждой версии отдельные system/vendor и пользовательские данные. "
        "Одновременно запускается только одна версия.", dialog_);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *installRow = new QHBoxLayout();
    variantBox_ = new QComboBox(dialog_);
    variantBox_->addItem("Google Play (GAPPS)", "GAPPS");
    variantBox_->addItem("Vanilla", "VANILLA");
    catalogBox_ = new QComboBox(dialog_);
    catalogBox_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    catalogBox_->setMinimumContentsLength(34);
    installButton_ = new QPushButton("Скачать", dialog_);
    installRow->addWidget(new QLabel("Доступно:", dialog_));
    installRow->addWidget(variantBox_);
    installRow->addWidget(catalogBox_, 1);
    installRow->addWidget(installButton_);
    layout->addLayout(installRow);

    instancesTable_ = new QTableWidget(dialog_);
    instancesTable_->setColumnCount(5);
    instancesTable_->setHorizontalHeaderLabels(
        {"Android", "Lineage", "Сборка", "Тип", "Состояние"});
    instancesTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    instancesTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    instancesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    instancesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    instancesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(instancesTable_, 1);

    auto *actionRow = new QHBoxLayout();
    refreshButton_ = new QPushButton("Обновить", dialog_);
    activateButton_ = new QPushButton("Активировать", dialog_);
    deleteButton_ = new QPushButton("Удалить", dialog_);
    actionRow->addWidget(refreshButton_);
    actionRow->addStretch();
    actionRow->addWidget(activateButton_);
    actionRow->addWidget(deleteButton_);
    layout->addLayout(actionRow);

    progressLabel_ = new QLabel("Готово.", dialog_);
    progressLabel_->setWordWrap(true);
    layout->addWidget(progressLabel_);

    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, dialog_);
    connect(closeButtons, &QDialogButtonBox::rejected, dialog_, &QDialog::hide);
    layout->addWidget(closeButtons);

    connect(refreshButton_, &QPushButton::clicked, this, &WaydroidVersionManager::refresh);
    connect(installButton_, &QPushButton::clicked, this, &WaydroidVersionManager::installSelected);
    connect(activateButton_, &QPushButton::clicked, this, &WaydroidVersionManager::activateSelected);
    connect(deleteButton_, &QPushButton::clicked, this, &WaydroidVersionManager::deleteSelected);
    connect(instancesTable_, &QTableWidget::itemSelectionChanged,
            this, &WaydroidVersionManager::updateButtons);
    connect(variantBox_, &QComboBox::currentIndexChanged, this, [this] {
        if (queryProcess_->state() == QProcess::NotRunning
            && operationProcess_->state() == QProcess::NotRunning) {
            startQuery(QueryStage::Catalog,
                       {"catalog", "--variant", variantBox_->currentData().toString()});
        }
    });
    updateButtons();
}

bool WaydroidVersionManager::mapperBusy() const
{
    return integratedView_ && integratedView_->busy();
}

void WaydroidVersionManager::setProgress(const QString &text, bool error)
{
    if (!progressLabel_)
        return;
    progressLabel_->setText(text);
    progressLabel_->setStyleSheet(error ? "color: #c43c3c;" : QString());
}

void WaydroidVersionManager::refresh()
{
    if (!dialog_ || queryProcess_->state() != QProcess::NotRunning
        || operationProcess_->state() != QProcess::NotRunning)
        return;
    if (helperPath().isEmpty()) {
        setProgress("В установке EWM отсутствует waydroid-instance-manager.py", true);
        return;
    }
    setProgress("Проверяю установленные версии…");
    startQuery(QueryStage::Status, {"status"});
}

void WaydroidVersionManager::startQuery(QueryStage stage, const QStringList &arguments)
{
    if (queryProcess_->state() != QProcess::NotRunning)
        return;
    queryStage_ = stage;
    queryOutput_.clear();
    QStringList args = helperBaseArguments();
    args += arguments;
    queryProcess_->start("/usr/bin/python3", args);
    updateButtons();
}

void WaydroidVersionManager::handleQueryFinished(int exitCode)
{
    const QueryStage completed = queryStage_;
    queryStage_ = QueryStage::Idle;
    if (exitCode != 0) {
        setProgress(queryOutput_.trimmed().isEmpty()
                        ? "Не удалось прочитать данные менеджера Waydroid."
                        : queryOutput_.trimmed(), true);
        updateButtons();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(queryOutput_.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setProgress("Менеджер Waydroid вернул некорректный JSON: " + parseError.errorString(), true);
        updateButtons();
        return;
    }
    const QJsonObject root = document.object();
    if (completed == QueryStage::Status) {
        managed_ = root.value("managed").toBool(false);
        refreshInstalledTable(root.value("instances").toArray());
        setProgress(managed_
                        ? "Установленные версии загружены."
                        : "Текущий Waydroid ещё не добавлен в менеджер. Он будет принят автоматически при первой загрузке или активации другой версии.");
        startQuery(QueryStage::Catalog,
                   {"catalog", "--variant", variantBox_->currentData().toString()});
        return;
    }
    if (completed == QueryStage::Catalog) {
        refreshCatalog(root.value("items").toArray());
        setProgress("Готово. Выберите Android для загрузки или установленную версию для активации.");
    }
    updateButtons();
}

void WaydroidVersionManager::refreshInstalledTable(const QJsonArray &instances)
{
    instancesTable_->setRowCount(0);
    for (const QJsonValue &value : instances) {
        const QJsonObject item = value.toObject();
        const int row = instancesTable_->rowCount();
        instancesTable_->insertRow(row);
        auto makeItem = [&](int column, const QString &text) {
            auto *cell = new QTableWidgetItem(text);
            cell->setData(Qt::UserRole, item.value("id").toString());
            cell->setData(Qt::UserRole + 1, item.value("active").toBool());
            instancesTable_->setItem(row, column, cell);
        };
        makeItem(0, item.value("android").toString("?"));
        makeItem(1, item.value("lineage").toString("?"));
        makeItem(2, item.value("build").toString("?"));
        makeItem(3, item.value("variant").toString("?"));
        QString state;
        if (item.value("active").toBool())
            state = managed_ ? "АКТИВЕН" : "Текущий";
        else if (!item.value("installed").toBool(true))
            state = "Повреждён";
        else
            state = "Установлен";
        makeItem(4, state);
    }
    if (instancesTable_->rowCount() > 0)
        instancesTable_->selectRow(0);
    updateButtons();
}

void WaydroidVersionManager::refreshCatalog(const QJsonArray &items)
{
    catalogBox_->clear();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString android = item.value("android").toString();
        if (android != "11" && android != "13")
            continue;
        const QString label = QString("Android %1 · Lineage %2 · %3 · %4 · %5")
                                  .arg(android,
                                       item.value("lineage").toString(),
                                       item.value("variant").toString(),
                                       item.value("build").toString(),
                                       humanBytes(item.value("total_size").toVariant().toLongLong()));
        catalogBox_->addItem(label, android);
    }
    installButton_->setEnabled(catalogBox_->count() > 0);
}

QString WaydroidVersionManager::selectedInstanceId() const
{
    if (!instancesTable_ || instancesTable_->selectedItems().isEmpty())
        return {};
    return instancesTable_->selectedItems().constFirst()->data(Qt::UserRole).toString();
}

bool WaydroidVersionManager::selectedInstanceActive() const
{
    if (!instancesTable_ || instancesTable_->selectedItems().isEmpty())
        return false;
    return instancesTable_->selectedItems().constFirst()->data(Qt::UserRole + 1).toBool();
}

void WaydroidVersionManager::updateButtons()
{
    if (!dialog_)
        return;
    const bool processBusy = queryProcess_->state() != QProcess::NotRunning
                             || operationProcess_->state() != QProcess::NotRunning;
    const bool hasSelection = !selectedInstanceId().isEmpty();
    const bool active = selectedInstanceActive();
    refreshButton_->setEnabled(!processBusy);
    variantBox_->setEnabled(!processBusy);
    catalogBox_->setEnabled(!processBusy);
    installButton_->setEnabled(!processBusy && catalogBox_->count() > 0);
    activateButton_->setEnabled(!processBusy && hasSelection && !active && managed_);
    deleteButton_->setEnabled(!processBusy && hasSelection && !active && managed_);
}

void WaydroidVersionManager::startPrivilegedOperation(
    const QStringList &arguments, const QString &progressText)
{
    if (operationProcess_->state() != QProcess::NotRunning)
        return;
    if (mapperBusy()) {
        QMessageBox::information(dialog_, "Waydroid запущен",
                                 "Сначала остановите текущую интегрированную сессию EWM. "
                                 "Менеджер версий не переключает системные каталоги во время игры.");
        return;
    }
    const QString pkexec = QStandardPaths::findExecutable("pkexec");
    if (pkexec.isEmpty()) {
        QMessageBox::warning(dialog_, "PolicyKit не найден",
                             "Для загрузки, переключения и удаления Waydroid нужен pkexec.");
        return;
    }
    const QString helper = helperPath();
    if (helper.isEmpty()) {
        setProgress("Не найден helper менеджера версий.", true);
        return;
    }

    operationOutput_.clear();
    QStringList helperArgs = helperBaseArguments();
    helperArgs += arguments;
    QStringList pkArgs = {"/usr/bin/python3"};
    pkArgs += helperArgs;
    setProgress(progressText);
    operationProcess_->start(pkexec, pkArgs);
    updateButtons();
}

void WaydroidVersionManager::installSelected()
{
    if (catalogBox_->currentIndex() < 0)
        return;
    const QString android = catalogBox_->currentData().toString();
    const QString variant = variantBox_->currentData().toString();
    const auto answer = QMessageBox::question(
        dialog_, "Скачать Android",
        QString("Скачать отдельный Android %1 (%2)?\n\n"
                "Текущий Waydroid будет временно остановлен на время инициализации, "
                "но после загрузки EWM вернёт его активным. Новая версия получит отдельные пользовательские данные.")
            .arg(android, variant),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return;
    startPrivilegedOperation({"install", "--android", android, "--variant", variant},
                             QString("Подготавливаю отдельный Android %1…").arg(android));
}

void WaydroidVersionManager::activateSelected()
{
    const QString instance = selectedInstanceId();
    if (instance.isEmpty() || selectedInstanceActive())
        return;
    const int row = instancesTable_->currentRow();
    const QString android = row >= 0 && instancesTable_->item(row, 0)
                                ? instancesTable_->item(row, 0)->text() : instance;
    const auto answer = QMessageBox::question(
        dialog_, "Переключить Android",
        QString("Активировать Android %1?\n\n"
                "Waydroid будет остановлен. Текущая версия и её данные останутся на диске, "
                "и к ней можно будет вернуться этим же окном.")
            .arg(android),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return;
    startPrivilegedOperation({"switch", "--id", instance},
                             QString("Переключаю Waydroid на Android %1…").arg(android));
}

void WaydroidVersionManager::deleteSelected()
{
    const QString instance = selectedInstanceId();
    if (instance.isEmpty() || selectedInstanceActive())
        return;
    const auto answer = QMessageBox::warning(
        dialog_, "Удалить Android",
        "Удалить выбранную неактивную версию Waydroid вместе с её отдельными пользовательскими данными?\n\nЭто действие нельзя отменить.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    startPrivilegedOperation({"delete", "--id", instance},
                             "Удаляю выбранную версию Waydroid…");
}
