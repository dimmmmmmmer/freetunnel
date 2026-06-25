// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>

namespace freetunnel {

#if defined(FT_HAVE_LIBSECRET)
bool libsecretStore(const QString &key, const QString &password);
QString libsecretLookup(const QString &key, bool *ok);
bool libsecretClear(const QString &key);
#endif

} // namespace freetunnel
