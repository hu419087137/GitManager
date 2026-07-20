#ifndef HOOKSERVICE_H
#define HOOKSERVICE_H
#include "GitTypes.h"
namespace Git {
class HookService {
public:
    explicit HookService(QString repositoryPath);
    QVector<HookInfo> hooks(QString* error = nullptr) const;
    HookResult run(const QString& name) const;
private:
    QString hooksPath(QString* error = nullptr) const;
    QString _repositoryPath;
};
} // namespace Git
#endif // HOOKSERVICE_H
