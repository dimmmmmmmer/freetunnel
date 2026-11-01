#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>

namespace freetunnel::toml_syntax {
QString tomlEsc(const QString &s);
struct OpenValue {
    bool basic = false;   // inside """ ... """
    bool literal = false; // inside ''' ... '''
    int brackets = 0;     // [ ... ] depth
    int braces = 0;       // { ... } depth
    bool inlineTable = false; // a { ... } was opened, here or on an earlier line
    bool open() const { return basic || literal || brackets > 0 || braces > 0; }
};

int endOfQuoted(const QString &line, int i);
int endOfOpenMultiline(OpenValue *v, const QString &text, int i);
void countBracket(OpenValue *v, QChar c);
void advanceOpenValue(OpenValue &v, const QString &line);
int commentStart(const QString &line);
QString unquotedKey(const QString &key);
QString tableName(const QString &written);
bool isTableHeader(const QString &line, QString *name);
QList<QPair<QString, QString>> splitTomlTables(const QString &toml);
QString normalizeBody(QString body);
QString keyOf(const QString &line);
QString unknownKeyLines(const QString &body, const QStringList &known);
const QStringList &knownEndpointKeys();
const QStringList &knownRootKeys();
}
