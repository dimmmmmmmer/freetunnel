import SwiftUI
import FreeTunnelCore

struct RoutingView: View {
    @ObservedObject var state: AppState
    @State private var newProfile = ""
    @State private var addingProfile = false
    private var name: String { state.preferences.activeSplitProfile }
    private var routing: Binding<Routing> {
        Binding(get: { state.preferences.splitProfiles.first { $0.name == name }?.routing ?? Routing() },
            set: { state.setRouting($0, name: name) })
    }
    private var viaVPN: Bool { routing.wrappedValue.defaultDirection == .bypass }
    private var manual: Binding<String> { viaVPN ? routing.manualVPN : routing.manualBypass }
    private var opposite: Binding<String> { viaVPN ? routing.manualBypass : routing.manualVPN }
    var body: some View {
        ScrollViewReader { reader in
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    VStack(spacing: 0) {
                        Toggle("Split Tunneling", isOn: Binding(get: { routing.wrappedValue.enabled }, set: { enabled in
                            var updated = state.preferences
                            for index in updated.splitProfiles.indices { updated.splitProfiles[index].routing.enabled = enabled }
                            state.preferences = updated
                            state.reapplySelected()
                        }))
                        AppPicker(title: "Mode", selection: Binding(get: { routing.wrappedValue.defaultDirection }, set: { direction in
                            var updated = state.preferences
                            for index in updated.splitProfiles.indices {
                                var value = updated.splitProfiles[index].routing
                                if value.defaultDirection != direction {
                                    let vpn = value.manualVPN; value.manualVPN = value.manualBypass; value.manualBypass = vpn
                                    value.defaultDirection = direction
                                    updated.splitProfiles[index].routing = value
                                }
                            }
                            state.preferences = updated
                            state.reapplySelected()
                        }), options: [.init(.vpn, "Bypass VPN"), .init(.bypass, "Through VPN")], previewID: "mode")
                    }
                    if viaVPN && manual.wrappedValue.isEmpty && opposite.wrappedValue.isEmpty && !routing.wrappedValue.lists.contains(where: { $0.enabled }) {
                        Text("Add a rule to use Through VPN. The full tunnel stays on while the profile is empty.")
                            .font(AppStyle.caption).padding(10).background(AppStyle.selected, in: RoundedRectangle(cornerRadius: 8))
                    }
                    AppSection("Profile") {
                        ChipFlow {
                            ForEach(state.preferences.splitProfiles) { profile in
                                HStack(spacing: 0) {
                                    Button { state.preferences.activeSplitProfile = profile.name } label: {
                                        Text(LocalizedStringKey(profile.name)).font(AppStyle.label).padding(.horizontal, 11).frame(height: 28)
                                    }.buttonStyle(.plain)
                                    if profile.name != "Default" {
                                        Button {
                                            state.confirm(String(format: NSLocalizedString("Delete profile “%@”?", comment: ""), profile.name)) {
                                                state.removeSplitProfile(profile.name)
                                            }
                                        } label: { Image(systemName: "xmark").font(.system(size: 9)).frame(width: 24, height: 28) }
                                            .buttonStyle(.plain).accessibilityLabel("Delete profile")
                                    }
                                }.foregroundStyle(name == profile.name ? AppStyle.accentText : AppStyle.text)
                                    .background(name == profile.name ? AppStyle.accent : AppStyle.surface, in: Capsule())
                            }
                            Button { addingProfile = true } label: { Text("+").font(.system(size: 17)).frame(width: 34, height: 28) }
                                .buttonStyle(.plain).background(AppStyle.surface, in: Capsule()).accessibilityLabel("Add profile")
                        }
                        if addingProfile {
                            TextField("profile name, then Enter", text: $newProfile).textFieldStyle(AppFieldStyle()).submitLabel(.done)
                                .onSubmit { state.addSplitProfile(newProfile); newProfile = ""; addingProfile = false }
                        }
                    }
                    VStack(alignment: .leading, spacing: 6) {
                        HStack(spacing: 8) {
                            Text(viaVPN ? "Rules — via VPN" : "Rules — bypass VPN").foregroundStyle(AppStyle.faint)
                            Spacer(minLength: 0)
                            Button("Recommended for Russia") { recommended() }
                            if !manual.wrappedValue.isEmpty {
                                Button("Clear all") {
                                    state.confirm("Clear all domains?", actionTitle: "Clear") { manual.wrappedValue = "" }
                                }.foregroundStyle(AppStyle.danger)
                            }
                        }.font(AppStyle.caption).buttonStyle(.plain)
                        RuleChips(rules: manual)
                    }
                    // Preserve explicit opposite-direction rules from earlier iOS previews.
                    if !opposite.wrappedValue.isEmpty {
                        AppSection(viaVPN ? "Rules — bypass VPN" : "Rules — via VPN") { RuleChips(rules: opposite) }
                    }
                    GeoListsBlock(state: state, profileName: name).id("geo-lists")
                }.frame(maxWidth: 600).frame(maxWidth: .infinity).padding(.horizontal, 18).padding(.vertical, 10)
            }.background(AppStyle.background).buttonStyle(AppButtonStyle()).toggleStyle(AppToggleStyle())
                .task {
                    #if DEBUG
                    if state.isPreview && ProcessInfo.processInfo.environment["FT_PREVIEW_GEO"] == "1" {
                        try? await Task.sleep(for: .milliseconds(300)); reader.scrollTo("geo-lists", anchor: .top)
                    }
                    #endif
                }
        }
    }
    private func recommended() {
        guard let url = Bundle.main.url(forResource: "recommended-russia", withExtension: "txt"),
              let text = try? String(contentsOf: url, encoding: .utf8) else { return }
        var rules = manual.wrappedValue.split(whereSeparator: { $0.isNewline }).map(String.init)
        for line in text.split(whereSeparator: { $0.isNewline }) {
            let value = line.trimmingCharacters(in: .whitespaces)
            if !value.isEmpty && !value.hasPrefix("#") && !rules.contains(value) { rules.append(value) }
        }
        manual.wrappedValue = rules.joined(separator: "\n")
    }
}
struct GeoListsBlock: View {
    @ObservedObject var state: AppState
    let profileName: String
    @State private var customOpen = false
    @State private var customName = ""
    @State private var source = ""
    @State private var kind: ListKind = .sites
    @State private var direction: Direction = .vpn
    private var routing: Binding<Routing> {
        Binding(get: { state.preferences.splitProfiles.first { $0.name == profileName }?.routing ?? Routing() },
            set: { state.setRouting($0, name: profileName) })
    }
    private var presets: [Preset] {
        guard let url = Bundle.main.url(forResource: "presets", withExtension: "json"),
              let data = try? Data(contentsOf: url) else { return [] }
        return (try? JSONDecoder().decode([Preset].self, from: data)) ?? []
    }
    private func add(_ list: GeoList) {
        do {
            _ = try GeoParser.source(list.source)
            var value = routing.wrappedValue
            guard !value.lists.contains(where: { $0.source == list.source && $0.kind == list.kind }) else { return }
            value.lists.append(list); routing.wrappedValue = value
            Task { await state.refresh(list) }
            customOpen = false; customName = ""; source = ""
        } catch { state.report(error) }
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Geo lists").font(AppStyle.heading).foregroundStyle(AppStyle.faint).padding(.bottom, 4)
            Text("Ready-made groups of sites and IP addresses. Your manual rules take priority.")
                .font(AppStyle.caption).foregroundStyle(AppStyle.secondary)
            AppPicker(title: "Add list", selection: Binding(get: { "" }, set: { id in
                if id == "custom" { customOpen = true }
                else if let preset = presets.first(where: { $0.id == id }) {
                    add(GeoList(name: preset.name, source: preset.source, kind: preset.kind, direction: preset.direction))
                }
            }), options: [.init("", "Choose…")] + presets.map { .init($0.id, $0.name) } + [.init("custom", "Add your own list")], previewID: "add-list")
            ForEach(routing.wrappedValue.lists) { list in
                GeoCard(state: state, profileName: profileName, list: list)
            }
            if customOpen {
                VStack(alignment: .leading, spacing: 8) {
                    Text("Add your own list").font(AppStyle.heading).foregroundStyle(AppStyle.faint)
                    Button("Cancel") { customOpen = false }
                    LabeledField(title: "Name", text: $customName)
                    LabeledField(title: "HTTPS address", text: $source, placeholder: "https://…/geosite.dat#category")
                    AppPicker(title: "List type", selection: $kind, options: [.init(.sites, "Sites"), .init(.ips, "IP addresses")])
                    AppPicker(title: "Route", selection: $direction, options: [.init(.vpn, "Through VPN"), .init(.bypass, "Bypass VPN")], previewID: "direction")
                    Button("Add") { add(GeoList(name: customName.isEmpty ? source : customName, source: source, kind: kind, direction: direction)) }
                }.textInputAutocapitalization(.never).autocorrectionDisabled().id("custom-list")
            }
            AppPicker(title: "Update lists", selection: Binding(get: { routing.wrappedValue.refreshHours }, set: { hours in
                var updated = state.preferences
                for index in updated.splitProfiles.indices { updated.splitProfiles[index].routing.refreshHours = hours }
                state.preferences = updated
            }), options: [.init(1, "Every hour"),
                .init(2, "Every 2 hours"), .init(5, "Every 5 hours"), .init(24, "Every 24 hours"), .init(0, "Never")], previewID: "interval")
                .accessibilityHint("Updates run when iOS allows the app to work. Open the app to refresh overdue lists.")
        }.onAppear {
            #if DEBUG
            customOpen = state.isPreview && ProcessInfo.processInfo.environment["FT_PREVIEW_FORM"] == "add-list"
            #endif
        }
    }
}
private struct GeoCard: View {
    @ObservedObject var state: AppState
    let profileName: String
    let list: GeoList
    private func change(_ edit: (inout GeoList) -> Void) {
        guard var value = state.preferences.splitProfiles.first(where: { $0.name == profileName })?.routing,
              let index = value.lists.firstIndex(where: { $0.id == list.id }) else { return }
        edit(&value.lists[index]); state.setRouting(value, name: profileName)
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Toggle(LocalizedStringKey(list.name), isOn: Binding(get: { list.enabled }, set: { value in change { $0.enabled = value } }))
            AppPicker(title: "Route", selection: Binding(get: { list.direction }, set: { value in change { $0.direction = value } }),
                options: [.init(.vpn, "Through VPN"), .init(.bypass, "Bypass VPN")], previewID: "direction")
            Group {
                if state.busy.contains(list.id) { Text("Updating…") }
                else if let snapshot = state.snapshots[list.id] {
                    Text(String(format: NSLocalizedString("%lld entries · %@", comment: ""), snapshot.entries.count,
                        snapshot.updated.formatted(.dateTime.day(.twoDigits).month(.twoDigits).year().hour().minute())))
                    if snapshot.skipped > 0 { Text("Unsupported entries skipped: \(snapshot.skipped)") }
                } else { Text("Not downloaded yet") }
                if let error = state.geoErrors[list.id] { Text(error) }
            }.font(AppStyle.caption).foregroundStyle(AppStyle.secondary)
            HStack {
                Button("Update") { Task { await state.refresh(list) } }.disabled(state.busy.contains(list.id))
                Spacer()
                Button("Remove") {
                    guard var value = state.preferences.splitProfiles.first(where: { $0.name == profileName })?.routing else { return }
                    value.lists.removeAll { $0.id == list.id }; state.setRouting(value, name: profileName)
                }
            }.buttonStyle(AppButtonStyle())
        }.appCard()
    }
}
private struct Preset: Decodable, Identifiable {
    let id: String; let name: String; let source: String; let kind: ListKind; let direction: Direction
}
