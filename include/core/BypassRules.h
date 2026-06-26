#pragma once

#include <QString>
#include <QStringList>

// UI validation: domain (optionally "*.x.y"), IP, or CIDR.
bool isValidBypassRule(const QString &rule);

// Core-facing form of one rule; empty when TrustTunnel would reject it.
QString coreBypassRuleFor(const QString &rule);

// Drop rules the core cannot use; dedupe by core form while keeping UI labels.
QStringList sanitizedBypassRules(const QStringList &rules);

// Punycode-normalized rules sent to TrustTunnel DOMAIN_FILTER.
QStringList coreBypassRules(const QStringList &rules);
