#include "MainWindow.h"
#include <QApplication>
#include <QFont>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    app.setOrganizationName("GitManager");
    app.setApplicationName("GitManager");
    app.setApplicationDisplayName("Git Manager");
    app.setApplicationVersion("1.0.0");

    QFont font = app.font();
    font.setFamily("Microsoft YaHei");
    font.setPointSize(10);
    app.setFont(font);

    MainWindow window;
    window.show();

    return app.exec();
}
