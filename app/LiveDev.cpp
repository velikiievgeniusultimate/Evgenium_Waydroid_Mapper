#include "LiveDev.h"
#include "LiveDevUi.h"
#include "IntegratedView.h"
#include <QDir>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaMethod>
#include <QStandardPaths>
#include <QTimer>
#include <QDebug>
#include <QWaylandSurface>
#include <QWindow>
#include <QQuickWindow>
#include <QCoreApplication>
#include <QFile>
#include <QCryptographicHash>

LiveDev::LiveDev(IntegratedView *view) : QObject(view)
{
    if (!qEnvironmentVariableIsSet("EWM_DEV_MODE"))
        return;
    auto *server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    const QString path = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
        + "/ewm-dev.sock";
    // Never unlink a live endpoint belonging to another mapper instance.
    QLocalSocket probe;
    probe.connectToServer(path);
    if (probe.waitForConnected(100)) {
        qWarning() << "[EWM dev] endpoint already active:" << path;
        return;
    }
    QLocalServer::removeServer(path);
    if (!server->listen(path)) {
        qWarning() << "[EWM dev]" << server->errorString();
        return;
    }
    qInfo() << "[EWM dev] listening:" << path;
    connect(server, &QLocalServer::newConnection, this, [server, view] {
        while (server->hasPendingConnections()) {
            auto *socket = server->nextPendingConnection();
            auto *deadline = new QTimer(socket);
            deadline->setSingleShot(true);
            QObject::connect(deadline, &QTimer::timeout, socket, &QLocalSocket::abort);
            deadline->start(3000);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, view] {
                if (socket->bytesAvailable() > 65536) { socket->abort(); return; }
                if (!socket->canReadLine()) return;
                QJsonParseError parse;
                const auto doc = QJsonDocument::fromJson(socket->readLine(), &parse);
                const auto request = doc.object();
                QJsonObject result;
                QString error;
                const auto command = request.value("command").toString();
                if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
                    error = "Expected a JSON object";
                } else if (command == "status") {
                    for (const char *name : {"ready", "busy", "windowVisible", "editMode",
                         "globalCalibration", "calibrationSummary", "autoGeometryActive", "autoGeometryStatus", "autoGeometrySkillIndex", "syntheticTouchActive",
                         "waitingForKey", "profileManagerVisible", "activeProfileId",
                         "activeProfileName", "androidWidth", "androidHeight", "bindings",
                         "mobaSkills", "characterCenter", "mobaMovement", "skillCancel",
                         "profiles", "editorMessage", "cursorStyle", "selectedBindingIndex",
                         "selectedMobaSkillIndex", "calibrationActive", "calibrationStep", "calibrationTotal",
                         "calibrationPointReady", "devWatchFiles", "devWatchDirectories"})
                        result.insert(name, QJsonValue::fromVariant(view->property(name)));
                    result.insert("pid", QCoreApplication::applicationPid());
                    result.insert("requestedQmlUrl", view->mapperOverlayUrl().toString());
                    if (auto *window = view->integratedWindow()) {
                        result.insert("uiRevision", window->property("mapperOverlayRevision").toInt());
                        result.insert("loadedQmlUrl", window->property("mapperOverlaySource").toString());
                    }
                    QFile source(view->mapperOverlayUrl().toLocalFile());
                    if (source.open(QIODevice::ReadOnly))
                        result.insert("diskQmlSha256", QString::fromLatin1(QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256).toHex()));
                    result.insert("surfaceId", QString::number(reinterpret_cast<quintptr>(view->inputSurface_.data()), 16));
                    result.insert("compositorId", QString::number(reinterpret_cast<quintptr>(view->waylandCompositor()), 16));
                } else if (command == "ui") {
                    result = LiveDevUi::execute(qobject_cast<QQuickWindow *>(view->integratedWindow()), request, view->editMode());
                    if (!result.value("ok").toBool()) error = result.value("error").toString();
                } else if (command == "preview-ui") {
                    if (view->busy() || view->ready()) error = "Preview is only available before Android starts";
                    else view->ensureCompositor();
                } else if (command == "reload-ui") {
                    error = view->reloadMapperOverlay();
                } else if (command == "call") {
                    const QByteArray name = request.value("method").toString().toLatin1();
                    // Only mapper editing/profile operations: no session, shell or process APIs.
                    const bool allowed = name == "cancelAutoGeometry"
                        || (!view->autoGeometryActive() && (name == "beginAutoGeometry" || name == "beginUranusCenterSearch" || name == "beginExperimentalCalibration" || name == "setSelectedSkillExperimental" || name == "setSelectedSkillDirectional" || name == "toggleEditMode" || name == "toggleProfileManager"
                        || name == "undoMapperEdit" || name == "cancelKeyCapture"
                        || name == "captureSelectedKey" || name == "selectBinding"
                        || name == "selectMobaSkill" || name == "addTapAt"
                        || name == "moveBinding" || name == "removeBinding"
                        || name == "duplicateBinding" || name == "setCursorStyle"
                        || name.startsWith("setSelectedBinding")
                        || name.startsWith("setSelectedMobaSkill")));
                    if (!allowed) error = "Method is not allowed";
                    else if (name != "setCursorStyle" && name != "toggleEditMode"
                             && name != "toggleProfileManager" && !view->editMode())
                        error = "Open F5 edit mode before editing controls";
                    else if ((name == "toggleEditMode" || name == "toggleProfileManager")
                             && (!view->ready() || !view->windowVisible() || view->busy()))
                        error = "Open Integrated Android before changing mapper modes";
                    else if (view->calibrationActive())
                        error = "Finish calibration before changing mapper controls";
                    else {
                        const auto args = request.value("args").toArray();
                        bool invoked = false;
                        for (int i = 0; i < view->metaObject()->methodCount(); ++i) {
                            const auto method = view->metaObject()->method(i);
                            if (method.name() != name || method.parameterCount() != args.size()
                                || args.size() > 3) continue;
                            QVariant values[3];
                            QGenericArgument arguments[3];
                            bool valid = true;
                            for (int j = 0; j < args.size(); ++j) {
                                values[j] = args[j].toVariant();
                                if (!values[j].convert(method.parameterMetaType(j))) { valid = false; break; }
                                arguments[j] = QGenericArgument(method.parameterMetaType(j).name(), values[j].constData());
                            }
                            if (valid) invoked = method.invoke(view, Qt::DirectConnection,
                                arguments[0], arguments[1], arguments[2]);
                            if (invoked) break;
                        }
                        if (!invoked) error = "Unknown method or invalid arguments";
                    }
                } else error = "Unknown command";
                result.insert("ok", error.isEmpty());
                if (!error.isEmpty()) result.insert("error", error);
                socket->write(QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n');
                socket->disconnectFromServer();
            });
        }
    });
    const QString directory = qEnvironmentVariable("EWM_DEV_QML_DIR");
    if (!directory.isEmpty()) {
        auto *watcher = new QFileSystemWatcher(this);
        auto *debounce = new QTimer(this);
        debounce->setSingleShot(true);
        debounce->setInterval(250);
        watcher->addPath(directory); // Watches atomic-save renames too.
        watcher->addPath(directory + "/MapperOverlay.qml");
        view->setProperty("devWatchFiles", watcher->files());
        view->setProperty("devWatchDirectories", watcher->directories());
        connect(watcher, &QFileSystemWatcher::directoryChanged, debounce, [debounce] { debounce->start(); });
        connect(watcher, &QFileSystemWatcher::fileChanged, debounce, [debounce] { debounce->start(); });
        connect(debounce, &QTimer::timeout, view, [view, watcher, directory, debounce] {
            const auto file = directory + "/MapperOverlay.qml";
            if (!watcher->files().contains(file)) watcher->addPath(file);
            view->setProperty("devWatchFiles", watcher->files());
            view->setProperty("devWatchDirectories", watcher->directories());
            if (view->autoGeometryActive() || view->calibrationActive() || view->waitingForKey() || view->mapperEditActionDepth_ > 0) {
                debounce->start();
                return;
            }
            const auto error = view->reloadMapperOverlay();
            if (!error.isEmpty()) qWarning().noquote() << "[EWM dev] reload rejected:" << error;
        });
    }
}
