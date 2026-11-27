import Foundation

/// Where an HTTP reply's bytes come from; nil at the end of the stream.
protocol ByteSource: AnyObject {
    func read() async throws -> Data?
}

struct HTTPReply: Equatable {
    var status: Int
    var location: String?
    var body: Data?
}

/// A minimal HTTP/1.1 response reader, with a limit on everything: head, line and body.
/// The request side always sends `Connection: close` and `Accept-Encoding: identity`.
final class HTTPResponseReader {
    static let maximumHead = 16 * 1024
    static let maximumLine = 8 * 1024
    private let source: ByteSource
    private let limit: Int
    private var buffer = [UInt8]()
    private var position = 0
    private var ended = false

    init(_ source: ByteSource, bodyLimit: Int = Subscription.maximumBytes) {
        self.source = source
        limit = bodyLimit
    }

    private func fill() async throws -> Bool {
        if ended { return false }
        guard let data = try await source.read(), !data.isEmpty else {
            ended = true
            return false
        }
        buffer = Array(data)
        position = 0
        return true
    }

    private func byte() async throws -> UInt8? {
        if position == buffer.count, !(try await fill()) { return nil }
        defer { position += 1 }
        return buffer[position]
    }

    private func take(_ count: Int) async throws -> [UInt8]? {
        if position == buffer.count, !(try await fill()) { return nil }
        let taken = min(count, buffer.count - position)
        defer { position += taken }
        return Array(buffer[position..<position + taken])
    }

    // One line without its line end (CRLF or a bare LF), of at most maximumLine bytes;
    // `budget` is what a part of the reply may take in all.
    private func line(_ budget: inout Int) async throws -> String {
        var bytes = [UInt8]()
        while true {
            guard let next = try await byte() else { throw SubscriptionFetchError.network }
            budget -= 1
            if budget < 0 { throw SubscriptionFetchError.invalid }
            if next == 10 { break }
            if bytes.count >= Self.maximumLine { throw SubscriptionFetchError.invalid }
            bytes.append(next)
        }
        if bytes.last == 13 { bytes.removeLast() }
        return String(bytes: bytes, encoding: .isoLatin1) ?? ""
    }

    static func status(of line: String) -> Int? {
        let chars = Array(line.utf8)
        let prefix = Array("HTTP/1.".utf8)
        guard chars.count >= 12, Array(chars[0..<7]) == prefix, chars[7] == 48 || chars[7] == 49,
            chars[8] == 32, chars[9...11].allSatisfy({ $0 >= 48 && $0 <= 57 }),
            chars.count == 12 || chars[12] == 32
        else { return nil }
        // Three ASCII digits, so the bytes are always valid UTF-8.
        return String(bytes: chars[9...11], encoding: .utf8).flatMap { Int($0) }
    }

    private func headers(_ budget: inout Int) async throws -> [(String, String)] {
        var out: [(String, String)] = []
        while true {
            let text = try await line(&budget)
            if text.isEmpty { return out }
            guard let colon = text.firstIndex(of: ":"), colon != text.startIndex,
                text.first != " ", text.first != "\t"
            else { throw SubscriptionFetchError.invalid }
            let name = String(text[..<colon])
            // A name with a space in it is not one this reads.
            guard name == name.trimmingCharacters(in: .whitespaces) else {
                throw SubscriptionFetchError.invalid
            }
            out.append((
                name.lowercased(),
                String(text[text.index(after: colon)...]).trimmingCharacters(in: .whitespaces)
            ))
        }
    }

    /// The reply: its status, a redirect's Location, and the body of a 200 only. Any other
    /// status is judged on its head, so no body of it is read.
    func read() async throws -> HTTPReply {
        var budget = Self.maximumHead
        var status = 0
        var fields: [(String, String)] = []
        while true {
            guard let code = Self.status(of: try await line(&budget)) else {
                throw SubscriptionFetchError.invalid
            }
            status = code
            fields = try await headers(&budget)
            // An interim reply (100, 103) comes before the real one; a switch to another
            // protocol is no answer to this request.
            if status == 101 { throw SubscriptionFetchError.invalid }
            if status < 100 || status >= 200 { break }
        }
        guard status == 200 else {
            return HTTPReply(status: status, location: fields.first { $0.0 == "location" }?.1, body: nil)
        }
        // As the desktop's `isJson`: the media type before any parameter, in any case.
        let types = fields.filter { $0.0 == "content-type" }.map { $0.1 }
        guard types.count == 1,
            types[0].split(separator: ";", maxSplits: 1, omittingEmptySubsequences: false)[0]
                .trimmingCharacters(in: .whitespaces).lowercased() == "application/json"
        else { throw SubscriptionFetchError.invalid }
        return HTTPReply(status: status, location: nil, body: try await body(fields))
    }

    private func body(_ fields: [(String, String)]) async throws -> Data {
        let codings = fields.filter { $0.0 == "transfer-encoding" }.map { $0.1 }
        if !codings.isEmpty {
            guard codings.joined(separator: ",").lowercased().trimmingCharacters(in: .whitespaces) == "chunked"
            else { throw SubscriptionFetchError.invalid }
            return try await chunked()
        }
        let lengths = fields.filter { $0.0 == "content-length" }.map { $0.1 }
        if lengths.isEmpty { return try await untilClosed() }
        for value in lengths {
            guard !value.isEmpty, value.utf8.allSatisfy({ $0 >= 48 && $0 <= 57 }), value == lengths[0]
            else { throw SubscriptionFetchError.invalid }
        }
        let digits = lengths[0].drop(while: { $0 == "0" })
        // Eight digits are over the cap already, and not parsed.
        guard digits.count <= 7, let length = digits.isEmpty ? 0 : Int(digits), length <= limit else {
            throw SubscriptionFetchError.tooLarge
        }
        return try await exactly(length)
    }

    private func exactly(_ length: Int) async throws -> Data {
        var out = Data()
        while out.count < length {
            guard let part = try await take(length - out.count) else { throw SubscriptionFetchError.network }
            out.append(contentsOf: part)
        }
        return out
    }

    private func untilClosed() async throws -> Data {
        var out = Data()
        while let part = try await take(4096) {
            if out.count + part.count > limit { throw SubscriptionFetchError.tooLarge }
            out.append(contentsOf: part)
        }
        return out
    }

    // Chunks of a hex size (an extension after a semicolon is ignored), each followed by a
    // line end, then the last, empty chunk and trailers up to an empty line.
    private func chunked() async throws -> Data {
        var out = Data()
        while true {
            var budget = Self.maximumLine + 2
            let header = try await line(&budget)
            let sizeText = header.split(separator: ";", maxSplits: 1, omittingEmptySubsequences: false)[0]
                .trimmingCharacters(in: .whitespaces)
            guard !sizeText.isEmpty, sizeText.allSatisfy({ $0.isHexDigit && $0.isASCII }) else {
                throw SubscriptionFetchError.invalid
            }
            let digits = sizeText.drop(while: { $0 == "0" })
            guard digits.count <= 7 else { throw SubscriptionFetchError.tooLarge }
            let length = digits.isEmpty ? 0 : Int(digits, radix: 16)!
            if length == 0 {
                var trailer = Self.maximumHead
                while !(try await line(&trailer)).isEmpty {}
                return out
            }
            if out.count + length > limit { throw SubscriptionFetchError.tooLarge }
            out.append(try await exactly(length))
            var end = Self.maximumLine + 2
            if !(try await line(&end)).isEmpty { throw SubscriptionFetchError.invalid }
        }
    }
}
