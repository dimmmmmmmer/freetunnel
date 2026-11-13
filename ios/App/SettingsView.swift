import FreeTunnelCore
import SwiftUI

struct SettingsView: View {
    @Environment(\.dynamicTypeSize) private var typeSize
    @ObservedObject var state: AppState
    private var routes: Binding<String> {
        Binding(
            get: { state.preferences.excludedRoutes.joined(separator: "\n") },
            set: { value in
                state.preferences.excludedRoutes = value.split(whereSeparator: { $0.isNewline })
                    .map(
                        String.init)
                state.reapplySelected()
            })
    }
    var body: some View {
        ScrollViewReader { reader in
            AppForm {
                AppSection("General") {
                    VStack(spacing: 0) {
                        AppPicker(
                            title: "Language", selection: $state.preferences.language,
                            options: [
                                .init("system", "System"), .init("en", "English"),
                                .init("ru", "Русский")
                            ], previewID: "language")
                        Divider().overlay(AppStyle.border)
                        AppPicker(
                            title: "Theme", selection: $state.preferences.theme,
                            options: [
                                .init("system", "System"), .init("light", "Light"),
                                .init("dark", "Dark")
                            ], previewID: "theme")
                        Divider().overlay(AppStyle.border)
                        Toggle("Connect on startup", isOn: $state.preferences.autoConnect)
                    }
                }
                AppSection("Security") {
                    Toggle(isOn: $state.preferences.killSwitch) {
                        VStack(alignment: .leading, spacing: 0) {
                            Text("Kill switch")
                            Text("block traffic if the VPN drops").font(AppStyle.caption)
                                .foregroundStyle(
                                    AppStyle.faint)
                        }
                    }.onChange(of: state.preferences.killSwitch) { _, _ in state.reapplySelected() }
                }
                VStack(alignment: .leading, spacing: 6) {
                    (typeSize.isAccessibilitySize
                        ? AnyLayout(VStackLayout(alignment: .leading, spacing: 8))
                        : AnyLayout(HStackLayout(spacing: 8))) {
                        Text("Excluded routes").foregroundStyle(AppStyle.faint)
                        if !typeSize.isAccessibilitySize { Spacer(minLength: 0) }
                        Button("Restore defaults") {
                            state.confirm(
                                "Replace the excluded routes with the defaults?",
                                actionTitle: "Replace"
                            ) {
                                state.preferences.excludedRoutes = Preferences.defaultExcludedRoutes
                                state.reapplySelected()
                            }
                        }
                        Button("Clear all") {
                            state.confirm("Clear all excluded routes?", actionTitle: "Clear") {
                                state.preferences.excludedRoutes = []
                                state.reapplySelected()
                            }
                        }.foregroundStyle(AppStyle.danger)
                    }.font(AppStyle.caption).buttonStyle(.plain)
                    RuleChips(
                        rules: routes, placeholder: "IP or subnet (e.g. 10.0.0.0/8), then Enter",
                        ipOnly: true,
                        identifier: "excluded-routes"
                    )
                }
                AppSection("Logging") {
                    VStack(spacing: 0) {
                        Toggle("Enable logging", isOn: $state.preferences.loggingEnabled)
                        Divider().overlay(AppStyle.border)
                        Toggle(isOn: $state.preferences.verboseLogs) {
                            VStack(alignment: .leading, spacing: 0) {
                                Text("Verbose logs")
                                Text("full VPN core detail for debugging (noisy)").font(
                                    AppStyle.caption
                                )
                                .foregroundStyle(AppStyle.faint)
                            }
                        }.disabled(!state.preferences.loggingEnabled)
                    }
                }.onChange(of: state.preferences.loggingEnabled) { _, _ in updateLogging() }
                    .onChange(of: state.preferences.verboseLogs) { _, _ in updateLogging() }
                AppSection("Maintenance") {
                    Button {
                        Task { await state.checkUpdates() }
                    } label: {
                        HStack {
                            Text(LocalizedStringKey(state.updateStatus))
                            Spacer()
                            if state.checkingUpdate {
                                ProgressView().controlSize(.small)
                            } else {
                                Text(
                                    Bundle.main.object(
                                        forInfoDictionaryKey: "CFBundleShortVersionString")
                                        as? String ?? "1.2.4"
                                ).foregroundStyle(AppStyle.faint)
                            }
                        }.frame(minHeight: 42)
                    }.buttonStyle(.plain).disabled(state.checkingUpdate)
                }
                HStack(spacing: 8) {
                    Link(
                        "FreeTunnel "
                            + (Bundle.main.object(
                                forInfoDictionaryKey: "CFBundleShortVersionString")
                                as? String ?? "1.2.4"),
                        destination: URL(string: "https://github.com/dimmmmmmmer/freetunnel")!)
                    Text("·")
                    Link(
                        String(localized: "TrustTunnel core ") + "1.1.12",
                        destination: URL(
                            string: "https://github.com/TrustTunnel/TrustTunnelClient")!)
                }.font(AppStyle.caption).foregroundStyle(AppStyle.faint).buttonStyle(.plain)
                    .frame(maxWidth: .infinity).padding(.vertical, 14).id("settings-bottom")
            }.task {
                #if DEBUG
                    if state.isPreview
                        && ProcessInfo.processInfo.environment["FT_PREVIEW_BOTTOM"] == "1" {
                        try? await Task.sleep(for: .milliseconds(300))
                        reader.scrollTo("settings-bottom", anchor: .bottom)
                    }
                #endif
            }
        }
    }
    private func updateLogging() {
        Task {
            await state.vpn.configureLogging(
                enabled: state.preferences.loggingEnabled, verbose: state.preferences.verboseLogs)
        }
    }
}
