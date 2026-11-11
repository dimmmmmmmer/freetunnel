import XCTest
@testable import FreeTunnelCore

final class UpdatePolicyTests: XCTestCase {
    func response(_ version: String = "1.2.10", bundle: String = "org.freetunnel.ios", url: String = "https://apps.apple.com/app/id123") throws -> Data {
        try JSONSerialization.data(withJSONObject: ["results": [["bundleId": bundle, "version": version, "trackViewUrl": url]]])
    }
    func testStoreUpdateMatchesIdentityAndNumericVersion() throws {
        XCTAssertNotNil(try UpdatePolicy.newerRelease(response(), bundleID: "org.freetunnel.ios", current: "1.2.9"))
        XCTAssertNil(try UpdatePolicy.newerRelease(response("1.2.9"), bundleID: "org.freetunnel.ios", current: "1.2.10"))
        XCTAssertNil(try UpdatePolicy.newerRelease(response(bundle: "other.app"), bundleID: "org.freetunnel.ios", current: "1.0"))
        XCTAssertNil(try UpdatePolicy.newerRelease(response("2.0-beta"), bundleID: "org.freetunnel.ios", current: "1.0"))
    }
    func testStoreAndTestFlightLinksStayOnAppleHosts() throws {
        for url in ["http://apps.apple.com/app/id1", "https://apps.apple.com.evil.example/app/id1", "https://user@apps.apple.com/app/id1"] {
            XCTAssertNil(try UpdatePolicy.newerRelease(response(url: url), bundleID: "org.freetunnel.ios", current: "1.0"))
        }
        XCTAssertNotNil(UpdatePolicy.testFlightURL("https://testflight.apple.com/join/Ab123"))
        XCTAssertNil(UpdatePolicy.testFlightURL("https://testflight.apple.com/join/"))
        XCTAssertNil(UpdatePolicy.testFlightURL("https://example.com/join/Ab123"))
        XCTAssertNil(UpdatePolicy.testFlightURL("https://user@testflight.apple.com/join/Ab123"))
    }
}
