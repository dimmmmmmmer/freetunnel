// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>

#include "core/CredentialStore.h"

namespace freetunnel {

#if defined(FT_HAVE_LIBSECRET)
bool libsecretStore(const QString &key, const QString &password);
// Found, NotFound or Error; see combineLinuxLookup() for how it is added to the rest.
StoredSecret libsecretLookup(const QString &key);
bool libsecretClear(const QString &key);
#endif

} // namespace freetunnel
