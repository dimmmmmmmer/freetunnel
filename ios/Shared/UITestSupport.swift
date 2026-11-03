import Foundation

// Only unsigned simulator UI tests use this isolated store. Device and Release
// builds always require the real App Group and Keychain entitlements.
enum UITestSupport {
    static var enabled: Bool {
        #if DEBUG && targetEnvironment(simulator)
            return ProcessInfo.processInfo.arguments.contains("--ui-testing")
        #else
            return false
        #endif
    }
    static func directory() throws -> URL {
        let base = try FileManager.default.url(
            for: .applicationSupportDirectory, in: .userDomainMask,
            appropriateFor: nil, create: true)
        let url = base.appendingPathComponent("UITests", isDirectory: true)
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }
    static func secretPath(_ key: String) throws -> URL {
        guard UUID(uuidString: key) != nil else { throw CocoaError(.fileReadInvalidFileName) }
        return try directory().appendingPathComponent("secret-" + key)
    }
}
