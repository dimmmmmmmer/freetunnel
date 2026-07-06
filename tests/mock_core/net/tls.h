// cppcheck-suppress-file missingIncludeSystem
#pragma once

namespace ag {

inline const char *tls_verify_cert(const char *, const char *, void *)
{
    return nullptr; // mock: every certificate verifies
}

} // namespace ag
