#pragma once

#include <QRect>
#include <QString>

#include <functional>
#include <memory>

class QWindow;

class WaylandPointerConfiner final
{
public:
    using StateCallback = std::function<void(bool, const QString &)>;

    WaylandPointerConfiner();
    ~WaylandPointerConfiner();

    WaylandPointerConfiner(const WaylandPointerConfiner &) = delete;
    WaylandPointerConfiner &operator=(const WaylandPointerConfiner &) = delete;

    bool confine(QWindow *window, const QRect &region,
                 const StateCallback &stateCallback);
    bool updateRegion(QWindow *window, const QRect &region);
    void release();
    bool requested() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
