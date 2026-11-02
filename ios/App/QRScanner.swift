import SwiftUI
import VisionKit

struct QRScanner: View {
    let received: (String) -> Void
    @State private var unavailable = false
    var body: some View {
        if !unavailable && DataScannerViewController.isSupported && DataScannerViewController.isAvailable {
            ScannerController(received: received, failed: { unavailable = true }).ignoresSafeArea()
        } else {
            ContentUnavailableView("Camera unavailable", systemImage: "qrcode.viewfinder", description: Text(
                "Paste a link or import a file instead."))
        }
    }
}
private struct ScannerController: UIViewControllerRepresentable {
    let received: (String) -> Void
    let failed: () -> Void
    func makeCoordinator() -> Coordinator { Coordinator(received: received, failed: failed) }
    func makeUIViewController(context: Context) -> DataScannerViewController {
        let scanner = DataScannerViewController(recognizedDataTypes: [.barcode(symbologies: [.qr])],
            qualityLevel: .balanced, recognizesMultipleItems: false, isGuidanceEnabled: true,
                isHighlightingEnabled: true)
        scanner.delegate = context.coordinator
        do { try scanner.startScanning() } catch {
            scanner.stopScanning()
            DispatchQueue.main.async { failed() }
        }
        return scanner
    }
    func updateUIViewController(_ controller: DataScannerViewController, context: Context) {}
    static func dismantleUIViewController(_ controller: DataScannerViewController, coordinator: Coordinator) {
        controller.stopScanning() }
    final class Coordinator: NSObject, DataScannerViewControllerDelegate {
        let received: (String) -> Void
        private var delivered = false
        let failed: () -> Void
        init(received: @escaping (String) -> Void, failed: @escaping () -> Void) {
            self.received = received
            self.failed = failed
        }
        func dataScanner(
            _ scanner: DataScannerViewController,
            becameUnavailableWithError error: DataScannerViewController.ScanningUnavailable
        ) {
            scanner.stopScanning()
            failed()
        }
        func dataScanner(
            _ scanner: DataScannerViewController,
            didAdd addedItems: [RecognizedItem],
            allItems: [RecognizedItem]
        ) {
            guard !delivered else { return }
            for item in addedItems {
                if case .barcode(let code) = item, let text = code.payloadStringValue, text.hasPrefix("tt://") {
                    delivered = true; scanner.stopScanning(); received(text); break
                }
            }
        }
    }
}
