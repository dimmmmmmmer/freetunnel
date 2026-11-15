# Installed helper test keys

This Ed25519 key pair is a public test fixture generated for the installed-helper
tests. It has never signed a FreeTunnel release. Only targets built with both
`FT_ENABLE_TEST_HOOKS` and `FT_INSTALLED_HELPER_TEST_KEY` accept it; production
uses the release public key in `include/core/ReleaseSigning.h`.
