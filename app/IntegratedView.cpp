#include "IntegratedView.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QWindow>

namespace {
constexpr auto NestedSocket = "evgenium-wayland-0";
}

IntegratedView::IntegratedView(QObject *parent)
    : QObject(parent), engine_(new QQmlApplicationEngine(this)), process_(new QProcess(this))
{
    engine_->rootContext()->setContextProperty("integratedBackend", this);
}

IntegratedView::~IntegratedView() = default;

void IntegratedView::showAndStart()
{
    ensureWindow();
    restartAndroid();
}

void IntegratedView::ensureWindow()
{
    if (engine_->rootObjects().isEmpty())
        engine_->load(QUrl(QStringLiteral("qrc:/IntegratedView.qml")));

    for (QObject *object : engine_->rootObjects()) {
        if (auto *window = qobject_cast<QWindow *>(object)) {
            window->show();
            window->raise();
            window->requestActivate();
        }
    }
}

QProcessEnvironment IntegratedView::nestedEnvironment() const
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("WAYLAND_DISPLAY", NestedSocket);
    return environment;
}

void IntegratedView::restartAndroid()
{
    if (process_->state() != QProcess::NotRunning)
        return;

    emit statusChanged("Restarting the Waydroid session for the integrated display…");
    stoppingForRestart_ = true;
    process_->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    process_->start("waydroid", {"session", "stop"});
    connect(process_, &QProcess::finished, this, [this] {
        if (!stoppingForRestart_)
            return;
        stoppingForRestart_ = false;
        QTimer::singleShot(400, this, &IntegratedView::startSession);
    }, Qt::SingleShotConnection);
}

void IntegratedView::startSession()
{
    emit statusChanged("Starting Waydroid on evgenium-wayland-0…");
    process_->setProcessEnvironment(nestedEnvironment());
    process_->start("waydroid", {"session", "start"});
    QTimer::singleShot(1800, this, &IntegratedView::showFullUi);
}

void IntegratedView::showFullUi()
{
    emit statusChanged("Requesting the Android surface…");
    auto *showProcess = new QProcess(this);
    showProcess->setProcessEnvironment(nestedEnvironment());
    connect(showProcess, &QProcess::finished, showProcess, &QObject::deleteLater);
    showProcess->start("waydroid", {"show-full-ui"});
}

void IntegratedView::stopIntegratedSession()
{
    stoppingForRestart_ = false;
    QProcess::startDetached("waydroid", {"session", "stop"});
    emit statusChanged("Waydroid session stopped.");
}

void IntegratedView::applyResolution(int width, int height)
{
    if (width < 320 || height < 320 || width > 7680 || height > 7680) {
        emit statusChanged("Resolution is outside the supported range.");
        return;
    }

    emit statusChanged(QString("Applying Android resolution %1 × %2…")
                           .arg(width).arg(height));

    auto *widthProcess = new QProcess(this);
    connect(widthProcess, &QProcess::finished, this,
            [this, widthProcess, height](int widthExitCode) {
        widthProcess->deleteLater();
        if (widthExitCode != 0) {
            emit statusChanged("Failed to set Waydroid width.");
            return;
        }

        auto *heightProcess = new QProcess(this);
        connect(heightProcess, &QProcess::finished, this,
                [this, heightProcess](int heightExitCode) {
            heightProcess->deleteLater();
            if (heightExitCode != 0) {
                emit statusChanged("Failed to set Waydroid height.");
                return;
            }
            restartAndroid();
        });
        heightProcess->start("waydroid",
                             {"prop", "set", "persist.waydroid.height",
                              QString::number(height)});
    });
    widthProcess->start("waydroid",
                        {"prop", "set", "persist.waydroid.width",
                         QString::number(width)});
}
