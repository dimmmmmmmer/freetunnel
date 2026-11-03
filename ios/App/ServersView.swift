import FreeTunnelCore
import SwiftUI
import UniformTypeIdentifiers

struct ServersView: View {
    @ObservedObject var state: AppState
    @ObservedObject var vpn: VPNController
    @State private var files = false
    @State private var scanning = false
    @State private var exportProfile: UUID?
    @State private var exportDocument: ConfigDocument?
    @State private var exporting = false
    @State private var exportName = "config"
    var body: some View {
        VStack(spacing: 8) {
            HStack(spacing: 16) {
                Button {
                    state.openAddMenu.toggle()
                } label: {
                    Image(systemName: "plus").font(.system(size: 21, weight: .light)).frame(
                        width: 40, height: 32)
                }.buttonStyle(.plain).accessibilityLabel("Add a config")
                    .popover(isPresented: $state.openAddMenu) { addMenu }
                Button {
                    Task { await state.pingServers() }
                } label: {
                    Image(systemName: "speedometer").font(.system(size: 19)).frame(width: 40, height: 32)
                }.buttonStyle(.plain).disabled(state.pinging).accessibilityLabel("Check latency")
            }.padding(.top, 8)
            ScrollView {
                VStack(spacing: 0) {
                    ForEach(state.preferences.profiles) { profile in serverRow(profile) }
                    if state.preferences.profiles.isEmpty {
                        Button("Add a config") { state.openAddMenu = true }.buttonStyle(.plain).padding(
                            .top, 20)
                    }
                }.padding(.horizontal, 18)
            }
        }.background(AppStyle.background)
            .task {
                #if DEBUG
                    if state.isPreview && ProcessInfo.processInfo.environment["FT_PREVIEW_MENU"] == "export" {
                        try? await Task.sleep(for: .milliseconds(500))
                        exportProfile = state.selected?.id
                    }
                #endif
            }
            .fileImporter(
                isPresented: $files, allowedContentTypes: [.plainText, .data], allowsMultipleSelection: true
            ) { result in
                switch result {
                case .success(let urls): state.importFiles(urls)
                case .failure(let error): state.report(error)
                }
            }
            .fileExporter(
                isPresented: $exporting, document: exportDocument, contentType: .plainText,
                defaultFilename: exportName + ".toml"
            ) { result in
                if case .failure(let error) = result { state.report(error) }
            }
            .sheet(isPresented: $scanning) {
                QRScanner { text in
                    scanning = false
                    state.confirmImport(text)
                }
            }
    }
    private var addMenu: some View {
        VStack(alignment: .leading, spacing: 0) {
            menuAction("Paste from clipboard") { state.confirmImport(UIPasteboard.general.string ?? "") }
            menuAction("From file…") { files = true }
            Divider().padding(.vertical, 5)
            menuAction("Create new…") { state.dialog = .server(nil) }
            Divider().padding(.vertical, 5)
            menuAction("Scan QR code") { scanning = true }
            menuAction("Subscription") { state.dialog = .subscription }
        }.padding(6).frame(width: 250).font(AppStyle.body)
            .presentationCompactAdaptation(.popover).presentationBackground(AppStyle.background)
    }
    private func menuAction(_ title: LocalizedStringKey, action: @escaping () -> Void) -> some View {
        Button {
            state.openAddMenu = false
            action()
        } label: {
            Text(title).frame(maxWidth: .infinity, minHeight: 40, alignment: .leading).padding(.horizontal, 9)
        }.buttonStyle(.plain)
    }
    private func serverRow(_ profile: Profile) -> some View {
        HStack(spacing: 8) {
            Button {
                state.select(profile.id)
            } label: {
                HStack(spacing: 8) {
                    Image("TunnelLogo").resizable().scaledToFit().frame(width: 22, height: 22)
                        .opacity(state.preferences.selectedID == profile.id ? 1 : 0.4)
                    Text(profile.name).font(
                        .custom(
                            state.preferences.selectedID == profile.id ? "Inter-Medium" : "Inter-Regular",
                            size: 14)
                    )
                    .lineLimit(1).truncationMode(.tail)
                }.frame(maxWidth: .infinity, minHeight: 56, alignment: .leading).contentShape(Rectangle())
            }.buttonStyle(.plain)
            if let ping = state.pings[profile.id] {
                Text(ping).font(AppStyle.caption).foregroundStyle(AppStyle.secondary)
            }
            if state.selected?.id == profile.id && !vpn.isDisconnected && vpn.status != .disconnecting {
                Text(vpn.status == .connected ? "connected" : "connecting…").font(
                    .custom("Inter-Medium", size: 11)
                )
                .padding(.horizontal, 8).padding(.vertical, 3)
                .foregroundStyle(vpn.status == .connected ? AppStyle.success : AppStyle.secondary)
                .background(AppStyle.selected, in: Capsule())
            }
            serverActions(profile)
        }.padding(.leading, 10).padding(.trailing, 6)
            .draggable(profile.id.uuidString)
            .dropDestination(for: String.self) { identifiers, _ in
                guard let id = identifiers.first,
                    let source = state.preferences.profiles.firstIndex(where: {
                        $0.id.uuidString == id
                    }),
                    let target = state.preferences.profiles.firstIndex(where: {
                        $0.id == profile.id
                    })
                else { return false }
                state.preferences.profiles.move(
                    fromOffsets: IndexSet(integer: source), toOffset: target > source ? target + 1 : target)
                return true
            }
            .contextMenu {
                if profile.subscriptionEnabled {
                    Button("Refresh subscription") { Task { await state.refreshSubscription(profile) } }
                }
            }
    }
    private func serverActions(_ profile: Profile) -> some View {
        HStack(spacing: 0) {
            Button {
                exportProfile = profile.id
            } label: {
                AppIcon(name: "ActionExport")
            }
            .accessibilityLabel("Export config")
            .popover(
                isPresented: Binding(
                    get: { exportProfile == profile.id }, set: { if !$0 { exportProfile = nil } })
            ) {
                VStack(alignment: .leading, spacing: 0) {
                    exportAction("Export .toml…") {
                        let config = try Configuration(state.secret(profile).toml)
                        exportDocument = ConfigDocument(text: try DeepLinkEncoder.exportTOML(config))
                        exportName = profile.name.replacingOccurrences(of: "/", with: "_")
                            .replacingOccurrences(of: "\\", with: "_")
                        exporting = true
                    }
                    exportAction("Copy deep-link") {
                        UIPasteboard.general.string = try DeepLinkEncoder.encode(
                            Configuration(state.secret(profile).toml))
                        state.message = "Link copied"
                    }
                }.padding(6).frame(width: 230).presentationCompactAdaptation(.popover)
                    .presentationBackground(AppStyle.background)
            }
            Button {
                state.dialog = .server(profile.id)
            } label: {
                AppIcon(name: "ActionEdit")
            }.accessibilityLabel("Edit")
            Button {
                state.confirm(
                    String(format: NSLocalizedString("Delete config “%@”?", comment: ""), profile.name)
                ) {
                    do { try state.remove(profile) } catch { state.report(error) }
                }
            } label: {
                AppIcon(name: "ActionClose")
            }.accessibilityLabel("Delete")
        }.buttonStyle(.plain).foregroundStyle(AppStyle.secondary)
    }
    private func exportAction(_ title: LocalizedStringKey, action: @escaping () throws -> Void) -> some View {
        Button {
            exportProfile = nil
            do { try action() } catch { state.report(error) }
        } label: {
            Text(title).frame(maxWidth: .infinity, minHeight: 40, alignment: .leading).padding(.horizontal, 9)
        }
        .buttonStyle(.plain)
    }
}
struct ConfigDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.plainText] }
    var text: String
    init(text: String) { self.text = text }
    init(configuration: ReadConfiguration) throws {
        guard let decoded = String(data: configuration.file.regularFileContents ?? Data(), encoding: .utf8)
        else {
            throw ClientError.invalidConfig
        }
        text = decoded
    }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: Data(text.utf8))
    }
}
struct ImportView: View {
    @ObservedObject var state: AppState
    var subscriptionOnly = false
    @State private var text = ""
    private func close() { state.dialog = nil }
    var body: some View {
        AppDialog(title: subscriptionOnly ? "Subscription" : "Import", close: close) {
            VStack(alignment: .leading, spacing: 12) {
                if subscriptionOnly {
                    LabeledField(title: "HTTPS subscription URL", text: $text)
                } else {
                    TextEditor(text: $text).frame(height: 180).appEditor().font(
                        .system(size: 12, design: .monospaced))
                }
                HStack {
                    if !subscriptionOnly { Button("Paste") { text = UIPasteboard.general.string ?? "" } }
                    Spacer()
                    Button(subscriptionOnly ? "Add" : "Import") {
                        if subscriptionOnly {
                            Task {
                                await state.subscribe(text)
                                if state.message == NSLocalizedString("Imported", comment: "") {
                                    state.dialog = nil
                                }
                            }
                        } else {
                            state.confirmImport(text)
                        }
                    }.buttonStyle(AppButtonStyle(primary: true)).disabled(text.isEmpty)
                }
            }.textInputAutocapitalization(.never).autocorrectionDisabled()
        }
    }
}
@MainActor extension AppState {
    func confirmImport(_ text: String) {
        guard !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            report(ClientError.invalidConfig)
            return
        }
        confirm("Import this configuration?", actionTitle: "Add") {
            do {
                _ = try self.importBatch(text)
                self.dialog = nil
                self.message = "Imported"
            } catch ClientError.insecureConfig {
                self.confirm(
                    "This configuration may disable certificate verification. Import it only if you trust its source.",
                    actionTitle: "Add"
                ) {
                    do {
                        _ = try self.importBatch(text, allowInsecure: true)
                        self.dialog = nil
                        self.message = "Imported"
                    } catch { self.report(error) }
                }
            } catch { self.report(error) }
        }
    }
}
