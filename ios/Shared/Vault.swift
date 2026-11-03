import Foundation
import FreeTunnelCore
import Security

enum Identity {
    static var group: String {
        Bundle.main.object(forInfoDictionaryKey: "FreeTunnelAppGroup") as? String ?? ""
    }
    static var keychain: String {
        Bundle.main.object(forInfoDictionaryKey: "FreeTunnelKeychainGroup") as? String ?? ""
    }
    static var tunnel: String {
        Bundle.main.object(
            forInfoDictionaryKey: "FreeTunnelTunnelIdentifier") as? String ?? ""
    }
    static func directory() throws -> URL {
        guard !group.isEmpty,
            let url = FileManager.default.containerURL(
                forSecurityApplicationGroupIdentifier: group)
        else {
            throw ClientError.storageFailed
        }
        return url
    }
}
enum Vault {
    private static func query(_ key: String) throws -> [String: Any] {
        guard !Identity.keychain.isEmpty else { throw ClientError.storageFailed }
        return [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: "org.freetunnel.ios",
            kSecAttrAccount as String: key,
            kSecAttrAccessGroup as String: Identity.keychain
        ]
    }
    static func write(_ data: Data, key: String) throws {
        var record = try query(key)
        let attributes: [String: Any] = [
            kSecValueData as String: data,
            kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        ]
        let status = SecItemUpdate(record as CFDictionary, attributes as CFDictionary)
        if status == errSecItemNotFound {
            record.merge(attributes) { _, new in new }
            guard SecItemAdd(record as CFDictionary, nil) == errSecSuccess else {
                throw ClientError.storageFailed
            }
        } else if status != errSecSuccess {
            throw ClientError.storageFailed
        }
    }
    static func read(_ key: String) throws -> Data {
        var record = try query(key)
        record[kSecReturnData as String] = true
        record[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        let status = SecItemCopyMatching(record as CFDictionary, &result)
        guard status == errSecSuccess, let data = result as? Data else { throw ClientError.missingSecret }
        return data
    }
    static func remove(_ key: String) throws {
        let status = SecItemDelete(try query(key) as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else { throw ClientError.storageFailed }
    }
}
