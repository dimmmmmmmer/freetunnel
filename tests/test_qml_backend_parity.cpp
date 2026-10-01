// cppcheck-suppress-file missingIncludeSystem
// The QML tests run every page against MockBackend; the app runs them against
// Backend. Nothing held the two together: a property, method or signal the mock
// grew and Backend never had — or the other way round, or the same name with
// another type or other parameter names — leaves the QML tests green and the
// real window with a binding that reads undefined, a call that throws, or a
// handler that never runs. The real Main.qml is loaded by test_app_startup only
// where it can be, and nothing there fails on a QML warning.
//
// So the two are compared here as QML sees them, through their meta-objects.
#include <QtTest>

#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QSet>

#include "app/Backend.h"
#include "ui/MockBackend.h"

namespace {

// Writable on the mock only so a test can put the page in a state; QML never
// writes them, and on Backend they follow the session.
const QStringList kSetByTestsOnly = {
        QStringLiteral("connected"),          QStringLiteral("connecting"),
        QStringLiteral("disconnecting"),      QStringLiteral("activeConfigProfile"),
        QStringLiteral("installedAppsReady"), QStringLiteral("unavailableHotkeys"),
        QStringLiteral("updateErrorOpensPage")};

// The mock's own, for the tests: a place a QML callback can record that it ran.
const QStringList kMockOnly = {QStringLiteral("confirmLog")};

// One property as QML sees it: its type, and whether a binding on it updates or
// a write to it lands.
QString describe(const QMetaProperty &p, bool withWritability)
{
    return QStringLiteral("%1 %2%3%4%5")
            .arg(QString::fromLatin1(p.typeName()), QString::fromLatin1(p.name()),
                 p.hasNotifySignal() ? QStringLiteral(" notify") : QString(),
                 p.isConstant() ? QStringLiteral(" constant") : QString(),
                 withWritability && p.isWritable() ? QStringLiteral(" writable") : QString());
}

// Every property the class declares itself, by name.
QMap<QString, QMetaProperty> propertiesOf(const QMetaObject &mo)
{
    QMap<QString, QMetaProperty> out;
    for (int i = mo.propertyOffset(); i < mo.propertyCount(); ++i)
        out.insert(QString::fromLatin1(mo.property(i).name()), mo.property(i));
    return out;
}

// Every method QML can reach — public signals, slots and invokables — with what
// a QML caller or handler depends on: return type and parameter types and, for
// a signal, parameter names too, since a handler refers to its parameters by
// name. A call passes them by position, so a method's names do not matter.
QSet<QString> methodsOf(const QMetaObject &mo)
{
    QSet<QString> out;
    for (int i = mo.methodOffset(); i < mo.methodCount(); ++i) {
        const QMetaMethod m = mo.method(i);
        if (m.access() != QMetaMethod::Public)
            continue;
        const QString what = QStringLiteral("%1 %2").arg(QString::fromLatin1(m.typeName()),
                                                          QString::fromLatin1(m.methodSignature()));
        if (m.methodType() != QMetaMethod::Signal) {
            out.insert(QStringLiteral("method ") + what);
            continue;
        }
        QStringList names;
        for (const QByteArray &n : m.parameterNames())
            names << QString::fromLatin1(n);
        out.insert(QStringLiteral("signal %1 [%2]").arg(what, names.join(QLatin1Char(','))));
    }
    return out;
}

QStringList sorted(QStringList l)
{
    l.sort();
    return l;
}

} // namespace

class TestQmlBackendParity : public QObject {
    Q_OBJECT

private slots:
    void theMockHasBackendsPropertiesAndNoOthers();
    void theMockHasBackendsMethodsAndSignalsAndNoOthers();
};

void TestQmlBackendParity::theMockHasBackendsPropertiesAndNoOthers()
{
    const auto real = propertiesOf(Backend::staticMetaObject);
    const auto mock = propertiesOf(MockBackend::staticMetaObject);

    QStringList missing; // QML reads them in the app; the tests cannot
    QStringList extra;   // QML tests read them; the app has nothing there
    QStringList differ;
    for (auto it = real.cbegin(); it != real.cend(); ++it) {
        if (!mock.contains(it.key()))
            missing << describe(it.value(), true);
    }
    for (auto it = mock.cbegin(); it != mock.cend(); ++it) {
        if (kMockOnly.contains(it.key()))
            continue;
        if (!real.contains(it.key())) {
            extra << describe(it.value(), true);
            continue;
        }
        const QMetaProperty r = real.value(it.key());
        const QMetaProperty m = it.value();
        // A write QML makes must land on both. The mock may be writable where
        // Backend is not only for what the tests alone set.
        const bool writesAgree = r.isWritable() == m.isWritable()
                || (!r.isWritable() && kSetByTestsOnly.contains(it.key()));
        if (describe(r, false) != describe(m, false) || !writesAgree)
            differ << QStringLiteral("Backend: %1 / MockBackend: %2").arg(describe(r, true), describe(m, true));
    }
    QVERIFY2(missing.isEmpty(), qPrintable(QStringLiteral("only on Backend: ") + missing.join(QStringLiteral("; "))));
    QVERIFY2(extra.isEmpty(), qPrintable(QStringLiteral("only on MockBackend: ") + extra.join(QStringLiteral("; "))));
    QVERIFY2(differ.isEmpty(), qPrintable(differ.join(QStringLiteral("; "))));
}

void TestQmlBackendParity::theMockHasBackendsMethodsAndSignalsAndNoOthers()
{
    const QSet<QString> real = methodsOf(Backend::staticMetaObject);
    const QSet<QString> mock = methodsOf(MockBackend::staticMetaObject);
    // Backend::checkForUpdates(bool userInitiated = true): moc lists it with and
    // without the argument. QML calls it without one, which the mock covers.
    const QSet<QString> onlyReal =
            real - mock - QSet<QString>{QStringLiteral("method void checkForUpdates(bool)")};
    const QSet<QString> onlyMock = mock - real;
    QVERIFY2(onlyReal.isEmpty(), qPrintable(QStringLiteral("only on Backend: ")
                                            + sorted(onlyReal.values()).join(QStringLiteral("; "))));
    QVERIFY2(onlyMock.isEmpty(), qPrintable(QStringLiteral("only on MockBackend: ")
                                            + sorted(onlyMock.values()).join(QStringLiteral("; "))));
}

QTEST_MAIN(TestQmlBackendParity)
#include "test_qml_backend_parity.moc"
