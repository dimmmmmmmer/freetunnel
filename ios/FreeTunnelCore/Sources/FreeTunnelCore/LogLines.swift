import Foundation

public enum LogLines {
    private static let core = try? NSRegularExpression(
        pattern: "^(\\d{2})\\.(\\d{2})\\.(\\d{4})\\s+(\\d{2}:\\d{2}:\\d{2})\\.\\d+\\s+(\\w+)\\s+(.*)$")
    private static let stored = try? NSRegularExpression(
        pattern: "^\\d{4}-\\d{2}-\\d{2} (\\d{2}:\\d{2}:\\d{2})\\t([A-Z]+)\\t(.*)$")
    public static func entry(
        _ message: String, level: String = "INFO", isCore: Bool = false, date: Date = Date()
    ) -> String {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd HH:mm:ss"
        var stamp = formatter.string(from: date)
        var tag = level
        var body = message
        if isCore {
            tag = "CORE"
            if let match = core?.firstMatch(in: message, range: NSRange(message.startIndex..., in: message)) {
                func group(_ index: Int) -> String {
                    Range(match.range(at: index), in: message).map { String(message[$0]) } ?? ""
                }
                stamp = "\(group(3))-\(group(2))-\(group(1)) \(group(4))"
                let value = group(5).uppercased()
                tag =
                    ["ERROR", "ERR", "FATAL"].contains(value)
                    ? "ERROR" : ["WARN", "WARNING"].contains(value) ? "WARN" : "CORE"
                body = group(6)
            }
        }
        let clean = String(
            body.unicodeScalars.prefix(2000).map {
                CharacterSet.controlCharacters.contains($0) ? " " : String($0)
            }.joined())
        return stamp + "\t" + tag + "\t" + clean
    }
    public static func shownText(_ text: String) -> String {
        text.split(whereSeparator: { $0.isNewline }).suffix(1000).map { value in
            let line = String(value)
            guard let match = stored?.firstMatch(in: line, range: NSRange(line.startIndex..., in: line))
            else {
                return line
            }
            return (1...3).compactMap { Range(match.range(at: $0), in: line).map { String(line[$0]) } }
                .joined(separator: " ")
        }.joined(separator: "\n")
    }
}
