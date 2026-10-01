// Must NOT compile: the releaseverify_requires_openssl test passes only when
// building this stops at ReleaseVerify.cpp's #error.
//
// It is ReleaseVerify.cpp as the app compiles it, told that OpenSSL is linked
// (FT_REQUIRE_OPENSSL), on a compiler that cannot see <openssl/evp.h>: a release
// build whose OpenSSL headers went missing. Compiled on regardless, that build
// shipped an updater that verifies nothing and rejects every release as
// unsigned, and the tests that would have noticed skip themselves for want of
// the same headers. The build has to stop instead.
#define FT_REQUIRE_OPENSSL
#include "../src/core/ReleaseVerify.cpp"
