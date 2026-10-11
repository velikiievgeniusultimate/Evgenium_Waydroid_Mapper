#pragma once
#include <QJsonObject>
class QQuickWindow;
namespace LiveDevUi {
QJsonObject execute(QQuickWindow *window, const QJsonObject &request, bool editing);
}
