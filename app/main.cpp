#include "MainWindow.h"
#include "AppLog.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("Evgenium Waydroid Mapper");
    QApplication::setOrganizationName("Evgenium");
    AppLog::install();
    MainWindow window;
    window.show();
    return application.exec();
}
