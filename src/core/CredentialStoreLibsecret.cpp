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

namespace {

// What libsecret said about an error, for the log: its words, and the domain and
// code to look it up by. Frees the error.
QString describeGError(GError *error)
{
    const QString text = QStringLiteral("libsecret: %1 (%2, code %3)")
                                 .arg(QString::fromUtf8(error->message),
                                      QString::fromUtf8(g_quark_to_string(error->domain)))
                                 .arg(error->code);
    g_error_free(error);
    return text;
}

// secret_password_search_sync() with SECRET_SEARCH_ALL needs libsecret 0.19: the
// header's SECRET_CHECK_VERSION says so, and so does the build's own check of the
// version pkg-config found (FT_LIBSECRET_HAS_SEARCH, cmake/FreetunnelDeps.cmake),
// for a header that does not pull the macro in.
#if defined(FT_LIBSECRET_HAS_SEARCH)
#define FT_LIBSECRET_CAN_SEARCH 1
#elif defined(SECRET_CHECK_VERSION)
#if SECRET_CHECK_VERSION(0, 19, 0)
#define FT_LIBSECRET_CAN_SEARCH 1
#endif
#endif

#if defined(FT_LIBSECRET_CAN_SEARCH)
// The attributes secret-tool stores an item under: service and account, with no
// schema of ours (a name to match would be the one thing they do not have).
// Search-only: nothing is stored with it.
const SecretSchema &secretToolSchema()
{
    static const QByteArray name = credentialServiceName().toUtf8();
    static const SecretSchema schema = [] {
        SecretSchema s{};
        s.name = name.constData();
        s.flags = SECRET_SCHEMA_DONT_MATCH_NAME;
        s.attributes[0] = {"service", SECRET_SCHEMA_ATTRIBUTE_STRING};
        s.attributes[1] = {"account", SECRET_SCHEMA_ATTRIBUTE_STRING};
        s.attributes[2] = {nullptr, SecretSchemaAttributeType(0)};
        return s;
    }();
    return schema;
}
#endif

// libsecret gives NULL without an error both for an item that is not there and for
// one that is there and locked, when the prompt to unlock it was dismissed. A
// search that is not allowed to unlock anything cannot prompt, and still sees a
// locked item: any match means the item exists and could not be read.
StoredSecret nothingWasReturned(const QString &key)
{
    StoredSecret out; // NotFound
#if defined(FT_LIBSECRET_CAN_SEARCH)
    GError *error = nullptr;
    GList *matches = secret_password_search_sync(&libsecretSchema(), SECRET_SEARCH_ALL, nullptr,
                                                 &error, "account", key.toUtf8().constData(),
                                                 nullptr);
    if (error) {
        out.status = SecretStatus::Error;
        out.detail = describeGError(error) + QStringLiteral(" (looking for a locked item)");
        return out;
    }
    if (matches) {
        out.status = SecretStatus::Error;
        out.detail = QStringLiteral("libsecret: the item is there and could not be unlocked");
        g_list_free_full(matches, g_object_unref);
    }
#else
    // Older than that, the two cannot be told apart, and a locked keyring reads
    // as an item that is not there, as it always has.
    Q_UNUSED(key);
#endif
    return out;
}

} // namespace

StoredSecret libsecretLookup(const QString &key)
{
    StoredSecret out;
    GError *error = nullptr;
    gchar *pw = secret_password_lookup_sync(&libsecretSchema(), nullptr, &error, "account",
                                            key.toUtf8().constData(), nullptr);
    if (error) {
        out.status = SecretStatus::Error;
        out.detail = describeGError(error);
        return out;
    }
    if (!pw)
        return nothingWasReturned(key);
    out.status = SecretStatus::Found;
    out.value = QString::fromUtf8(pw);
    secret_password_free(pw);
    return out;
}

std::optional<StoredSecret> libsecretToolItemsLookup(const QString &key)
{
#if defined(FT_LIBSECRET_CAN_SEARCH)
    StoredSecret out; // NotFound
    const QByteArray service = credentialServiceName().toUtf8();
    GError *error = nullptr;
    GList *matches = secret_password_search_sync(&secretToolSchema(), SECRET_SEARCH_ALL, nullptr,
                                                 &error, "service", service.constData(), "account",
                                                 key.toUtf8().constData(), nullptr);
    if (error) {
        out.status = SecretStatus::Error;
        out.detail = describeGError(error) + QStringLiteral(" (looking for an item secret-tool stored)");
        return out;
    }
    if (matches) {
        out.status = SecretStatus::Error;
        out.detail = QStringLiteral("libsecret: an item secret-tool stored is there and could not be read");
        g_list_free_full(matches, g_object_unref);
    }
    return out;
#else
    Q_UNUSED(key);
    return std::nullopt;
#endif
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
