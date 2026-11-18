import SwiftUI

// A sheet presents its own alerts; presenting them underneath it hides errors.
struct FormAlerts: ViewModifier {
    @ObservedObject var state: AppState
    @State private var identifier = UUID()
    func body(content: Content) -> some View {
        content
            .onAppear { state.openDialogs.insert(identifier) }
            .onDisappear { state.openDialogs.remove(identifier) }
            .alert(
                "FreeTunnel",
                isPresented: Binding(
                    get: { state.message != nil },
                    set: { if !$0 { state.message = nil } }
                )
            ) {
                Button("OK") { state.message = nil }
            } message: {
                Text(LocalizedStringKey(state.message ?? ""))
            }
    }
}
