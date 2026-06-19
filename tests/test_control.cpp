#include <QtTest>

#include "core/ControlCommand.h"

using namespace freetunnel;

// Coverage for the deep-link / tray / second-instance control parser.
class TestControl : public QObject {
    Q_OBJECT

private slots:
    void verbs();
    void schemePrefixOptional();
    void caseAndStraySlashes();
    void importLinkKeepsPayload();
    void noopCommands();
    void unknownIsNoop();
};

void TestControl::verbs() {
    QCOMPARE(parseControlCommand("freetunnel://toggle").action, ControlAction::Toggle);
    QCOMPARE(parseControlCommand("freetunnel://connect").action, ControlAction::Connect);
    QCOMPARE(parseControlCommand("freetunnel://disconnect").action, ControlAction::Disconnect);
}

void TestControl::schemePrefixOptional() {
    // A bare verb (e.g. forwarded by a second instance) works too.
    QCOMPARE(parseControlCommand("toggle").action, ControlAction::Toggle);
    QCOMPARE(parseControlCommand("connect").action, ControlAction::Connect);
}

void TestControl::caseAndStraySlashes() {
    QCOMPARE(parseControlCommand("FreeTunnel://Toggle").action, ControlAction::Toggle);
    QCOMPARE(parseControlCommand("freetunnel://toggle/").action, ControlAction::Toggle);
    QCOMPARE(parseControlCommand("  freetunnel://DISCONNECT  ").action, ControlAction::Disconnect);
}

void TestControl::importLinkKeepsPayload() {
    const QString link = "tt://?abc123";
    const auto cmd = parseControlCommand(link);
    QCOMPARE(cmd.action, ControlAction::ImportLink);
    QCOMPARE(cmd.payload, link);
    // Surrounding whitespace is trimmed off the payload.
    QCOMPARE(parseControlCommand("  " + link + "  ").payload, link);
}

void TestControl::noopCommands() {
    QCOMPARE(parseControlCommand("").action, ControlAction::None);
    QCOMPARE(parseControlCommand("   ").action, ControlAction::None);
    QCOMPARE(parseControlCommand("focus").action, ControlAction::None);
    QCOMPARE(parseControlCommand("FOCUS").action, ControlAction::None);
}

void TestControl::unknownIsNoop() {
    QCOMPARE(parseControlCommand("freetunnel://launchmissiles").action, ControlAction::None);
    QCOMPARE(parseControlCommand("https://example.com").action, ControlAction::None);
}

QTEST_MAIN(TestControl)
#include "test_control.moc"
