// cppcheck-suppress-file missingIncludeSystem
#include "core/BypassRules.h"

#include <QHostAddress>
#include <QRegularExpression>
#include <QUrl>

// A /0 is a subnet, but it is every address of its kind. As a rule it took all
// of that traffic out of the tunnel under "Bypass VPN", with the window still
// saying Connected, as an excluded route of every address did (Settings refuses
// that one too); the core takes it, so it is refused here, and one saved by an
// earlier version is dropped like any rule the core cannot use.
static bool isValidIpBypassRule(const QString &rule)
{
    const int slash = rule.indexOf(QLatin1Char('/'));
    const QString addr = slash >= 0 ? rule.left(slash) : rule;
    if (QHostAddress(addr).isNull())
        return false;
    if (slash < 0)
        return true;
    bool ok = false;
    const int p = rule.mid(slash + 1).toInt(&ok);
    const int max = addr.contains(QLatin1Char(':')) ? 128 : 32;
    return ok && p > 0 && p <= max;
}

static bool isValidDomainBypassRule(const QString &rule)
{
    // The last label is letters, or the ASCII spelling of one that is not:
    // .рф is xn--p1ai, and a domain copied out of an address bar, a log or a
    // certificate comes in that spelling. Letters alone turned it away, while
    // the very same domain typed in Cyrillic was accepted and converted.
    static const QRegularExpression fqdn(
        QStringLiteral("^(?=.{1,253}$)([\\p{L}\\p{N}]([\\p{L}\\p{N}-]{0,61}[\\p{L}\\p{N}])?\\.)+"
                       "([\\p{L}]{2,63}|[xX][nN]--[a-zA-Z0-9]([a-zA-Z0-9-]{0,57}[a-zA-Z0-9])?)$"),
        QRegularExpression::UseUnicodePropertiesOption);
    return fqdn.match(rule).hasMatch();
}

bool isValidBypassRule(const QString &rule)
{
    QString r = rule;
    bool wildcard = false;
    if (r.startsWith(QLatin1String("*."))) {
        r = r.mid(2);
        wildcard = true;
    } else if (r.startsWith(QLatin1Char('.'))) {
        r = r.mid(1);
        wildcard = true;
    }
    if (r.isEmpty())
        return false;
    // A wildcard belongs to a name, and the core reads it that way: "*.1.2.3.4"
    // is the subdomains of a domain called 1.2.3.4, which nothing is, and
    // ".1.2.3.4" and "*.10.0.0.0/8" are thrown away as malformed. Accepted here,
    // each was listed back to the user and matched nothing — and as the only
    // rule in "Through VPN" it counted as one, so nothing went through the
    // tunnel at all.
    if (isValidIpBypassRule(r))
        return !wildcard;
    return isValidDomainBypassRule(r);
}

bool isEveryAddressRule(const QString &rule)
{
    const QString r = rule.trimmed();
    const qsizetype slash = r.indexOf(QLatin1Char('/'));
    bool ok = false;
    return slash > 0 && r.mid(slash + 1).toInt(&ok) == 0 && ok && !QHostAddress(r.left(slash)).isNull();
}

bool isWildcardAddressRule(const QString &rule)
{
    const QString r = rule.trimmed();
    const qsizetype prefix = r.startsWith(QLatin1String("*.")) ? 2 : r.startsWith(QLatin1Char('.')) ? 1 : 0;
    return prefix > 0 && isValidIpBypassRule(r.mid(prefix));
}

static QString punycodeHost(const QString &host)
{
    const QByteArray ace = QUrl::toAce(host);
    return ace.isEmpty() ? host : QString::fromLatin1(ace);
}

QString coreBypassRuleFor(const QString &rule)
{
    QString r = rule.trimmed();
    if (r.isEmpty())
        return {};
    bool wild = false;
    if (r.startsWith(QLatin1String("*."))) {
        wild = true;
        r = r.mid(2);
    } else if (r.startsWith(QLatin1Char('.'))) {
        // ".example.com" means the same as "*.example.com" — isValidBypassRule()
        // has always accepted both and stripped either prefix the same way. This
        // branch used to drop the rule instead, so a rule written that way was
        // accepted by the UI, stored, listed back to the user, and silently never
        // reached the core: in bypass mode the traffic it named went through the
        // tunnel anyway, and in "Through VPN" mode it did not go through at all.
        wild = true;
        r = r.mid(1);
    }
    if (isValidIpBypassRule(r))
        return wild ? QString() : rule.trimmed(); // see isValidBypassRule()
    if (!r.contains(QLatin1Char('.')))
        return {};
    if (!isValidDomainBypassRule(r))
        return {};
    const QString ascii = punycodeHost(r);
    return wild ? QStringLiteral("*.") + ascii : ascii;
}

QStringList sanitizedBypassRules(const QStringList &rules)
{
    QStringList out;
    out.reserve(rules.size());
    for (const QString &rule : rules) {
        const QString trimmed = rule.trimmed();
        if (trimmed.isEmpty() || coreBypassRuleFor(trimmed).isEmpty() || out.contains(trimmed))
            continue;
        out << trimmed;
    }
    return out;
}

QStringList coreBypassRules(const QStringList &rules)
{
    QStringList out;
    out.reserve(rules.size());
    for (const QString &rule : rules) {
        const QString core = coreBypassRuleFor(rule);
        if (!core.isEmpty() && !out.contains(core))
            out << core;
    }
    return out;
}
