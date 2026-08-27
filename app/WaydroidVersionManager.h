#pragma once

#include <QObject>
#include <QJsonArray>
#include <QString>
#include <QStringList>

class QComboBox;
class QDialog;
class QLabel;
class QProcess;
class QPushButton;
class QTableWidget;
class QWidget;
class IntegratedView;

class WaydroidVersionManager final : public QObject
{
    Q_OBJECT
public:
    explicit WaydroidVersionManager(QWidget *mainWindow);
    void attachToSettingsMenu();

private:
    enum class QueryStage { Idle, Status, Catalog };

    void openDialog();
    void buildDialog();
    void refresh();
    void startQuery(QueryStage stage, const QStringList &arguments);
    void handleQueryFinished(int exitCode);
    void refreshInstalledTable(const QJsonArray &instances);
    void refreshCatalog(const QJsonArray &items);
    void startPrivilegedOperation(const QStringList &arguments,
                                  const QString &progressText);
    void activateSelected();
    void deleteSelected();
    void installSelected();
    void updateButtons();
    QString helperPath() const;
    QStringList helperBaseArguments() const;
    QString selectedInstanceId() const;
    bool selectedInstanceActive() const;
    bool mapperBusy() const;
    void setProgress(const QString &text, bool error = false);

    QWidget *mainWindow_ = nullptr;
    IntegratedView *integratedView_ = nullptr;
    QDialog *dialog_ = nullptr;
    QComboBox *variantBox_ = nullptr;
    QComboBox *catalogBox_ = nullptr;
    QTableWidget *instancesTable_ = nullptr;
    QLabel *progressLabel_ = nullptr;
    QPushButton *installButton_ = nullptr;
    QPushButton *activateButton_ = nullptr;
    QPushButton *deleteButton_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QProcess *queryProcess_ = nullptr;
    QProcess *operationProcess_ = nullptr;
    QueryStage queryStage_ = QueryStage::Idle;
    QString queryOutput_;
    QString operationOutput_;
    bool managed_ = false;
};
