import SwiftUI

struct PreviewForms: ViewModifier {
    @ObservedObject var state: AppState
    func body(content: Content) -> some View {
        content.task {
            #if DEBUG
            guard state.isPreview else { return }
            switch ProcessInfo.processInfo.environment["FT_PREVIEW_FORM"] {
            case "import": state.openAddMenu = true
            case "new-server": state.dialog = .server(nil)
            case "edit-server": state.dialog = .server(state.selected?.id)
            default: break
            }
            #endif
        }
    }
}
