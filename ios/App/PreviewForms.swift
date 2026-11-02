import SwiftUI

// Simulator screenshots only: no launch environment can activate this in Release.
struct PreviewForms: ViewModifier {
    @ObservedObject var state: AppState
    @State private var showing = false
    func body(content: Content) -> some View {
        #if DEBUG
        content.task {
            showing = state.isPreview && !(ProcessInfo.processInfo.environment["FT_PREVIEW_FORM"] ?? "").isEmpty
        }.sheet(isPresented: $showing) {
            Group {
                switch ProcessInfo.processInfo.environment["FT_PREVIEW_FORM"] {
                case "import": ImportView(state: state)
                case "new-server": NewServerView(state: state)
                case "edit-server":
                    if let profile = state.selected { ServerEditor(state: state, profile: profile) }
                case "add-list":
                    if let profile = state.selected { AddListView(state: state, profileID: profile.id) }
                case "licenses": NavigationStack { LicensesView() }
                default: EmptyView()
                }
            }.tint(AppStyle.accent).foregroundStyle(AppStyle.text).font(AppStyle.body)
        }
        #else
        content
        #endif
    }
}
