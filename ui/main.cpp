#include <QApplication>
#include <QStyleFactory>
#include <QFile>
#include "mainwindow.h"
#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char *argv[]) {
#ifdef _WIN32
    FreeConsole();
#endif
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/MZLZ.gif"));

    app.setApplicationName("Excel Transfer Tool");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("MZLZ Finance");

    app.setStyle(QStyleFactory::create("Fusion"));

    // Load stylesheet
    QFile styleFile(":/styles.qss");
    if (styleFile.exists()) {
        if (styleFile.open(QFile::ReadOnly)) {
            QString styleSheet = QLatin1String(styleFile.readAll());
            app.setStyleSheet(styleSheet);
        }
    }

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}