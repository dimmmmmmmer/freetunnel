import CoreImage
import CoreImage.CIFilterBuiltins
import FreeTunnelCore
import SwiftUI
import UIKit
import UniformTypeIdentifiers

// A config's deep link as a QR code for the Android app to scan (the iOS counterpart of the
// desktop's QrCodeDialog and Android's showConfigQr).
enum QRCode {
    // The quiet zone the standard asks for, in modules, so the card's colour never touches it.
    static let quiet = 4

    struct Result {
        // Dark modules on white with the quiet zone, a pixel a module: scale it without smoothing.
        let image: UIImage
        // Modules a side, the quiet zone included.
        let modules: Int
    }

    // The code as grey pixels, row 0 at the top.
    private struct GreyBitmap {
        let width: Int
        let height: Int
        let pixels: [UInt8]

        func isDark(_ column: Int, _ row: Int) -> Bool { pixels[row * width + column] < 128 }

        // The box round the dark pixels, whatever margin the filter adds; nil when there are none
        // or the box is not square.
        func darkBounds() -> Bounds? {
            var bounds = Bounds(minX: width, minY: height, maxX: -1, maxY: -1)
            for row in 0..<height {
                for column in 0..<width where isDark(column, row) {
                    bounds.minX = min(bounds.minX, column)
                    bounds.maxX = max(bounds.maxX, column)
                    bounds.minY = min(bounds.minY, row)
                    bounds.maxY = max(bounds.maxY, row)
                }
            }
            guard bounds.maxX >= bounds.minX, bounds.maxY >= bounds.minY,
                bounds.maxX - bounds.minX == bounds.maxY - bounds.minY
            else { return nil }
            return bounds
        }
    }

    private struct Bounds {
        var minX: Int
        var minY: Int
        var maxX: Int
        var maxY: Int
    }

    private static func greyBitmap(_ text: String) -> GreyBitmap? {
        let filter = CIFilter.qrCodeGenerator()
        filter.message = Data(text.utf8)
        filter.correctionLevel = "L"
        guard let output = filter.outputImage else { return nil }
        let extent = output.extent.integral
        let width = Int(extent.width)
        let height = Int(extent.height)
        guard width > 0, width == height,
            let rendered = CIContext().createCGImage(output, from: extent)
        else { return nil }
        // Read the code back as grey pixels (row 0 at the top).
        var pixels = [UInt8](repeating: 255, count: width * height)
        let drawn = pixels.withUnsafeMutableBytes { buffer -> Bool in
            guard
                let context = CGContext(
                    data: buffer.baseAddress, width: width, height: height, bitsPerComponent: 8,
                    bytesPerRow: width, space: CGColorSpaceCreateDeviceGray(),
                    bitmapInfo: CGImageAlphaInfo.none.rawValue)
            else { return false }
            context.interpolationQuality = .none
            context.draw(rendered, in: CGRect(x: 0, y: 0, width: width, height: height))
            return true
        }
        guard drawn else { return nil }
        return GreyBitmap(width: width, height: height, pixels: pixels)
    }

    // nil when the text does not fit a QR code at correction level L.
    static func make(_ text: String) -> Result? {
        guard let bitmap = greyBitmap(text), let bounds = bitmap.darkBounds() else { return nil }
        // The finder pattern's top edge is seven modules wide: that gives the module's pixels.
        var run = 0
        while bounds.minX + run <= bounds.maxX && bitmap.isDark(bounds.minX + run, bounds.minY) { run += 1 }
        let module = max(1, run / 7)
        let count = (bounds.maxX - bounds.minX + 1) / module
        guard count >= 21 else { return nil }
        let side = count + 2 * quiet
        let format = UIGraphicsImageRendererFormat()
        format.scale = 1
        format.opaque = true
        let image = UIGraphicsImageRenderer(
            size: CGSize(width: side, height: side), format: format
        ).image { context in
            UIColor.white.setFill()
            context.fill(CGRect(x: 0, y: 0, width: side, height: side))
            UIColor.black.setFill()
            for row in 0..<count {
                for column in 0..<count
                where bitmap.isDark(
                    bounds.minX + column * module + module / 2, bounds.minY + row * module + module / 2) {
                    context.fill(CGRect(x: quiet + column, y: quiet + row, width: 1, height: 1))
                }
            }
        }
        return Result(image: image, modules: side)
    }
}

