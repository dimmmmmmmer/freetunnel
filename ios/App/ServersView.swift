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
                }.buttonStyle(.plain).accessibilityLabel("Add a config").accessibilityIdentifier(
                    "server-add"
                )
                .popover(isPresented: $state.openAddMenu) { addMenu }
                Button {
                    Task { await state.pingServers() }
                } label: {
                    Image(systemName: "speedometer").font(.system(size: 19)).frame(
                        width: 40, height: 32)
                }.buttonStyle(.plain).disabled(state.pinging).accessibilityLabel("Check latency")
            }.padding(.top, 8)
            ScrollView {
                VStack(spacing: 0) {
                    ForEach(state.preferences.profiles) { profile in serverRow(profile) }
                    if state.preferences.profiles.isEmpty {
                        Button("Add a config") { state.openAddMenu = true }.buttonStyle(.plain)
                            .padding(
                                .top, 20)
                    }
                }.padding(.horizontal, 18)
            }
        }.background(AppStyle.background)
            .task {
                #if DEBUG
                    if state.isPreview
                        && ProcessInfo.processInfo.environment["FT_PREVIEW_MENU"] == "export" {
                        try? await Task.sleep(for: .milliseconds(500))
                        exportProfile = state.selected?.id
                    }
                #endif
            }
            .fileImporter(
                isPresented: $files, allowedContentTypes: [.plainText, .data],
                allowsMultipleSelection: true
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
            menuAction("Paste from clipboard") {
                state.confirmImport(UIPasteboard.general.string ?? "")
            }
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
            Text(title).frame(maxWidth: .infinity, minHeight: 40, alignment: .leading).padding(
                .horizontal, 9)
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
                            state.preferences.selectedID == profile.id
                                ? "Inter-Medium" : "Inter-Regular",
                            size: 14)
                    )
                    .lineLimit(1).truncationMode(.tail)
                }.frame(maxWidth: .infinity, minHeight: 56, alignment: .leading).contentShape(
                    Rectangle())
            }.buttonStyle(.plain).accessibilityIdentifier("server-select-" + profile.name)
            if let ping = state.pings[profile.id] {
                Text(ping).font(AppStyle.caption).foregroundStyle(AppStyle.secondary)
            }
            if state.selected?.id == profile.id && !vpn.isDisconnected
                && vpn.status != .disconnecting {
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
                moveServer(identifiers, to: profile)
            }
            .contextMenu {
                if profile.subscriptionEnabled {
                    Button("Refresh subscription") {
                        Task { await state.refreshSubscription(profile) }
                    }
                }
            }
    }
    private func moveServer(_ identifiers: [String], to profile: Profile) -> Bool {
                guard let id = identifiers.first,
                    let source = state.preferences.profiles.firstIndex(where: {
                        $0.id.uuidString == id
                    }),
                    let target = state.preferences.profiles.firstIndex(where: {
                        $0.id == profile.id
                    })
                else { return false }
                state.preferences.profiles.move(
                    fromOffsets: IndexSet(integer: source),
                    toOffset: target > source ? target + 1 : target)
                return true
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
                        exportDocument = ConfigDocument(
                            text: try DeepLinkEncoder.exportTOML(config))
                        exportName = profile.name.replacingOccurrences(of: "/", with: "_")
                            .replacingOccurrences(of: "\\", with: "_")
                        exporting = true
                    }
                    exportAction("Copy deep-link") {
                        UIPasteboard.general.string = try DeepLinkEncoder.encode(
                            Configuration(state.secret(profile).toml))
                        state.message = "Link copied"
                    }
                }.padding(6).frame(width: 230).foregroundStyle(AppStyle.text)
                    .presentationCompactAdaptation(.popover)
                    .presentationBackground(AppStyle.background)
            }
            Button {
                state.dialog = .server(profile.id)
            } label: {
                AppIcon(name: "ActionEdit")
            }.accessibilityLabel("Edit").accessibilityIdentifier("server-edit-" + profile.name)
            Button {
                state.confirm(
                    String(
                        format: NSLocalizedString("Delete config “%@”?", comment: ""), profile.name)
                ) {
                    do { try state.remove(profile) } catch { state.report(error) }
                }
            } label: {
                AppIcon(name: "ActionClose")
            }.accessibilityLabel("Delete").accessibilityIdentifier("server-delete-" + profile.name)
        }.buttonStyle(.plain).foregroundStyle(AppStyle.secondary)
    }
    private func exportAction(_ title: LocalizedStringKey, action: @escaping () throws -> Void)
        -> some View {
        Button {
            exportProfile = nil
            do { try action() } catch { state.report(error) }
        } label: {
            Text(title).frame(maxWidth: .infinity, minHeight: 40, alignment: .leading).padding(
                .horizontal, 9)
        }
        .buttonStyle(.plain)
    }
}
struct ConfigDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.plainText] }
    var text: String
    init(text: String) { self.text = text }
    init(configuration: ReadConfiguration) throws {
        guard
            let decoded = String(
                data: configuration.file.regularFileContents ?? Data(), encoding: .utf8)
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
                    if !subscriptionOnly {
                        Button("Paste") { text = UIPasteboard.general.string ?? "" }
                    }
                    Spacer()
                    Button(subscriptionOnly ? "Add" : "Import") {
                        if subscriptionOnly {
                            state.subscribe(text)
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
        do {
            var batch: [ImportDocument.Document] = []
            for item in try ImportDocument.split(text) {
                do { batch.append(try ImportDocument.parse(item)) } catch { report(error) }
            }
            enqueueImports(batch)
        } catch { report(error) }
    }
    func enqueueImports(_ batch: [ImportDocument.Document]) {
        guard batch.count + pendingImports.count + (importing ? 1 : 0) <= 100 else {
            report(ClientError.tooLarge); return
        }
        pendingImports += batch
        nextImport()
    }
    func nextImport() {
        guard !importing, confirmation == nil, !pendingImports.isEmpty else { return }
        importing = true
        let document = pendingImports.removeFirst()
        if let source = document.subscription {
            confirm(String(format: NSLocalizedString("Fetch subscription from %@?", comment: ""), URLComponents(string: source)?.host ?? ""), actionTitle: "Continue") {
                Task {
                    do {
                        let data = try await Downloads.read(Subscription.request(source), limit: Subscription.maximumBytes)
                        self.offerImportedConfig(try Subscription.apply(data), subscription: source)
                    } catch { self.report(error); self.finishImport() }
                }
            }
            confirmation?.cancelAction = { self.finishImport() }
        } else if let toml = document.toml {
            offerImportedConfig(toml, subscription: nil)
        } else { finishImport() }
    }
    func finishImport() {
        importing = false
        nextImport()
    }
    func offerImportedConfig(_ text: String, subscription: String?) {
        do {
            let config = try Configuration(text)
            let existing = preferences.profiles.first { $0.name.caseInsensitiveCompare(config.name) == .orderedSame }
            let title = existing == nil ? "Add" : "Replace"
            var prompt = NSLocalizedString("Import this configuration?", comment: "") + "\n" + config.name
            if existing != nil { prompt += "\n" + NSLocalizedString("A configuration with this name already exists.", comment: "") }
            if config.skipsVerification {
                prompt += "\n" + NSLocalizedString("This configuration may disable certificate verification. Import it only if you trust its source.", comment: "")
            }
            let save: (UUID?) -> Void = { id in
                do {
                    if let id, var profile = self.preferences.profiles.first(where: { $0.id == id }) {
                        let record = Secret(toml: try config.storingImportedRulesInProfile(), subscription: subscription)
                        try Vault.write(JSONEncoder().encode(record), key: id.uuidString)
                        profile.subscriptionEnabled = subscription != nil
                        profile.subscriptionUpdated = subscription == nil ? nil : Date()
                        self.update(profile)
                        if self.selected?.id == id { self.reapplySelected() }
                    } else {
                        try self.add(toml: text, subscription: subscription)
                    }
                    self.dialog = nil
                    self.message = "Imported"
                } catch { self.report(error) }
                self.finishImport()
            }
            confirm(prompt, actionTitle: title) { save(existing?.id) }
            confirmation?.cancelAction = { self.finishImport() }
            if existing != nil {
                confirmation?.alternateTitle = "Add copy"
                confirmation?.alternateAction = { save(nil) }
            }
        } catch { report(error); finishImport() }
    }
}
