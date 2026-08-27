#include "MainWindow.h"
#include "AppLog.h"
#include "IntegratedView.h"
#include "WaydroidVersionManager.h"

#include <QApplication>
#include <QDebug>
#include <QKeySequence>
#include <QShortcut>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("Evgenium Waydroid Mapper");
    QApplication::setApplicationDisplayName("EWM");
    QApplication::setOrganizationName("Evgenium");
    QApplication::setDesktopFileName("evgenium-waydroid-mapper");
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

    window.show();
    return application.exec();
}
