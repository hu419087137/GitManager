#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QByteArray>

class QMainWindow;

namespace Git {

class AppSettings {
public:
    static void restoreWindow(QMainWindow* window);
    static void saveWindow(const QMainWindow* window);
    static void resetWindowLayout();
};

} // namespace Git

#endif // APPSETTINGS_H
