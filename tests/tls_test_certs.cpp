// cppcheck-suppress-file missingIncludeSystem
#include "tls_test_certs.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTemporaryDir>

namespace {

// The whole configuration the commands read, given with -config and as
// OPENSSL_CONF, so no system openssl.cnf is needed: the Windows runner's OpenSSL
// may have none where it looks. No authorityKeyIdentifier, which older
// versions cannot write for a self-signed certificate.
const char kConfig[] = R"([req]
distinguished_name = dn
prompt = no

[dn]
CN = FreeTunnel test

[ca]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign, cRLSign
subjectKeyIdentifier = hash

[server]
basicConstraints = critical, CA:FALSE
extendedKeyUsage = serverAuth
subjectKeyIdentifier = hash
subjectAltName = DNS:vpn.test, IP:127.0.0.1

[other]
basicConstraints = critical, CA:FALSE
extendedKeyUsage = serverAuth
subjectKeyIdentifier = hash
subjectAltName = DNS:other.test
)";

class Maker {
public:
    Maker(const QString &openssl, const QTemporaryDir &dir) : m_openssl(openssl), m_dir(dir.path()) {}

    QString path(const QString &name) const { return m_dir.filePath(name); }

    bool run(const QStringList &arguments, QString *error) const
    {
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("OPENSSL_CONF"), path(QStringLiteral("openssl.cnf")));
        process.setProcessEnvironment(environment);
        process.setWorkingDirectory(m_dir.path());
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(m_openssl, arguments);
        const bool finished = process.waitForFinished(120000);
        if (finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0)
            return true;
        *error = QStringLiteral("%1 %2 failed: %3")
                         .arg(m_openssl, arguments.join(QLatin1Char(' ')),
                              QString::fromLocal8Bit(process.readAll()) + process.errorString());
        return false;
    }

    // A new RSA-2048 key, and a certificate for it that `extensions` describes:
    // self-signed, or signed by `issuer` (a name made by this maker) when given.
    bool make(const QString &name, const QString &subject, const QString &extensions, const QString &issuer,
              QString *error) const
    {
        const QStringList common = {QStringLiteral("-newkey"), QStringLiteral("rsa:2048"), QStringLiteral("-nodes"),
                                    QStringLiteral("-keyout"), path(name + QStringLiteral(".key")),
                                    QStringLiteral("-subj"), subject, QStringLiteral("-config"),
                                    path(QStringLiteral("openssl.cnf"))};
        if (issuer.isEmpty())
            return run(QStringList{QStringLiteral("req"), QStringLiteral("-x509"), QStringLiteral("-new"),
                                   QStringLiteral("-sha256"), QStringLiteral("-days"), QStringLiteral("30"),
                                   QStringLiteral("-extensions"), extensions, QStringLiteral("-out"),
                                   path(name + QStringLiteral(".crt"))}
                               + common,
                       error);
        return run(QStringList{QStringLiteral("req"), QStringLiteral("-new"), QStringLiteral("-out"),
                               path(name + QStringLiteral(".csr"))}
                           + common,
                   error)
                && run({QStringLiteral("x509"), QStringLiteral("-req"), QStringLiteral("-in"),
                        path(name + QStringLiteral(".csr")), QStringLiteral("-CA"),
                        path(issuer + QStringLiteral(".crt")), QStringLiteral("-CAkey"),
                        path(issuer + QStringLiteral(".key")), QStringLiteral("-set_serial"),
                        QStringLiteral("2"), QStringLiteral("-sha256"), QStringLiteral("-days"), QStringLiteral("30"),
                        QStringLiteral("-extfile"), path(QStringLiteral("openssl.cnf")), QStringLiteral("-extensions"),
                        extensions, QStringLiteral("-out"), path(name + QStringLiteral(".crt"))},
                       error);
    }

    bool load(const QString &name, TlsTestIdentity *out, QString *error) const
    {
        QFile certificate(path(name + QStringLiteral(".crt")));
        QFile key(path(name + QStringLiteral(".key")));
        if (!certificate.open(QIODevice::ReadOnly) || !key.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("cannot read %1's files").arg(name);
            return false;
        }
        out->pem = certificate.readAll();
        out->certificate = QSslCertificate(out->pem, QSsl::Pem);
        out->key = QSslKey(key.readAll(), QSsl::Rsa, QSsl::Pem);
        if (out->certificate.isNull() || out->key.isNull()) {
            *error = QStringLiteral("Qt cannot read %1's certificate or key").arg(name);
            return false;
        }
        return true;
    }

private:
    QString m_openssl;
    QDir m_dir;
};

} // namespace

bool makeTlsTestCerts(const QString &openssl, TlsTestCerts *out, QString *error)
{
    const QTemporaryDir dir;
    if (!dir.isValid()) {
        *error = QStringLiteral("no temporary directory: %1").arg(dir.errorString());
        return false;
    }
    QFile config(dir.filePath(QStringLiteral("openssl.cnf")));
    if (!config.open(QIODevice::WriteOnly) || config.write(kConfig) != qint64(sizeof kConfig - 1)) {
        *error = QStringLiteral("cannot write openssl.cnf");
        return false;
    }
    config.close();

    const Maker maker(openssl, dir);
    const QString vpn = QStringLiteral("/CN=vpn.test");
    const QString server = QStringLiteral("server");
    return maker.make(QStringLiteral("ca"), QStringLiteral("/CN=FreeTunnel test CA"), QStringLiteral("ca"), QString(),
                      error)
            && maker.make(QStringLiteral("casigned"), vpn, server, QStringLiteral("ca"), error)
            && maker.make(QStringLiteral("selfsigned"), vpn, server, QString(), error)
            && maker.make(QStringLiteral("otherkey"), vpn, server, QString(), error)
            && maker.make(QStringLiteral("othername"), QStringLiteral("/CN=other.test"), QStringLiteral("other"),
                          QString(), error)
            && maker.load(QStringLiteral("ca"), &out->ca, error)
            && maker.load(QStringLiteral("casigned"), &out->caSigned, error)
            && maker.load(QStringLiteral("selfsigned"), &out->selfSigned, error)
            && maker.load(QStringLiteral("otherkey"), &out->otherKey, error)
            && maker.load(QStringLiteral("othername"), &out->otherName, error);
}
