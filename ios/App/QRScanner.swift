import SwiftUI
import VisionKit

struct QRScanner: View {
    let received: (String) -> Void
    var body: some View {
        if DataScannerViewController.isSupported && DataScannerViewController.isAvailable {
            ScannerController(received: received).ignoresSafeArea()
        } else {
            ContentUnavailableView("Camera unavailable", systemImage: "qrcode.viewfinder", description: Text("Paste a link or import a file instead."))
        }
    }
}
private struct ScannerController: UIViewControllerRepresentable {
    let received: (String) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(received: received) }
    func makeUIViewController(context: Context) -> DataScannerViewController {
        let scanner = DataScannerViewController(recognizedDataTypes: [.barcode(symbologies: [.qr])],
            qualityLevel: .balanced, recognizesMultipleItems: false, isGuidanceEnabled: true, isHighlightingEnabled: true)
        scanner.delegate = context.coordinator
        do { try scanner.startScanning() } catch { scanner.stopScanning() }
        return scanner
    }
    func updateUIViewController(_ controller: DataScannerViewController, context: Context) {}
    static func dismantleUIViewController(_ controller: DataScannerViewController, coordinator: Coordinator) { controller.stopScanning() }
    final class Coordinator: NSObject, DataScannerViewControllerDelegate {
        let received: (String) -> Void
        private var delivered = false
        init(received: @escaping (String) -> Void) { self.received = received }
        func dataScanner(_ scanner: DataScannerViewController, didAdd addedItems: [RecognizedItem], allItems: [RecognizedItem]) {
            guard !delivered else { return }
            for item in addedItems {
                if case .barcode(let code) = item, let text = code.payloadStringValue, text.hasPrefix("tt://") {
                    delivered = true; scanner.stopScanning(); received(text); break
                }
            }
        }
    }
}
