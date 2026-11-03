import UIKit
import XCTest

@MainActor final class FreeTunnelUITests: XCTestCase {
    private var app = XCUIApplication()
    private let config = """
        [endpoint]
        name = "UI import"
        hostname = "vpn.example.org"
        addresses = ["192.0.2.1:443"]
        username = "fixture-user"
        password = "fixture-password"
        """
    override func setUpWithError() throws {
        continueAfterFailure = false
        XCUIDevice.shared.orientation = .portrait
        app.launchArguments = ["--ui-testing", "-AppleLanguages", "(en)", "-AppleLocale", "en_US"]
        app.launchEnvironment = ["FT_UI_TEST_RESET": "1", "FT_UI_TEST_CLIPBOARD": config]
    }
    private func tab(_ name: String) {
        app.buttons["tab-" + name].tap()
    }
    private func reveal(_ element: XCUIElement) {
        for _ in 0..<10 {
            if element.isHittable { return }
            let scroll = app.scrollViews.containing(.any, identifier: element.identifier).firstMatch
            if scroll.exists { scroll.swipeUp() } else { app.swipeUp() }
        }
        XCTAssertTrue(element.isHittable, "Cannot reach \(element.identifier)")
    }
    private func capture(_ name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
    private func relaunch() {
        app.terminate()
        app.launchEnvironment["FT_UI_TEST_RESET"] = "0"
        app.launch()
    }
    private func importConfig() {
        tab("servers")
        app.buttons["server-add"].tap()
        app.buttons["Paste from clipboard"].tap()
        app.buttons["confirm-action"].tap()
        XCTAssertTrue(app.buttons["server-edit-UI import"].waitForExistence(timeout: 5))
    }
    func testImportEditPersistAndDelete() {
        app.launch()
        importConfig()
        app.buttons["server-edit-UI import"].tap()
        let name = app.textFields["server-name"]
        XCTAssertTrue(name.waitForExistence(timeout: 5))
        name.tap()
        name.typeText(
            String(repeating: XCUIKeyboardKey.delete.rawValue, count: "UI import".count) + "Renamed"
        )
        capture("editor-keyboard")
        reveal(app.buttons["server-save"])
        app.buttons["server-save"].tap()
        XCTAssertTrue(app.buttons["server-edit-Renamed"].waitForExistence(timeout: 5))
        relaunch()
        tab("servers")
        app.buttons["server-edit-Renamed"].tap()
        XCTAssertEqual(app.textFields["server-host"].value as? String, "vpn.example.org")
        XCTAssertEqual(app.textFields["server-user"].value as? String, "fixture-user")
        app.buttons["Back"].tap()
        app.buttons["server-delete-Renamed"].tap()
        app.buttons["confirm-action"].tap()
        XCTAssertFalse(app.buttons["server-edit-Renamed"].exists)
        relaunch()
        tab("servers")
        XCTAssertFalse(app.buttons["server-edit-Renamed"].exists)
        capture("empty-servers-after-delete")
    }
    func testInvalidImportDoesNotCreateServer() {
        app.launchEnvironment["FT_UI_TEST_CLIPBOARD"] = "tt://not-a-valid-config"
        app.launch()
        tab("servers")
        app.buttons["server-add"].tap()
        app.buttons["Paste from clipboard"].tap()
        app.buttons["confirm-action"].tap()
        XCTAssertTrue(app.staticTexts["notification"].waitForExistence(timeout: 5))
        XCTAssertEqual(
            app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'server-edit-'")).count,
            0)
        capture("invalid-import")
        relaunch()
        tab("servers")
        XCTAssertEqual(
            app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'server-edit-'")).count,
            0)
    }
    func testManualRulesPersistAndRemove() {
        app.launch()
        tab("routing")
        let field = app.textFields["manual-rules"]
        field.tap()
        field.typeText("example.org, example.net\n")
        XCTAssertTrue(app.buttons["Remove example.org"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["Remove example.net"].exists)
        capture("manual-rule-chips")
        relaunch()
        tab("routing")
        app.buttons["Remove example.org"].tap()
        XCTAssertFalse(app.buttons["Remove example.org"].exists)
        XCTAssertTrue(app.buttons["Remove example.net"].exists)
        relaunch()
        tab("routing")
        XCTAssertFalse(app.buttons["Remove example.org"].exists)
        XCTAssertTrue(app.buttons["Remove example.net"].exists)
    }
    func testThemeAndIntervalPersist() {
        app.launch()
        tab("settings")
        app.buttons["picker-theme"].tap()
        app.buttons["Dark"].tap()
        XCTAssertEqual(app.buttons["picker-theme"].value as? String, "Dark")
        tab("routing")
        reveal(app.buttons["picker-interval"])
        app.buttons["picker-interval"].tap()
        capture("interval-options")
        app.buttons["Every 2 hours"].tap()
        relaunch()
        tab("settings")
        XCTAssertEqual(app.buttons["picker-theme"].value as? String, "Dark")
        tab("routing")
        reveal(app.buttons["picker-interval"])
        XCTAssertEqual(app.buttons["picker-interval"].value as? String, "Every 2 hours")
    }
    func testGeoListDirectionAndRemoval() {
        app.launch()
        tab("routing")
        reveal(app.buttons["picker-add-list"])
        app.buttons["picker-add-list"].tap()
        // The catalog is bundled; only the list download is a deterministic fixture.
        app.buttons["Blocked sites in Russia"].tap()
        let direction = app.buttons["picker-direction"]
        reveal(direction)
        direction.tap()
        app.buttons["Bypass VPN"].tap()
        relaunch()
        tab("routing")
        reveal(direction)
        XCTAssertEqual(direction.value as? String, "Bypass VPN")
        capture("geo-card")
        reveal(app.buttons["Remove"])
        app.buttons["Remove"].tap()
        XCTAssertFalse(direction.exists)
        relaunch()
        tab("routing")
        XCTAssertFalse(direction.exists)
    }
    func testPortraitAndLandscapeLayouts() {
        app.launchArguments = ["--ui-preview", "-AppleLanguages", "(ru)"]
        app.launchEnvironment = ["FT_PREVIEW_LONG_NAMES": "1", "FT_PREVIEW_SCENARIO": "connected"]
        app.launch()
        for orientation in [UIDeviceOrientation.portrait, .landscapeLeft] {
            XCUIDevice.shared.orientation = orientation
            let label = orientation == .portrait ? "portrait" : "landscape"
            for page in ["connection", "servers", "routing", "settings", "logs"] {
                tab(page)
                XCTAssertTrue(app.buttons["tab-" + page].isHittable)
                capture(label + "-" + page)
            }
            tab("servers")
            app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'server-edit-'")).firstMatch.tap()
            XCTAssertTrue(app.textFields["server-name"].waitForExistence(timeout: 5))
            capture(label + "-editor")
            reveal(app.buttons["server-save"])
            capture(label + "-editor-bottom")
            app.buttons["server-save"].tap()
        }
    }
    func testLargeTextLayouts() {
        app.launchArguments = [
            "--ui-preview", "-AppleLanguages", "(ru)",
            "-UIPreferredContentSizeCategoryName", "UICTContentSizeCategoryAccessibilityL"
        ]
        app.launchEnvironment = ["FT_PREVIEW_LONG_NAMES": "1"]
        app.launch()
        for page in ["connection", "servers", "routing", "settings", "logs"] {
            tab(page)
            XCTAssertTrue(app.buttons["tab-" + page].isHittable)
            capture("large-text-" + page)
        }
        tab("routing")
        reveal(app.buttons["picker-interval"])
        app.buttons["picker-interval"].tap()
        capture("large-text-interval")
        XCTAssertTrue(app.buttons["Никогда"].exists)
    }
}
