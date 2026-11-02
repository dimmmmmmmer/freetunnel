import SwiftUI
import UniformTypeIdentifiers
import FreeTunnelCore

struct ServersView: View {
    @ObservedObject var state: AppState
    @State private var adding = false
    @State private var creating = false
    @State private var files = false
    @State private var editing: Profile?
    @State private var removing: Profile?
    var body: some View {
        NavigationStack {
            List {
                ForEach(state.preferences.profiles) { profile in
                    HStack {
                        Button { if state.vpn.isDisconnected { state.preferences.selectedID = profile.id } } label: {
                            HStack {
                                Image(systemName: state.preferences.selectedID == profile.id
                                      ? "checkmark.circle.fill" : "circle")
                                VStack(alignment: .leading) {
                                    Text(profile.name).foregroundStyle(.primary)
                                    if profile.subscriptionEnabled { Text("Subscription").font(AppStyle.caption)
                                        .foregroundStyle(.secondary) }
                                }
                            }
                        }.buttonStyle(.plain)
                        Spacer()
                        Menu {
                            Button("Edit") { editing = profile }
                            if profile.subscriptionEnabled {
                                Button("Refresh subscription") { Task { await state.refreshSubscription(profile) } }
                            }
                            Button("Remove", role: .destructive) { removing = profile }
                        } label: { Image(systemName: "ellipsis.circle").padding(8) }
                    }.appCard().listRowBackground(Color.clear).listRowSeparator(.hidden)
                }.onMove { source, destination in state.preferences.profiles.move(fromOffsets: source,
                    toOffset: destination) }
                if state.preferences.profiles.isEmpty { Text("Add a server to get connected").foregroundStyle(
                    .secondary) }
            }.listStyle(.plain).scrollContentBackground(.hidden).background(AppStyle.background)
                .navigationBarTitleDisplayMode(.inline).navigationTitle("Servers")
                .toolbar {
                    ToolbarItem(placement: .topBarLeading) { EditButton() }
                    ToolbarItem(placement: .topBarTrailing) {
                        Menu {
                            Button("New configuration") { creating = true }
                            Button("Paste links or TOML") { adding = true }
                            Button("Import files") { files = true }
                        } label: { Label("Add server", systemImage: "plus") }
                    }
                }
                .fileImporter(isPresented: $files, allowedContentTypes: [.plainText, .data],
                    allowsMultipleSelection: true) { result in
                    switch result {
                    case .success(let urls): state.importFiles(urls)
                    case .failure(let error): state.report(error)
                    }
                }
                .sheet(isPresented: $adding) { ImportView(state: state).modifier(FormAlerts(state: state)) }
                .sheet(isPresented: $creating) { NewServerView(state: state).modifier(FormAlerts(state: state)) }
                .sheet(item: $editing) { ServerEditor(state: state, profile: $0).modifier(FormAlerts(state: state)) }
                .confirmationDialog("Remove server?", isPresented: Binding(get: { removing != nil }, set: {
                    if !$0 { removing = nil } }), titleVisibility: .visible) {
                    Button("Remove", role: .destructive) {
                        guard let profile = removing else { return }
                        do { try state.remove(profile) } catch { state.report(error) }
                        removing = nil
                    }
                }
        }
    }
}

struct ImportView: View {
    @ObservedObject var state: AppState
    var initial = ""
    @Environment(\.dismiss) private var dismiss
    @State private var text = ""
    @State private var subscription = ""
    @State private var scanning = false
    @State private var confirmInsecure = false
    private func importNow(allowInsecure: Bool = false) {
        do {
            _ = try state.importBatch(text, allowInsecure: allowInsecure)
            state.importText = ""
            dismiss()
        } catch ClientError.insecureConfig { confirmInsecure = true } catch { state.report(error) }
    }
    var body: some View {
        NavigationStack {
            AppForm {
                AppSection("Links or TOML") {
                    TextEditor(text: $text).font(.system(.body, design: .monospaced)).frame(minHeight: 180).appEditor()
                        .textInputAutocapitalization(.never).autocorrectionDisabled()
                    Button("Paste") { text = UIPasteboard.general.string ?? "" }
                    Button("Scan QR code") { scanning = true }
                    Button("Import") {
                        importNow()
                    }.disabled(text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                }
                AppSection("Subscription") {
                    SecureField("HTTPS subscription URL", text: $subscription).textContentType(.URL)
                        .textInputAutocapitalization(.never).autocorrectionDisabled()
                    Button("Add subscription") { Task { await state.subscribe(subscription) } }
                        .disabled(subscription.isEmpty || state.busy.contains("subscription"))
                }
            }.navigationTitle("Add server")
                .toolbar { Button("Done") { dismiss() } }
                .onAppear { if text.isEmpty { text = initial } }
                .sheet(isPresented: $scanning) {
                    QRScanner { result in text = result; scanning = false }
                }
                .confirmationDialog(LocalizedStringKey(
                    "This configuration may disable certificate verification. " +
                    "Import it only if you trust its source."
                ), isPresented: $confirmInsecure, titleVisibility: .visible) {
                    Button("Import") { importNow(allowInsecure: true) }
                }
        }
    }
}

struct ServerEditor: View {
    @ObservedObject var state: AppState
    let profile: Profile
    @Environment(\.dismiss) private var dismiss
    @State private var name = ""
    @State private var text = ""
    @State private var record: Secret?
    @State private var sharing = false
    var body: some View {
        NavigationStack {
            AppForm {
                TextField("Name", text: $name)
                AppSection("Configuration") {
                    TextEditor(text: $text).font(.system(.caption, design: .monospaced))
                        .frame(minHeight: 260).appEditor()
                        .textInputAutocapitalization(.never).autocorrectionDisabled()
                    Text("Configuration contains the server password.").font(AppStyle.caption)
                        .foregroundStyle(AppStyle.secondary)
                }
                Button("Share configuration") { sharing = true }
            }.navigationTitle("Edit server")
                .toolbar {
                    ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Save") {
                            do {
                                guard var record else { throw ClientError.missingSecret }
                                record.toml = try Configuration(text).serialized()
                                try Vault.write(JSONEncoder().encode(record), key: profile.id.uuidString)
                                var updated = state.preferences.profiles.first { $0.id == profile.id } ?? profile
                                updated.name = name.trimmingCharacters(in: .whitespacesAndNewlines)
                                if updated.name.isEmpty { updated.name = try Configuration(text).name }
                                state.update(updated)
                                dismiss()
                            } catch { state.report(error) }
                        }.disabled(record == nil || !state.vpn.isDisconnected)
                    }
                }
                .task {
                    #if DEBUG
                    if state.isPreview {
                        name = profile.name
                        text = "[endpoint]\nname = \"FreeTunnel demo\"\nhostname = \"vpn.example.org\"\n"
                        return
                    }
                    #endif
                    do { record = try state.secret(profile); text = record?.toml ?? ""
                        name = profile.name } catch { state.report(error) }
                }
                .sheet(isPresented: $sharing) { ShareSheet(items: [text]) }
        }
    }
}
struct ShareSheet: UIViewControllerRepresentable {
    let items: [Any]
    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: items, applicationActivities: nil)
    }
    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}
