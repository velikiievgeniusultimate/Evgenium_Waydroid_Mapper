#include "LiveDevUi.h"
#include <QQuickWindow>
#include <QQuickItem>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QJsonArray>
#include <QStandardPaths>
#include <QImage>
#include <QWheelEvent>

namespace {
QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name && root->isVisible()) return root;
    for (auto *child : root->childItems())
        if (auto *found = findItem(child, name)) return found;
    return nullptr;
}
void collect(QQuickItem *root, QJsonArray &items)
{
    if (!root->objectName().isEmpty()) {
        const auto rect = root->mapRectToScene(root->boundingRect());
        QJsonObject item{{"name", root->objectName()}, {"visible", root->isVisible()},
            {"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()}};
        for (const char *name : {"text", "shape", "selected", "keyText", "typeText", "labelAbove", "settingsOnLeft"}) {
            const auto value = root->property(name);
            if (value.isValid()) item.insert(name, QJsonValue::fromVariant(value));
        }
        items.append(item);
    }
    for (auto *child : root->childItems()) collect(child, items);
}
void mouse(QQuickWindow *window, QEvent::Type type, QPointF point,
           Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, point, point, window->mapToGlobal(point.toPoint()), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &event);
}
}
QJsonObject LiveDevUi::execute(QQuickWindow *window, const QJsonObject &request, bool editing)
{
    if (!window) return {{"ok", false}, {"error", "Integrated window is unavailable"}};
    const auto action = request.value("action").toString();
    const auto options = request.value("options").toObject();
    if (action == "tree") {
        QJsonArray items;
        collect(window->contentItem(), items);
        return {{"ok", true}, {"items", items}, {"width", window->width()}, {"height", window->height()}};
    }
    if (action == "screenshot") {
        const auto path = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/ewm-dev-window.png";
        if (!window->grabWindow().save(path)) return {{"ok", false}, {"error", "Could not capture the window"}};
        return {{"ok", true}, {"path", path}};
    }
    if (!window->isVisible()) return {{"ok", false}, {"error", "Open Integrated Android first"}};
    if (action == "key") {
        // Limited to mapper shortcuts; no arbitrary input into the Android game.
        const auto name = options.value("key").toString();
        int key = name == "Escape" ? Qt::Key_Escape : name == "F5" ? Qt::Key_F5 : name == "F6" ? Qt::Key_F6 : 0;
        if (!key) return {{"ok", false}, {"error", "Supported keys: F5, F6, Escape"}};
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QCoreApplication::sendEvent(window, &release);
    } else if (action == "resize") {
        const int width = options.value("width").toInt();
        const int height = options.value("height").toInt();
        if (width < 640 || height < 480 || width > 7680 || height > 4320)
            return {{"ok", false}, {"error", "Window size is out of range"}};
        window->resize(width, height);
    } else if (action == "scroll") {
        if (!editing) return {{"ok", false}, {"error", "Open F5 before UI pointer testing"}};
        const QPointF point(options.value("x").toDouble(window->width() / 2.0),
                            options.value("y").toDouble(window->height() / 2.0));
        QWheelEvent event(point, window->mapToGlobal(point.toPoint()), {},
            QPoint(0, options.value("delta").toInt(-720)), Qt::NoButton, Qt::NoModifier,
            Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &event);
    } else if (action == "click" || action == "drag") {
        if (!editing) return {{"ok", false}, {"error", "Open F5 before UI pointer testing"}};
        auto *item = findItem(window->contentItem(), options.value("target").toString());
        if (!item || item->objectName().isEmpty()) return {{"ok", false}, {"error", "Visible target was not found"}};
        const auto point = item->mapToScene(QPointF(item->width() * options.value("rx").toDouble(.5),
                                                  item->height() * options.value("ry").toDouble(.5)));
        mouse(window, QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
        mouse(window, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
        QPointF end = point;
        if (action == "drag") {
            const QPointF delta(options.value("dx").toDouble(), options.value("dy").toDouble());
            for (int i = 1; i <= 6; ++i) {
                end = point + delta * (i / 6.0);
                mouse(window, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
            }
        }
        mouse(window, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    } else return {{"ok", false}, {"error", "Unknown UI action"}};
    return {{"ok", true}};
}