@MainActor enum ScreenCapture {
    // The screen is being recorded, mirrored or shared.
    static var active: Bool {
        UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.contains {
            $0.screen.isCaptured || $0.traitCollection.sceneCaptureState == .active
        }
    }
}

@MainActor extension AppState {
    // What "Copy deep-link" copies and the QR code shows.
    func deepLink(_ profile: Profile) throws -> String {
        try DeepLinkEncoder.encode(Configuration(secret(profile).toml))
    }
    // The link carries the password: kept off the shared pasteboard and not for long.
    func copyToPasteboard(_ text: String) {
        UIPasteboard.general.setItems(
            [[UTType.utf8PlainText.identifier: text]],
            options: [.localOnly: true, .expirationDate: Date().addingTimeInterval(120)])
    }
}

struct QRCodeDialog: View {
    @ObservedObject var state: AppState
    let id: UUID
    @Environment(\.scenePhase) private var phase
    @Environment(\.verticalSizeClass) private var verticalSize
    @State private var code: QRCode.Result?
    @State private var captured = false
    @State private var idleTimerWasDisabled = false
    // Held here: a publisher made in `body` would be a new one for every evaluation.
    @State private var tick = Timer.publish(every: 1, on: .main, in: .common).autoconnect()
    private var profile: Profile? { state.preferences.profiles.first { $0.id == id } }
    private var hidden: Bool { captured || phase != .active }
    private func close() { state.dialog = nil }
    var body: some View {
        AppDialog(title: LocalizedStringKey(profile?.name ?? ""), close: close) {
            VStack(spacing: 12) {
                Color.clear.aspectRatio(1, contentMode: .fit)
                    .frame(maxHeight: verticalSize == .compact ? 220 : 372)
                    .overlay {
                        GeometryReader { geometry in
                            codeView(side: min(geometry.size.width, geometry.size.height))
                                .frame(
                                    width: geometry.size.width, height: geometry.size.height)
                        }
                    }
                Text("Scan with FreeTunnel on Android").font(AppStyle.caption)
                    .foregroundStyle(AppStyle.secondary).multilineTextAlignment(.center)
                    .frame(maxWidth: .infinity)
            }
        }
        .onAppear {
            idleTimerWasDisabled = UIApplication.shared.isIdleTimerDisabled
            UIApplication.shared.isIdleTimerDisabled = true
            captured = ScreenCapture.active
            if let profile, let link = try? state.deepLink(profile) { code = QRCode.make(link) }
            if code == nil { close() }
        }
        .onDisappear {
            // The link holds the password: nothing of it stays once the code is off the screen.
            code = nil
            UIApplication.shared.isIdleTimerDisabled = idleTimerWasDisabled
        }
        .onReceive(NotificationCenter.default.publisher(for: UIScreen.capturedDidChangeNotification)) { _ in
            captured = ScreenCapture.active
        }
        .onReceive(tick) { _ in
            captured = ScreenCapture.active
        }
    }
    @ViewBuilder
    private func codeView(side: CGFloat) -> some View {
        if hidden {
            VStack(spacing: 8) {
                Image(systemName: "eye.slash").font(.system(size: 28))
                Text("Hidden while the screen is being shared").font(AppStyle.caption)
                    .multilineTextAlignment(.center)
            }.foregroundStyle(AppStyle.secondary).padding(12).frame(width: side, height: side)
                .background(AppStyle.surface, in: RoundedRectangle(cornerRadius: 8))
                .accessibilityElement(children: .combine).accessibilityIdentifier("qr.hidden")
        } else if let code {
            // Whole points a module, so that every module is as wide as the next and its edges
            // are sharp: a scanner reads a blurred or uneven grid badly.
            let module = max(1, (side / CGFloat(code.modules)).rounded(.down))
            let length = module * CGFloat(code.modules)
            Image(uiImage: code.image).interpolation(.none).antialiased(false).resizable()
                .frame(width: length, height: length)
                .accessibilityLabel("QR code").accessibilityIdentifier("qr.code")
        }
    }
}
