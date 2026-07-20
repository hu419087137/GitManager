#include "CredentialStore.h"
#include "HostingService.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

namespace Git {

QString CredentialStore::key(HostingProvider provider)
{
    return QStringLiteral("GitManager/Hosting/%1")
        .arg(HostingService::providerName(provider));
}

#ifdef Q_OS_WIN
namespace {
QString winError(DWORD code)
{
    return QStringLiteral("Windows Credential Manager error %1.").arg(code);
}
}
#endif

bool CredentialStore::read(HostingProvider provider, QString* token,
                           QString* error)
{
    if (token) token->clear();
#ifdef Q_OS_WIN
    const QString target = key(provider);
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()),
                   CRED_TYPE_GENERIC, 0, &credential)) {
        const DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND) return true;
        if (error) *error = winError(code);
        return false;
    }
    if (token && credential->CredentialBlob && credential->CredentialBlobSize)
        *token = QString::fromUtf8(
            reinterpret_cast<const char*>(credential->CredentialBlob),
            static_cast<int>(credential->CredentialBlobSize));
    CredFree(credential);
    return true;
#else
    Q_UNUSED(provider);
    if (error) *error = QStringLiteral("Secure credential storage is only supported on Windows.");
    return false;
#endif
}

bool CredentialStore::write(HostingProvider provider, const QString& token,
                            QString* error)
{
    if (token.isEmpty()) return remove(provider, error);
#ifdef Q_OS_WIN
    QByteArray bytes = token.toUtf8();
    if (bytes.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        bytes.fill('\0');
        if (error) *error = QStringLiteral("Access token is too large for Windows Credential Manager.");
        return false;
    }
    const QString target = key(provider);
    QString username = HostingService::providerName(provider);
    CREDENTIALW credential = {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = reinterpret_cast<LPWSTR>(const_cast<ushort*>(target.utf16()));
    credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(bytes.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = reinterpret_cast<LPWSTR>(username.data());
    const BOOL written = CredWriteW(&credential, 0);
    const DWORD writeError = written ? ERROR_SUCCESS : GetLastError();
    bytes.fill('\0');
    if (!written) {
        if (error) *error = winError(writeError);
        return false;
    }
    return true;
#else
    Q_UNUSED(provider);
    if (error) *error = QStringLiteral("Secure credential storage is only supported on Windows.");
    return false;
#endif
}

bool CredentialStore::remove(HostingProvider provider, QString* error)
{
#ifdef Q_OS_WIN
    const QString target = key(provider);
    if (CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()),
                    CRED_TYPE_GENERIC, 0)) return true;
    const DWORD code = GetLastError();
    if (code == ERROR_NOT_FOUND) return true;
    if (error) *error = winError(code);
    return false;
#else
    Q_UNUSED(provider);
    if (error) *error = QStringLiteral("Secure credential storage is only supported on Windows.");
    return false;
#endif
}

} // namespace Git
