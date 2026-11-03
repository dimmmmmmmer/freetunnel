import SwiftUI

// Layout and palette mirror the desktop QML components.
enum AppStyle {
    static let body = Font.custom("Inter-Regular", size: 14, relativeTo: .body)
    static let label = Font.custom("Inter-Regular", size: 13, relativeTo: .subheadline)
    static let heading = Font.custom("Inter-Regular", size: 12, relativeTo: .caption)
    static let caption = Font.custom("Inter-Regular", size: 12, relativeTo: .caption)
    static let background = color(0xececec, 0x181818)
    static let surface = color(0xe2e2e2, 0x262626)
    static let tile = color(0xd8d8d8, 0x202020)
    static let input = color(0xd6d6d6, 0x101010)
    static let inputBorder = color(0xc2c2c2, 0x3a3a3a)
    static let text = color(0x1b1b1b, 0xeaeaea)
    static let secondary = color(0x6b6b6b, 0x9a9a9a)
    static let faint = color(0x9a9a9a, 0x6a6a6a)
    static let accent = color(0x4f4f4f, 0xb0b0b0)
    static let accentText = color(0xffffff, 0x181818)
    static let border = color(0xe5e5e5, 0x2e2e2e)
    static let danger = color(0xa32d2d, 0xe06a6a)
    static let success = color(0x1d9e75, 0x3fbf93)
    static let toggleOff = color(0xc4c4c4, 0x3a3a3a)
    static let toggleKnobOff = color(0xffffff, 0x4e4e4e)
    static let selected = Color(
        uiColor: UIColor { traits in
            let dark = traits.userInterfaceStyle == .dark
            return UIColor(white: dark ? 0.69 : 0.31, alpha: dark ? 0.16 : 0.12)
        })
    private static func color(_ light: UInt32, _ dark: UInt32) -> Color {
        Color(
            uiColor: UIColor { traits in
                let value = traits.userInterfaceStyle == .dark ? dark : light
                return UIColor(
                    red: Double((value >> 16) & 255) / 255,
                    green: Double((value >> 8) & 255) / 255, blue: Double(value & 255) / 255,
                    alpha: 1)
            })
    }
}
struct AppButtonStyle: ButtonStyle {
    var primary = false
    var dialog = false
    @Environment(\.isEnabled) private var enabled
    func makeBody(configuration: Configuration) -> some View {
        configuration.label.font(dialog ? AppStyle.body : AppStyle.label)
            .foregroundStyle(
                primary ? AppStyle.accentText : dialog ? AppStyle.text : AppStyle.accent
            )
            .padding(.horizontal, dialog ? 13 : 10).frame(
                minWidth: dialog ? 88 : nil, minHeight: 32
            )
            .background(
                primary ? AppStyle.accent : dialog ? AppStyle.background : AppStyle.surface,
                in: RoundedRectangle(cornerRadius: 8)
            )
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(AppStyle.border))
            .opacity(!enabled ? 0.45 : configuration.isPressed ? 0.65 : 1)
    }
}
struct AppToggleStyle: ToggleStyle {
    var minimumHeight: CGFloat = 42
    func makeBody(configuration: Configuration) -> some View {
        HStack(spacing: 8) {
            configuration.label
            Spacer(minLength: 8)
            Button {
                configuration.isOn.toggle()
            } label: {
                Capsule().fill(configuration.isOn ? AppStyle.accent : AppStyle.toggleOff)
                    .frame(width: 38, height: 22)
                    .overlay(alignment: configuration.isOn ? .trailing : .leading) {
                        Circle().fill(configuration.isOn ? .white : AppStyle.toggleKnobOff)
                            .frame(width: 16, height: 16).padding(3)
                    }
                    .animation(.easeOut(duration: 0.12), value: configuration.isOn)
            }.buttonStyle(.plain)
                .accessibilityLabel(Text("Toggle"))
                .accessibilityValue(Text(configuration.isOn ? "On" : "Off"))
                .accessibilityAddTraits(configuration.isOn ? [.isSelected] : [])
        }.frame(minHeight: minimumHeight).accessibilityRepresentation {
            Toggle(configuration).toggleStyle(.switch)
        }
    }
}
struct AppFieldStyle: TextFieldStyle {
    @ScaledMetric(relativeTo: .subheadline) private var fieldHeight = 36.0
    // swiftlint:disable:next identifier_name
    func _body(configuration: TextField<Self._Label>) -> some View {
        configuration.font(AppStyle.label).padding(.horizontal, 12).frame(minHeight: fieldHeight)
            .background(AppStyle.input, in: RoundedRectangle(cornerRadius: 8))
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(AppStyle.inputBorder))
    }
}
struct AppForm<Content: View>: View {
    @ViewBuilder let content: Content
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) { content }
                .frame(maxWidth: 600).frame(maxWidth: .infinity).padding(.horizontal, 18).padding(
                    .vertical, 10)
        }.scrollDismissesKeyboard(.interactively).background(AppStyle.background).buttonStyle(
            AppButtonStyle())
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
        VStack(alignment: .leading, spacing: 6) {
            Text(title).font(AppStyle.heading).foregroundStyle(AppStyle.faint).padding(.bottom, 4)
            content
        }.frame(maxWidth: .infinity, alignment: .leading)
    }
}
struct LabeledField: View {
    let title: LocalizedStringKey
    @Binding var text: String
    var placeholder = ""
    var secure = false
    var identifier = ""
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title).font(AppStyle.label).foregroundStyle(AppStyle.secondary)
            Group {
                if secure {
                    SecureField(placeholder, text: $text)
                } else {
                    TextField(placeholder, text: $text)
                }
            }.textFieldStyle(AppFieldStyle()).accessibilityLabel(title).accessibilityIdentifier(
                identifier)
        }
    }
}
struct AppChoice<Value: Hashable>: Identifiable {
    let value: Value
    let title: String
    var id: Value { value }
    init(_ value: Value, _ title: String) {
        self.value = value
        self.title = title
    }
}
struct AppPicker<Value: Hashable>: View {
    @Environment(\.dynamicTypeSize) private var typeSize
    let title: LocalizedStringKey
    @Binding var selection: Value
    let options: [AppChoice<Value>]
    var boxed = false
    var previewID: String?
    @State private var open = false
    @ScaledMetric(relativeTo: .body) private var rowHeight = 40.0
    @ScaledMetric(relativeTo: .body) private var fieldHeight = 36.0
    private var menuWidth: CGFloat {
        let font = UIFontMetrics.default.scaledFont(
            for: UIFont(name: "Inter-Regular", size: 14) ?? .systemFont(ofSize: 14))
        let width =
            options.map {
                (NSLocalizedString($0.title, comment: "") as NSString).size(withAttributes: [
                    .font: font
                ])
                .width
            }.max() ?? 0
        return min(330, max(170, width + 60))
    }
    var body: some View {
        Group {
            if boxed {
                VStack(alignment: .leading, spacing: 4) {
                    Text(title).font(AppStyle.label).foregroundStyle(AppStyle.secondary)
                    trigger.frame(maxWidth: .infinity, alignment: .leading).padding(.horizontal, 10)
                        .frame(
                            minHeight: fieldHeight
                        )
                        .background(AppStyle.input, in: RoundedRectangle(cornerRadius: 8))
                        .overlay(RoundedRectangle(cornerRadius: 8).stroke(AppStyle.inputBorder))
                }
            } else if typeSize.isAccessibilitySize {
                VStack(alignment: .leading, spacing: 6) {
                    Text(title)
                    trigger
                }.frame(maxWidth: .infinity, alignment: .leading).padding(.vertical, 6)
            } else {
                HStack(spacing: 8) {
                    Text(title)
                    Spacer(minLength: 8)
                    trigger
                }.frame(minHeight: 42)
            }
        }.task {
            #if DEBUG
                if ProcessInfo.processInfo.arguments.contains("--ui-preview"), let previewID,
                    ProcessInfo.processInfo.environment["FT_PREVIEW_MENU"] == previewID {
                    try? await Task.sleep(for: .milliseconds(500))
                    open = true
                }
            #endif
        }
    }
    private var trigger: some View {
        Button {
            open = true
        } label: {
            HStack(spacing: 5) {
                Text(
                    LocalizedStringKey(options.first { $0.value == selection }?.title ?? "Choose…")
                )
                .lineLimit(1).truncationMode(.tail)
                if boxed { Spacer(minLength: 0) }
                Image(systemName: "arrowtriangle.down.fill").font(.system(size: 7))
            }.foregroundStyle(boxed ? AppStyle.text : AppStyle.secondary)
        }.buttonStyle(.plain).accessibilityLabel(title)
            .accessibilityIdentifier(previewID.map { "picker-" + $0 } ?? "")
            .accessibilityValue(
                Text(
                    LocalizedStringKey(options.first { $0.value == selection }?.title ?? "Choose…"))
            )
            .popover(isPresented: $open) {
                ScrollView {
                    VStack(alignment: .leading, spacing: 0) {
                        ForEach(options) { option in
                            Button {
                                selection = option.value
                                open = false
                            } label: {
                                HStack(spacing: 12) {
                                    Text(LocalizedStringKey(option.title)).fixedSize(
                                        horizontal: false, vertical: true)
                                    Spacer(minLength: 8)
                                    if selection == option.value {
                                        Image(systemName: "checkmark").font(.system(size: 10))
                                    }
                                }.foregroundStyle(AppStyle.text).padding(.horizontal, 12).frame(
                                    minHeight: rowHeight)
                            }.buttonStyle(.plain)
                        }
                    }.font(AppStyle.body).padding(6)
                }.scrollBounceBehavior(.basedOnSize)
                    .frame(
                        width: menuWidth, height: min(360, CGFloat(options.count) * rowHeight + 12)
                    )
                    .fixedSize()
                    .presentationCompactAdaptation(.popover).presentationBackground(
                        AppStyle.background)
            }
    }
}
struct AppDialog<Content: View>: View {
    let title: LocalizedStringKey
    let close: () -> Void
    @ViewBuilder let content: Content
    var body: some View {
        ZStack {
            Color.black.opacity(0.45).ignoresSafeArea().onTapGesture(perform: close)
            VStack(alignment: .leading, spacing: 12) {
                HStack(spacing: 12) {
                    Button(action: close) {
                        Image(systemName: "arrow.left").frame(width: 22, height: 28)
                    }
                    .buttonStyle(.plain).accessibilityLabel("Back")
                    Text(title).font(.custom("Inter-Medium", size: 15, relativeTo: .headline))
                    Spacer(minLength: 0)
                }
                content
            }.padding(16).frame(maxWidth: 372)
                .background(AppStyle.background, in: RoundedRectangle(cornerRadius: 14))
                .overlay(RoundedRectangle(cornerRadius: 14).stroke(AppStyle.border))
                .padding(.horizontal, 14).padding(.vertical, 12)
        }.buttonStyle(AppButtonStyle()).toggleStyle(AppToggleStyle())
    }
}
extension View {
    func appCard() -> some View {
        padding(10).background(AppStyle.surface, in: RoundedRectangle(cornerRadius: 10))
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(AppStyle.border))
    }
    func appEditor() -> some View {
        scrollContentBackground(.hidden).padding(8)
            .background(AppStyle.input, in: RoundedRectangle(cornerRadius: 8))
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(AppStyle.inputBorder))
    }
}
struct AppIcon: View {
    let name: String
    var body: some View {
        Image(name).resizable().scaledToFit().frame(width: 17, height: 17)
            .frame(width: 26, height: 44).contentShape(Rectangle())
    }
}
