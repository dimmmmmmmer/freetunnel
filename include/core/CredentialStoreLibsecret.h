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
// Whether an item that secret-tool stored for key (attributes service and account,
// and no schema of libsecret's: LinuxLookupSources::secretToolItems) is there,
// found by a search that cannot prompt and so sees a locked item too. NotFound:
// none is. Error: one is, or the search failed. Empty when this libsecret is older
// than 0.19 and cannot search.
std::optional<StoredSecret> libsecretToolItemsLookup(const QString &key);
#endif

} // namespace freetunnel
