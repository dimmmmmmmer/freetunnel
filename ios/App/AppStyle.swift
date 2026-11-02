import SwiftUI

// Keep these semantic colors in sync with the theme in qml/Main.qml.
enum AppStyle {
    static let body = Font.custom("Inter-Regular", size: 16, relativeTo: .body)
    static let label = Font.custom("Inter-Regular", size: 14, relativeTo: .subheadline)
    static let heading = Font.custom("Inter-Medium", size: 16, relativeTo: .headline)
    static let caption = Font.custom("Inter-Regular", size: 12, relativeTo: .caption)
    static let background = color(0xececec, 0x181818)
    static let surface = color(0xe2e2e2, 0x262626)
    static let input = color(0xd6d6d6, 0x101010)
    static let inputBorder = color(0xc2c2c2, 0x3a3a3a)
    static let text = color(0x1b1b1b, 0xeaeaea)
    static let secondary = color(0x6b6b6b, 0x9a9a9a)
    static let accent = color(0x4f4f4f, 0xb0b0b0)
    static let border = color(0xe5e5e5, 0x2e2e2e)
    static let danger = color(0xa32d2d, 0xe06a6a)

    private static func color(_ light: UInt32, _ dark: UInt32) -> Color {
        Color(uiColor: UIColor { traits in
            let value = traits.userInterfaceStyle == .dark ? dark : light
            return UIColor(red: Double((value >> 16) & 255) / 255,
                green: Double((value >> 8) & 255) / 255, blue: Double(value & 255) / 255, alpha: 1)
        })
    }
}

struct AppButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var enabled
    func makeBody(configuration: Configuration) -> some View {
        configuration.label.font(AppStyle.label)
            .foregroundStyle(configuration.role == .destructive ? AppStyle.danger : AppStyle.accent)
            .padding(.horizontal, 12).frame(minHeight: 44)
            .background(AppStyle.surface, in: RoundedRectangle(cornerRadius: 8))
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(AppStyle.border))
            .opacity(!enabled ? 0.45 : configuration.isPressed ? 0.65 : 1)
    }
}

struct AppFieldStyle: TextFieldStyle {
    func _body(configuration: TextField<Self._Label>) -> some View {
        configuration.padding(12).frame(minHeight: 44)
            .background(AppStyle.input, in: RoundedRectangle(cornerRadius: 8))
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(AppStyle.inputBorder))
    }
}

struct AppForm<Content: View>: View {
    @ViewBuilder let content: Content
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) { content }
                .frame(maxWidth: 600).frame(maxWidth: .infinity).padding(20)
        }.background(AppStyle.background)
            .buttonStyle(AppButtonStyle()).textFieldStyle(AppFieldStyle())
            .navigationBarTitleDisplayMode(.inline)
    }
}

struct AppSection<Content: View>: View {
    let title: LocalizedStringKey
    @ViewBuilder let content: Content
    init(_ title: LocalizedStringKey, @ViewBuilder content: () -> Content) {
        self.title = title
        self.content = content()
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(title).font(AppStyle.heading).foregroundStyle(AppStyle.secondary)
            content
        }.frame(maxWidth: .infinity, alignment: .leading)
    }
}

extension View {
    func appCard() -> some View {
        padding(12).background(AppStyle.surface, in: RoundedRectangle(cornerRadius: 10))
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(AppStyle.border))
    }
    func appEditor() -> some View {
        scrollContentBackground(.hidden).padding(8)
            .background(AppStyle.input, in: RoundedRectangle(cornerRadius: 8))
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(AppStyle.inputBorder))
    }
}

struct AppPicker<Selection: Hashable, Content: View>: View {
    let title: LocalizedStringKey
    @Binding var selection: Selection
    let content: Content
    init(_ title: LocalizedStringKey, selection: Binding<Selection>, @ViewBuilder content: () -> Content) {
        self.title = title
        self._selection = selection
        self.content = content()
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title).font(AppStyle.label).foregroundStyle(AppStyle.secondary)
            Picker(title, selection: $selection) { content }
                .labelsHidden().pickerStyle(.menu).frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal, 8).frame(minHeight: 44)
                .background(AppStyle.input, in: RoundedRectangle(cornerRadius: 8))
                .overlay(RoundedRectangle(cornerRadius: 8).stroke(AppStyle.inputBorder))
        }
    }
}
