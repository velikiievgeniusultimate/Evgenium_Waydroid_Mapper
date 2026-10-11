#pragma once
#include <QObject>
class IntegratedView;
// Opt-in, same-user development endpoint. Never controls Android lifecycle.
class LiveDev final : public QObject {
public:
    explicit LiveDev(IntegratedView *view);
};
