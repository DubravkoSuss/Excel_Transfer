#ifndef CRASHGUARD_H
#define CRASHGUARD_H

#include <QString>
#include <exception>

namespace CrashGuard {

inline QString currentExceptionText()
{
    try {
        throw;
    } catch (const std::exception& e) {
        return QString::fromUtf8(e.what());
    } catch (...) {
        return QStringLiteral("Unknown non-std exception");
    }
}

inline QString format(const QString& where, const QString& detail = QString())
{
    if (detail.isEmpty()) {
        return QStringLiteral("[%1] %2").arg(where, currentExceptionText());
    }
    return QStringLiteral("[%1] %2: %3").arg(where, detail, currentExceptionText());
}

} // namespace CrashGuard

#endif // CRASHGUARD_H
