import Foundation

/// The session timer under Home's logo: HH:MM:SS, as on desktop and Android.
public enum SessionTime {
    public static func format(seconds: Int) -> String {
        let total = max(0, seconds)
        return String(format: "%02d:%02d:%02d", total / 3600, (total % 3600) / 60, total % 60)
    }
}
