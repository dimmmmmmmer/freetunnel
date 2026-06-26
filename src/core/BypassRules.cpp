#include "core/BypassRules.h"

#include <QHostAddress>
#include <QRegularExpression>
#include <QUrl>

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
    return ok && p >= 0 && p <= max;
}

static bool isValidDomainBypassRule(const QString &rule)
{
    static const QRegularExpression fqdn(
        QStringLiteral("^(?=.{1,253}$)([\\p{L}\\p{N}]([\\p{L}\\p{N}-]{0,61}[\\p{L}\\p{N}])?\\.)+"
                       "[\\p{L}]{2,63}$"),
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
    if (isValidIpBypassRule(r))
        return true;
    Q_UNUSED(wildcard);
    return isValidDomainBypassRule(r);
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
        return {};
    }
    if (isValidIpBypassRule(r))
        return rule.trimmed();
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
