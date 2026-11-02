import SwiftUI
import FreeTunnelCore

struct RoutingView: View {
    @ObservedObject var state: AppState
    @State private var adding = false
    private var routing: Binding<Routing> {
        Binding(get: { state.selected?.routing ?? Routing() }, set: { value in
            guard var profile = state.selected else { return }; profile.routing = value; state.update(profile)
        })
    }
    var body: some View {
        NavigationStack {
            if let profile = state.selected {
                ScrollViewReader { reader in
                ScrollView {
                    VStack(alignment: .leading, spacing: 20) {
                        Text(profile.name).font(.headline)
                        Toggle("Enable routing rules", isOn: routing.enabled)
                        Picker("Other traffic", selection: routing.defaultDirection) {
                            Text("Through VPN").tag(Direction.vpn); Text("Direct").tag(Direction.bypass)
                        }.pickerStyle(.segmented)
                        GroupBox("Manual rules") {
                            VStack(alignment: .leading, spacing: 12) {
                                Text("Through VPN").font(.subheadline)
                                TextEditor(text: routing.manualVPN).frame(minHeight: 90).font(.system(.body,
                                    design: .monospaced))
                                Text("Direct").font(.subheadline)
                                TextEditor(text: routing.manualBypass).frame(minHeight: 90).font(.system(
                                    .body, design: .monospaced))
                                Button("Recommended for Russia") {
                                    guard let url = Bundle.main.url(forResource: "recommended-russia",
                                        withExtension: "txt"),
                                          let text = try? String(contentsOf: url, encoding: .utf8) else { return }
                                    let lines = text.split(whereSeparator: { $0.isNewline }).map {
                                        $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty &&
                                        !$0.hasPrefix("#") }
                                    let existing = routing.wrappedValue.manualBypass.split(whereSeparator: {
                                        $0.isNewline }).map(String.init)
                                    routing.wrappedValue.manualBypass = Array(Set(existing + lines)).sorted()
                                        .joined(separator: "\n")
                                }
                                Text(
                                    "One domain or IP range per line. Manual rules take priority over lists.")
                                    .font(.footnote).foregroundStyle(.secondary)
                            }.textInputAutocapitalization(.never).autocorrectionDisabled().padding(.top, 6)
                        }
                        HStack {
                            Text("Geo lists").font(.title2.bold())
                            Spacer()
                            Button("Add list", systemImage: "plus") { adding = true }
                        }.id("geo-lists")
                        Picker("Refresh lists", selection: routing.refreshHours) {
                            Text("Never").tag(0)
                            Text("Every hour").tag(1)
                            Text("Every 2 hours").tag(2)
                            Text("Every 5 hours").tag(5)
                            Text("Every 24 hours").tag(24)
                        }
                        Text(LocalizedStringKey(
                            "Updates run when iOS allows the app to work. " +
                            "Open the app to refresh overdue lists."
                        )).font(.footnote).foregroundStyle(.secondary)
                        ForEach(profile.routing.lists) { list in
                            GeoCard(state: state, profile: profile, list: list)
                        }
                        Text("Changes apply on the next connection.").font(.footnote).foregroundStyle(.secondary)
                    }.padding()
                }
                .task {
                    #if DEBUG
                    if state.isPreview && ProcessInfo.processInfo.environment["FT_PREVIEW_GEO"] == "1" {
                        try? await Task.sleep(for: .milliseconds(300))
                        reader.scrollTo("geo-lists", anchor: .top)
                    }
                    #endif
                }
                }.navigationTitle("Split tunnelling").navigationBarTitleDisplayMode(.inline).sheet(
                    isPresented: $adding) { AddListView(state: state,
                    profileID: profile.id) }
            } else {
                ContentUnavailableView("Select a server first", systemImage: "server.rack")
            }
        }
    }
}
private struct GeoCard: View {
    @ObservedObject var state: AppState
    let profile: Profile
    let list: GeoList
    @State private var confirmRemove = false
    private func change(_ edit: (inout GeoList) -> Void) {
        guard var latest = state.preferences.profiles.first(where: { $0.id == profile.id }),
              let index = latest.routing.lists.firstIndex(where: { $0.id == list.id }) else { return }
        edit(&latest.routing.lists[index]); state.update(latest)
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Toggle(LocalizedStringKey(list.name), isOn: Binding(get: { list.enabled }, set: {
                value in change { $0.enabled = value } }))
                .font(.headline)
            Text(list.source).font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
                .frame(maxWidth: .infinity, alignment: .leading)
            Picker("Route", selection: Binding(get: { list.direction }, set: { value in change {
                $0.direction = value } })) {
                Text("Through VPN").tag(Direction.vpn); Text("Direct").tag(Direction.bypass)
            }.pickerStyle(.segmented)
            if let snapshot = state.snapshots[list.id] {
                Text("\(snapshot.entries.count) rules · \(snapshot.skipped) skipped").font(.footnote)
                Text(snapshot.updated, style: .relative).font(.caption).foregroundStyle(.secondary)
            } else { Text("Not downloaded yet").font(.footnote).foregroundStyle(.secondary) }
            HStack {
                Button("Refresh") { Task { await state.refresh(list) } }.disabled(state.busy.contains(list.id))
                Spacer()
                if state.busy.contains(list.id) { ProgressView() }
                Button("Remove", role: .destructive) { confirmRemove = true }
            }.buttonStyle(.bordered)
        }.padding(16).background(.secondary.opacity(0.08), in: RoundedRectangle(cornerRadius: 16))
            .overlay(RoundedRectangle(cornerRadius: 16).stroke(.secondary.opacity(0.25)))
            .confirmationDialog("Remove list?", isPresented: $confirmRemove, titleVisibility: .visible) {
                Button("Remove", role: .destructive) {
                    guard var latest = state.preferences.profiles.first(where: { $0.id == profile.id }) else { return }
                    latest.routing.lists.removeAll { $0.id == list.id }; state.update(latest)
                }
            }
    }
}
private struct Preset: Decodable, Identifiable {
    let id: String; let name: String; let source: String; let kind: ListKind; let direction: Direction
}
private struct AddListView: View {
    @ObservedObject var state: AppState
    let profileID: UUID
    @Environment(\.dismiss) private var dismiss
    @State private var name = ""
    @State private var source = ""
    @State private var kind: ListKind = .sites
    @State private var direction: Direction = .vpn
    private var presets: [Preset] {
        guard let url = Bundle.main.url(forResource: "presets", withExtension: "json"),
              let data = try? Data(contentsOf: url) else { return [] }
        return (try? JSONDecoder().decode([Preset].self, from: data)) ?? []
    }
    private func add(_ list: GeoList) {
        do {
            _ = try GeoParser.source(list.source)
            guard var profile = state.preferences.profiles.first(where: { $0.id == profileID }) else { return }
            profile.routing.lists.append(list); state.update(profile)
            Task { await state.refresh(list) }
            dismiss()
        } catch { state.report(error) }
    }
    var body: some View {
        NavigationStack {
            Form {
                Section("Ready-made lists") {
                    ForEach(presets) { preset in
                        Button {
                            add(GeoList(name: preset.name, source: preset.source, kind: preset.kind,
                                direction: preset.direction))
                        } label: { Text(LocalizedStringKey(preset.name)) }
                    }
                }
                Section("Custom list") {
                    TextField("Name", text: $name)
                    TextField("HTTPS list URL", text: $source).keyboardType(.URL).textInputAutocapitalization(
                        .never).autocorrectionDisabled()
                    Picker("Contents", selection: $kind) { Text("Domains").tag(ListKind.sites); Text(
                        "IP ranges").tag(ListKind.ips) }
                    Picker("Route", selection: $direction) { Text("Through VPN").tag(Direction.vpn); Text(
                        "Direct").tag(Direction.bypass) }
                    Button("Add") { add(GeoList(name: name.isEmpty ? source : name, source: source,
                        kind: kind, direction: direction)) }
                        .disabled(source.isEmpty)
                }
            }.navigationTitle("Add list").toolbar { Button("Cancel") { dismiss() } }
        }
    }
}
