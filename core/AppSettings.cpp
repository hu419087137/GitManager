#include "AppSettings.h"
#include <QMainWindow>
#include <QSettings>
#include <QSplitter>

namespace Git {
namespace {
QSettings settings()
{
    return QSettings(QStringLiteral("GitManager"), QStringLiteral("GitManager"));
}
}

void AppSettings::restoreWindow(QMainWindow* window)
{
    if (!window) return;
    const QSettings store = settings();
    const QByteArray geometry = store.value(QStringLiteral("window/geometry")).toByteArray();
    const QByteArray state = store.value(QStringLiteral("window/state")).toByteArray();
    if (!geometry.isEmpty()) window->restoreGeometry(geometry);
    if (!state.isEmpty()) window->restoreState(state);
    for (QSplitter* splitter : window->findChildren<QSplitter*>()) {
        if (splitter->objectName().isEmpty()) continue;
        const QByteArray splitterState = store.value(
            QStringLiteral("window/splitters/%1").arg(splitter->objectName())).toByteArray();
        if (!splitterState.isEmpty()) splitter->restoreState(splitterState);
    }
    window->setProperty("activePanel", store.value(QStringLiteral("window/activePanel"), 0));
}

void AppSettings::saveWindow(const QMainWindow* window)
{
    if (!window) return;
    QSettings store = settings();
    store.setValue(QStringLiteral("window/geometry"), window->saveGeometry());
    store.setValue(QStringLiteral("window/state"), window->saveState());
    store.setValue(QStringLiteral("window/activePanel"), window->property("activePanel"));
    for (QSplitter* splitter : window->findChildren<QSplitter*>()) {
        if (!splitter->objectName().isEmpty()) {
            store.setValue(QStringLiteral("window/splitters/%1").arg(splitter->objectName()),
                           splitter->saveState());
        }
    }
    store.sync();
}

void AppSettings::resetWindowLayout()
{
    QSettings store = settings();
    store.remove(QStringLiteral("window"));
    store.sync();
}
} // namespace Git
