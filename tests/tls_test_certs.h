// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Certificates for TLS tests, made when the test runs by the openssl command, so
// the tree holds no private key. RSA-2048 throughout: under schannel a debug Qt
// asserts on an EC key in server mode, and Secure Transport cannot import one
// for a server. Each server certificate names vpn.test and 127.0.0.1 (but
// otherName) and is for TLS servers, valid for 30 days, as macOS asks of one.

#include <QByteArray>
#include <QSslCertificate>
#include <QSslKey>
#include <QString>

struct TlsTestIdentity {
    QSslCertificate certificate;
    QSslKey key;
    QByteArray pem; // the certificate's PEM, as a config's certificate holds it
};

struct TlsTestCerts {
    TlsTestIdentity ca;         // a CA, which signs caSigned
    TlsTestIdentity caSigned;   // vpn.test, signed by ca
    TlsTestIdentity selfSigned; // vpn.test, self-signed, as an endpoint's often is
    TlsTestIdentity otherKey;   // the same names as selfSigned, with another key
    TlsTestIdentity otherName;  // other.test only, self-signed
};

// Makes them with the openssl command at `openssl`, in a temporary directory that
// is gone again when this returns, or says in *error why it could not.
bool makeTlsTestCerts(const QString &openssl, TlsTestCerts *out, QString *error);
