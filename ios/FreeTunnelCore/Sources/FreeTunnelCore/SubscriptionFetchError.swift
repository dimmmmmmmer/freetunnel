import Foundation

/// Why a subscription fetch failed, as the desktop's `SubscriptionFetchError` tells them
/// apart. Never carries the address or its credentials.
public enum SubscriptionFetchError: Error, Equatable {
    /// The address is none.
    case badURL
    /// The config pins certificates and none of them can be read.
    case badPin
    /// The server's certificate is not one the config vouches for.
    case untrusted
    case timeout
    /// The connection or the server's reply failed.
    case network
    case http(Int)
    case tooLarge
    /// A reply that is no HTTP, or a redirect that is not allowed.
    case invalid

    /// The error the app reports to the user.
    public var clientError: ClientError {
        switch self {
        case .badURL: return .invalidSource
        case .badPin: return .subscriptionBadPin
        case .untrusted: return .subscriptionUntrusted
        case .tooLarge: return .tooLarge
        // As the desktop: a 401 is a link that no longer works, and a 403, 404, 405 or 407 a
        // server with no subscription here; nothing a retry would change.
        case .http(401): return .subscriptionDenied
        case .http(403), .http(404), .http(405), .http(407): return .subscriptionNoUpdates
        case .timeout, .network, .http, .invalid: return .networkFailed
        }
    }
}
