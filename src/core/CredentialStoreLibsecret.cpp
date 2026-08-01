// cppcheck-suppress-file missingIncludeSystem
#if defined(FT_HAVE_LIBSECRET)

// libsecret/GLib headers must be included before any Qt header: Qt defines a
// `signals` macro that breaks glib's struct fields named `signals`.
#include <libsecret/secret.h>

#include "core/CredentialStore.h"
#include "core/CredentialStoreLibsecret.h"

namespace freetunnel {

namespace {

// Schema name doubles as the isolation boundary: test builds resolve
// credentialServiceName() to a separate name so a run can't touch real secrets.
const SecretSchema &libsecretSchema()
{
    static const QByteArray name = credentialServiceName().toUtf8();
    static const SecretSchema schema = {
        name.constData(),
        SECRET_SCHEMA_NONE,
        {
            {"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {nullptr, SecretSchemaAttributeType(0)},
        },
    };
    return schema;
}

} // namespace

bool libsecretStore(const QString &key, const QString &password)
{
    GError *error = nullptr;
    const bool ok = secret_password_store_sync(
            &libsecretSchema(), SECRET_COLLECTION_DEFAULT, "FreeTunnel",
            password.toUtf8().constData(), nullptr, &error, "account",
            key.toUtf8().constData(), nullptr);
    if (error)
        g_error_free(error);
    return ok;
}

QString libsecretLookup(const QString &key, bool *ok)
{
    *ok = false;
    GError *error = nullptr;
    gchar *pw = secret_password_lookup_sync(&libsecretSchema(), nullptr, &error, "account",
                                            key.toUtf8().constData(), nullptr);
    if (error) {
        g_error_free(error);
        return QString();
    }
    if (!pw)
        return QString();
    *ok = true;
    const QString out = QString::fromUtf8(pw);
    secret_password_free(pw);
    return out;
}

bool libsecretClear(const QString &key)
{
    GError *error = nullptr;
    const bool ok = secret_password_clear_sync(&libsecretSchema(), nullptr, &error, "account",
                                               key.toUtf8().constData(), nullptr);
    if (error)
        g_error_free(error);
    return ok;
}

} // namespace freetunnel

#endif
