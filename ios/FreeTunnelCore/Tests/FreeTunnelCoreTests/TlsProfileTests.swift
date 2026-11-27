import XCTest

@testable import FreeTunnelCore

final class TlsProfileTests: XCTestCase {
    let source = """
        [endpoint]
        hostname = "vpn.example.com"
        addresses = ["192.0.2.1:443"]
        username = "alice"
        password = "secret"
        """
    /// 00 01 02 | 01 host | 05 alice | 06 s3cret | 02 addr | extra records.
    func link(_ extra: [UInt8]) -> String {
        func record(_ tag: UInt8, _ text: String) -> [UInt8] {
            [tag, UInt8(text.utf8.count)] + Array(text.utf8)
        }
        // Appended a part at a time: one long chain of + took the type checker too long.
        var bytes: [UInt8] = [0, 1, 2]
        bytes += record(1, "vpn.example.com")
        bytes += record(5, "alice")
        bytes += record(6, "s3cret")
        bytes += record(2, "1.2.3.4:443")
        bytes += extra
        let base64: String = Data(bytes).base64EncodedString()
        let urlSafe: String = base64.replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "=", with: "")
        return "tt://?" + urlSafe
    }
    func profile(_ link: String) throws -> String? {
        let config = try Configuration(XCTUnwrap(ImportDocument.parse(link).toml))
        return config.endpoint["tls_profile"] as? String
    }
    func testEveryTagByteDecodesToDesktopOrder() throws {
        let names = ["chrome", "chrome_canary", "safari", "firefox", "okhttp", "openssl", "boringssl"]
        for (byte, name) in names.enumerated() {
            let value = try profile(link([0x10, 1, UInt8(byte)]))
            XCTAssertEqual(value, byte == 0 ? nil : name)
            XCTAssertEqual(TlsProfiles.fromByte(UInt8(byte)), name)
        }
    }
    func testUnknownOrMalformedTagValueIsRefusedButLaterTagsAreIgnored() throws {
        XCTAssertThrowsError(try ImportDocument.parse(link([0x10, 1, 7])))
        XCTAssertThrowsError(try ImportDocument.parse(link([0x10, 1, 0xff])))
        XCTAssertThrowsError(try ImportDocument.parse(link([0x10, 0])))
        XCTAssertThrowsError(try ImportDocument.parse(link([0x10, 2, 1, 1])))
        XCTAssertNil(try profile(link([0x11, 1, 3])))
    }
    func testEditorRoundTripAndLinkExport() throws {
        let original = try Configuration(source)
        var fields = EditorFields(original)
        XCTAssertEqual(fields.tlsProfile, "chrome")
        XCTAssertNil(try fields.configuration(previous: original).endpoint["tls_profile"])
        fields.tlsProfile = "firefox"
        let edited = try fields.configuration(previous: original)
        XCTAssertEqual(edited.endpoint["tls_profile"] as? String, "firefox")
        XCTAssertEqual(EditorFields(try Configuration(edited.serialized())).tlsProfile, "firefox")
        let exported = try DeepLinkEncoder.encode(edited)
        XCTAssertEqual(try profile(exported), "firefox")
        // Chrome adds no tag at all.
        XCTAssertEqual(
            try DeepLinkEncoder.encode(original),
            try DeepLinkEncoder.encode(fields.configuration(previous: original).withProfile("chrome")))
        fields.tlsProfile = "nope"
        XCTAssertThrowsError(try fields.configuration(previous: original))
    }
    func testTomlKeyIsKeptAliasMappedAndUnknownRefused() throws {
        let kept = try Configuration(source + "\ntls_profile = \"okhttp\"\ncustom = 1")
        XCTAssertEqual(EditorFields(kept).tlsProfile, "okhttp")
        let rewritten = try EditorFields(kept).configuration(previous: kept)
        XCTAssertEqual(rewritten.endpoint["tls_profile"] as? String, "okhttp")
        XCTAssertEqual(rewritten.endpoint["custom"] as? Int64, 1)
        let alias = try Configuration(source + "\ntls_profile = \"default\"")
        XCTAssertEqual(EditorFields(alias).tlsProfile, "boringssl")
        let imported = try ImportDocument.parse(source + "\ntls_profile = \"default\"")
        XCTAssertEqual(
            try Configuration(XCTUnwrap(imported.toml)).endpoint["tls_profile"] as? String,
            "boringssl")
        XCTAssertThrowsError(try Configuration(source + "\ntls_profile = \"Chrome\""))
        XCTAssertThrowsError(try ImportDocument.parse(source + "\ntls_profile = \"curl\""))
        XCTAssertThrowsError(try ImportDocument.parse(source + "\ntls_profile = 3"))
        XCTAssertNoThrow(try Configuration(source + "\ntls_profile = \"\""))
    }
}

extension Configuration {
    fileprivate func withProfile(_ name: String) throws -> Configuration {
        var fields = EditorFields(self)
        fields.tlsProfile = name
        return try fields.configuration(previous: self)
    }
}
