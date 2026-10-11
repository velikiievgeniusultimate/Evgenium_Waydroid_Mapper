#include "MainWindow.h"
#include "AppLog.h"
#include "IntegratedView.h"
#include "WaydroidVersionManager.h"

#include <QApplication>
#include <QDebug>
#include <QKeySequence>
#include <QShortcut>
#include <QSettings>
#include <QStandardPaths>
#include <QFileInfo>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("Evgenium Waydroid Mapper");
    QApplication::setApplicationDisplayName("EWM");
    QApplication::setOrganizationName("Evgenium");
    QApplication::setDesktopFileName("evgenium-waydroid-mapper");
    // Local developer preferences survive installation/release replacement and
    // work with the ordinary desktop launcher. Released builds remain opt-in.
    QSettings development(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + "/Evgenium/ewm-development.ini", QSettings::IniFormat);
    if (!qEnvironmentVariableIsSet("EWM_DEV_MODE")
        && development.value("development/enabled", false).toBool()) {
        qputenv("EWM_DEV_MODE", "1");
        const auto directory = development.value("development/qmlDirectory").toString();
        if (!qEnvironmentVariableIsSet("EWM_DEV_QML_DIR")
            && QFileInfo::exists(directory + "/MapperOverlay.qml"))
            qputenv("EWM_DEV_QML_DIR", directory.toUtf8());
    }
    AppLog::install();
    MainWindow window;
    auto *versionManager = new WaydroidVersionManager(&window);
    versionManager->attachToSettingsMenu();

    // ShellSurfaceItem can own keyboard focus while the Android surface is in
    // gameplay. Capture F12 at application-shortcut level so the nested
    // Wayland client cannot consume the cursor-lock hotkey before EWM sees it.
    auto *cursorLockShortcut = new QShortcut(QKeySequence(Qt::Key_F12), &window);
    cursorLockShortcut->setContext(Qt::ApplicationShortcut);
    cursorLockShortcut->setAutoRepeat(false);
    QObject::connect(cursorLockShortcut, &QShortcut::activated, &window, [&window] {
        qInfo().noquote() << "[EWM] F12 application shortcut activated";
        if (auto *integratedView = window.findChild<IntegratedView *>())
            integratedView->toggleCursorLock();
        else
            qWarning().noquote() << "[EWM] F12 could not find IntegratedView";
    });

    if (qEnvironmentVariableIsSet("EWM_DEV_MODE")) {
        auto *reload = new QShortcut(QKeySequence("Ctrl+Shift+R"), &window);
        reload->setContext(Qt::ApplicationShortcut);
        reload->setAutoRepeat(false);
        QObject::connect(reload, &QShortcut::activated, &window, [&window] {
            if (auto *view = window.findChild<IntegratedView *>()) {
                const auto error = view->reloadMapperOverlay();
                if (!error.isEmpty()) qWarning().noquote() << "[EWM dev]" << error;
            }
        });
    }
    window.show();
    return application.exec();
}
