#if defined(FT_HAVE_LIBSECRET)

// libsecret/GLib headers must be included before any Qt header: Qt defines a
// `signals` macro that breaks glib's struct fields named `signals`.
#include <libsecret/secret.h>

#include "core/CredentialStoreLibsecret.h"

namespace freetunnel {

namespace {

const SecretSchema kLibsecretSchema = {
    "com.freetunnel.app",
    SECRET_SCHEMA_NONE,
    {
        {"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {nullptr, SecretSchemaAttributeType(0)},
    },
};

} // namespace

bool libsecretStore(const QString &key, const QString &password)
{
    GError *error = nullptr;
    const bool ok = secret_password_store_sync(
            &kLibsecretSchema, SECRET_COLLECTION_DEFAULT, "FreeTunnel",
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
    gchar *pw = secret_password_lookup_sync(&kLibsecretSchema, nullptr, &error, "account",
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
    const bool ok = secret_password_clear_sync(&kLibsecretSchema, nullptr, &error, "account",
                                               key.toUtf8().constData(), nullptr);
    if (error)
        g_error_free(error);
    return ok;
}

} // namespace freetunnel

#endif
