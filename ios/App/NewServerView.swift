import FreeTunnelCore
import SwiftUI
import UniformTypeIdentifiers

struct ServerEditor: View {
    @ObservedObject var state: AppState
    var profile: Profile?
    @State private var fields = EditorFields()
    @State private var original: Configuration?
    @State private var initial = EditorFields()
    @State private var splitProfile = "Default"
    @State private var initialProfile = "Default"
    @State private var ready = false
    @State private var importingCertificate = false
    private var changed: Bool { fields != initial || splitProfile != initialProfile }
    private func close() {
        if changed {
            state.confirm("Discard unsaved changes?", actionTitle: "Discard") { state.dialog = nil }
        } else {
            state.dialog = nil
        }
    }
    var body: some View {
        AppDialog(title: profile == nil ? "New config" : "Edit config", close: close) {
            ScrollViewReader { reader in
                ScrollView {
                    VStack(alignment: .leading, spacing: 12) {
                        LabeledField(
                            title: "Config name", text: $fields.name,
                            placeholder: String(localized: "Germany · Frankfurt"),
                            identifier: "server-name")
                        LabeledField(
                            title: "Server host", text: $fields.hostname,
                            placeholder: "frankfurt.example.com",
                            identifier: "server-host"
                        )
                        LabeledField(
                            title: "Address(es) · host:port (comma-separated)",
                            text: $fields.addresses,
                            placeholder: "1.2.3.4:443", identifier: "server-addresses")
                        HStack(alignment: .top, spacing: 10) {
                            LabeledField(
                                title: "Username", text: $fields.username, identifier: "server-user"
                            )
                            LabeledField(
                                title: "Password", text: $fields.password, secure: true,
                                identifier: "server-password")
                        }
                        HStack(alignment: .top, spacing: 10) {
                            AppPicker(
                                title: "Protocol", selection: $fields.proto,
                                options: [
                                    .init("http2", "HTTP/2"),
                                    .init("http3", "HTTP/3")
                                ], boxed: true
                            ).frame(maxWidth: .infinity)
                            LabeledField(
                                title: "DNS servers", text: $fields.dns,
                                placeholder: "1.1.1.1, 8.8.8.8"
                            )
                            .frame(maxWidth: .infinity)
                        }
                        HStack(alignment: .top, spacing: 10) {
                            LabeledField(title: "Custom SNI", text: $fields.sni)
                            LabeledField(title: "Client random (hex)", text: $fields.random)
                        }
                        AppPicker(
                            title: "Split profile", selection: $splitProfile,
                            options: state.preferences.splitProfiles.map {
                                .init($0.name, $0.name)
                            },
                            boxed: true)
                        VStack(spacing: 0) {
                            Toggle("Allow IPv6", isOn: $fields.ipv6)
                            Toggle("Skip certificate check", isOn: $fields.skipVerification)
                            Toggle("Anti-DPI", isOn: $fields.antiDPI)
                        }.font(AppStyle.label).foregroundStyle(AppStyle.secondary)
                        VStack(alignment: .leading, spacing: 4) {
                            HStack {
                                Text("Certificate (PEM) · optional").foregroundStyle(
                                    AppStyle.secondary)
                                Spacer(minLength: 4)
                                Button("Load from file…") { importingCertificate = true }
                                    .buttonStyle(.plain)
                            }.font(AppStyle.caption)
                            TextEditor(text: $fields.certificate).font(
                                .system(size: 11, design: .monospaced)
                            )
                            .frame(height: 84).appEditor()
                        }
                        HStack(spacing: 8) {
                            Spacer()
                            Button("Cancel", action: close).buttonStyle(
                                AppButtonStyle(dialog: true))
                            Button("Save") {
                                do {
                                    if changed || profile == nil {
                                        try state.saveServer(
                                            fields, original: original, profile: profile,
                                            splitProfile: splitProfile)
                                    }
                                    state.dialog = nil
                                } catch { state.report(error) }
                            }.buttonStyle(AppButtonStyle(primary: true, dialog: true)).disabled(
                                !ready
                            )
                            .accessibilityIdentifier("server-save")
                        }.padding(.top, 6).id("editor-bottom")
                    }.textInputAutocapitalization(.never).autocorrectionDisabled()
                }.scrollDismissesKeyboard(.interactively).task {
                    guard !ready else { return }
                    do {
                        if let profile {
                            let config = try Configuration(state.secret(profile).toml)
                            original = config
                            fields = EditorFields(config)
                            fields.name = profile.name
                            splitProfile = profile.splitProfile
                        }
                        initial = fields
                        initialProfile = splitProfile
                        ready = true
                        #if DEBUG
                            if state.isPreview
                                && ProcessInfo.processInfo.environment["FT_PREVIEW_BOTTOM"] == "1" {
                                try? await Task.sleep(for: .milliseconds(300))
                                reader.scrollTo("editor-bottom", anchor: .bottom)
                            }
                        #endif
                    } catch { state.report(error) }
                }
            }
        }.fileImporter(
            isPresented: $importingCertificate, allowedContentTypes: [.data, .plainText]
        ) { result in
            do {
                let url = try result.get()
                let access = url.startAccessingSecurityScopedResource()
                defer { if access { url.stopAccessingSecurityScopedResource() } }
                guard (try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0) <= 1_048_576
                else {
                    throw ClientError.tooLarge
                }
                let data = try Data(contentsOf: url)
                guard data.count <= 1_048_576, let text = String(data: data, encoding: .utf8),
                    text.contains("-----BEGIN CERTIFICATE-----"), !text.contains("PRIVATE KEY")
                else {
                    throw ClientError.invalidConfig
                }
                fields.certificate = text
            } catch { state.report(error) }
        }
    }
}
