// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include "mock_core_controller.h"

namespace ag {

// Delegates to mockcore::Controller so a test can script a bad chain.
//
// This used to return nullptr unconditionally — every certificate verified — and
// that made qt_trusttunnel_verify_server_certificate's reject branch
// (event->result = -1) unreachable under test. With the reject branch dead, the
// verifier itself was dead weight the suite could not miss: deleting the
// verify_handler assignment in makeCallbacks() hands the tunnel's TLS to whoever
// answers, and every test still passed.
inline const char *tls_verify_cert(const char *cert, const char *chain, void *)
{
    return mockcore::Controller::instance().verifyCertificate(cert, chain);
}

} // namespace ag
