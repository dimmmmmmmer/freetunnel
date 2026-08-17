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
    // Zero-initialised first, then filled: SecretSchema ends with eight reserved
    // members that libsecret requires to be zero and that callers are not meant to
    // name. Listing only the fields we set makes -Wmissing-field-initializers
    // complain about every one of them, and naming them would bind this code to a
    // struct layout libsecret explicitly keeps to itself.
    static const SecretSchema schema = [] {
        SecretSchema s{};
        s.name = name.constData();
        s.flags = SECRET_SCHEMA_NONE;
        s.attributes[0] = {"account", SECRET_SCHEMA_ATTRIBUTE_STRING};
        s.attributes[1] = {nullptr, SecretSchemaAttributeType(0)};
        return s;
    }();
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
