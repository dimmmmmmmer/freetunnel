import Foundation

/// RFC 3492, with every step of the arithmetic checked: nil on overflow.
struct PunycodeEncoder {
    private static let base: UInt32 = 36
    private static let tMin: UInt32 = 1
    private static let tMax: UInt32 = 26
    private static let skew: UInt32 = 38
    private static let damp: UInt32 = 700

    private let input: [UInt32]
    private let basic: UInt32
    private var next: UInt32 = 128
    private var delta: UInt32 = 0
    private var bias: UInt32 = 72
    private var handled: UInt32
    private var output: [Character]

    static func encode(_ input: [UInt32]) -> String? {
        guard input.allSatisfy({ $0 <= 0x10FFFF }) else { return nil }
        var encoder = PunycodeEncoder(input: input)
        return encoder.run()
    }

    private init(input: [UInt32]) {
        self.input = input
        output = input.filter { $0 < 128 }.map { Character(Unicode.Scalar($0)!) }
        basic = UInt32(output.count)
        handled = basic
        if basic > 0 { output.append("-") }
    }

    private mutating func run() -> String? {
        while handled < UInt32(input.count) {
            guard let smallest = input.filter({ $0 >= next }).min() else { return nil }
            guard jump(to: smallest), scan(), step() else { return nil }
        }
        return String(output)
    }

    // Moves the delta over every code point below the smallest one still to encode.
    private mutating func jump(to smallest: UInt32) -> Bool {
        let (product, overflow) = (smallest - next).multipliedReportingOverflow(by: handled + 1)
        let (sum, overflow2) = delta.addingReportingOverflow(product)
        if overflow || overflow2 { return false }
        delta = sum
        next = smallest
        return true
    }

    private mutating func scan() -> Bool {
        for point in input {
            if point < next {
                let (bumped, over) = delta.addingReportingOverflow(1)
                if over { return false }
                delta = bumped
            }
            if point == next {
                appendInteger(delta)
                bias = Self.adapt(delta, handled + 1, handled == basic)
                delta = 0
                handled += 1
            }
        }
        return true
    }

    private mutating func step() -> Bool {
        let (bumped, over) = delta.addingReportingOverflow(1)
        if over || next == UInt32.max { return false }
        delta = bumped
        next += 1
        return true
    }

    private mutating func appendInteger(_ value: UInt32) {
        var quotient = value
        var weight = Self.base
        while true {
            let threshold = weight <= bias ? Self.tMin : (weight >= bias + Self.tMax ? Self.tMax : weight - bias)
            if quotient < threshold { break }
            output.append(Self.digit(threshold + (quotient - threshold) % (Self.base - threshold)))
            quotient = (quotient - threshold) / (Self.base - threshold)
            weight += Self.base
        }
        output.append(Self.digit(quotient))
    }

    private static func digit(_ value: UInt32) -> Character {
        Character(Unicode.Scalar(value < 26 ? value + 97 : value + 22)!)
    }

    private static func adapt(_ delta: UInt32, _ points: UInt32, _ first: Bool) -> UInt32 {
        var scaled = first ? delta / damp : delta / 2
        scaled += scaled / points
        var shift: UInt32 = 0
        while scaled > ((base - tMin) * tMax) / 2 {
            scaled /= base - tMin
            shift += base
        }
        return shift + (base - tMin + 1) * scaled / (scaled + skew)
    }
}
