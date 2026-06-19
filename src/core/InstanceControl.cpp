#include "core/InstanceControl.h"

#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QStandardPaths>

namespace freetunnel {

namespace {

QString randomInstanceToken()
{
    return QString::number(QRandomGenerator::system()->generate64(), 16)
            + QString::number(QRandomGenerator::system()->generate64(), 16);
}

} // namespace

QString instanceAuthFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + QStringLiteral("/instance-auth");
}

bool writeInstanceAuthToken(QString *tokenOut)
{
    const QString token = randomInstanceToken();
    const QString path = instanceAuthFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (f.write(token.toUtf8()) != token.size()) {
        f.remove();
        return false;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (tokenOut)
        *tokenOut = token;
    return true;
}

void removeInstanceAuthToken()
{
    QFile::remove(instanceAuthFilePath());
}

bool readInstanceAuthToken(QString *tokenOut)
{
    if (!tokenOut)
        return false;
    QFile f(instanceAuthFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QString token = QString::fromUtf8(f.readAll()).trimmed();
    if (token.isEmpty())
        return false;
    *tokenOut = token;
    return true;
}

QByteArray formatInstanceMessage(const QString &token, const QString &payload)
{
    return token.toUtf8() + '\n' + payload.toUtf8();
}

bool parseInstanceMessage(const QByteArray &data, QString *tokenOut, QString *payloadOut)
{
    if (!tokenOut || !payloadOut)
        return false;
    const int nl = data.indexOf('\n');
    if (nl <= 0)
        return false;
    *tokenOut = QString::fromUtf8(data.left(nl));
    *payloadOut = QString::fromUtf8(data.mid(nl + 1));
    return !tokenOut->isEmpty();
}

bool instanceTokensEqual(const QString &a, const QString &b)
{
    const QByteArray ba = a.toUtf8();
    const QByteArray bb = b.toUtf8();
    if (ba.size() != bb.size())
        return false;
    char diff = 0;
    for (int i = 0; i < ba.size(); ++i)
        diff |= static_cast<char>(ba[i] ^ bb[i]);
    return diff == 0;
}

bool forwardToRunningInstance(const QString &socketName, const QString &controlArg)
{
    QString token;
    if (!readInstanceAuthToken(&token))
        return false;

    QLocalSocket probe;
    probe.connectToServer(socketName);
    if (!probe.waitForConnected(250))
        return false;

    const QString payload = controlArg.isEmpty() ? QStringLiteral("focus") : controlArg;
    const QByteArray msg = formatInstanceMessage(token, payload);
    if (probe.write(msg) != msg.size())
        return false;
    probe.flush();
    probe.waitForBytesWritten(300);
    probe.disconnectFromServer();
    return true;
}

} // namespace freetunnel
