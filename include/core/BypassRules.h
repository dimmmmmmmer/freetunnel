// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>
#include <QStringList>

// UI validation: domain (optionally "*.x.y"), IP, or CIDR.
bool isValidBypassRule(const QString &rule);

// An IP or CIDR written with "*." or a leading dot, which isValidBypassRule()
// refuses. Asked separately so the refusal can say what to write instead.
bool isWildcardAddressRule(const QString &rule);

// A subnet of every address of its kind, as 0.0.0.0/0 or ::/0, which
// isValidBypassRule() refuses. Asked separately so the refusal can say why; an
// excluded route of every address is refused for the same reason.
bool isEveryAddressRule(const QString &rule);

// Core-facing form of one rule; empty when TrustTunnel would reject it.
QString coreBypassRuleFor(const QString &rule);

// Drop rules the core cannot use; dedupe by core form while keeping UI labels.
QStringList sanitizedBypassRules(const QStringList &rules);

// Punycode-normalized rules sent to TrustTunnel DOMAIN_FILTER.
QStringList coreBypassRules(const QStringList &rules);
