// cppcheck-suppress-file missingIncludeSystem
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
    void aLinkIsToldFromACommand();
    void aMarkedImportLinkKeepsItsPayloadUnmarked();
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

// A link the system opened and the same URL run as a command parse to the same
// action; only the first may be asked about, so the mark has to survive parsing
// and nothing unmarked may read as a link.
void TestControl::aLinkIsToldFromACommand() {
    const auto link = parseControlCommand(linkControlString("freetunnel://disconnect"));
    QCOMPARE(link.action, ControlAction::Disconnect);
    QVERIFY(link.fromLink);

    const auto command = parseControlCommand("freetunnel://disconnect");
    QCOMPARE(command.action, ControlAction::Disconnect);
    QVERIFY(!command.fromLink);

    // The verb after the mark is read as loosely as it is without one.
    const auto loose = parseControlCommand("  " + linkControlString(" FreeTunnel://Toggle/ "));
    QCOMPARE(loose.action, ControlAction::Toggle);
    QVERIFY(loose.fromLink);
    QVERIFY(!parseControlCommand("toggle").fromLink);
    QVERIFY(!parseControlCommand("focus").fromLink);
}

// A tt:// link that came through the URL handler is imported exactly as before:
// the mark is not part of what is imported.
void TestControl::aMarkedImportLinkKeepsItsPayloadUnmarked() {
    const QString link = "tt://?abc123";
    const auto cmd = parseControlCommand(linkControlString(link));
    QCOMPARE(cmd.action, ControlAction::ImportLink);
    QCOMPARE(cmd.payload, link);
    QVERIFY(cmd.fromLink);
}

QTEST_MAIN(TestControl)
#include "test_control.moc"
