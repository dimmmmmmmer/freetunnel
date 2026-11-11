import FreeTunnelCore
import SwiftUI

@main struct FreeTunnelApp: App {
    @StateObject private var state = AppState()
    @Environment(\.scenePhase) private var phase
    var body: some Scene {
        WindowGroup {
            RootView(state: state)
                .preferredColorScheme(
                    state.preferences.theme == "dark"
                        ? .dark : state.preferences.theme == "light" ? .light : nil
                )
                .environment(
                    \.locale,
                    state.preferences.language == "system"
                        ? .current : Locale(identifier: state.preferences.language)
                )
                .onOpenURL { url in
                    if url.scheme == "tt" {
                        state.confirmImport(url.absoluteString)
                    } else if url.isFileURL {
                        state.importFiles([url])
                    }
                }
                .task { await state.refreshDue() }
                .onChange(of: phase) { _, newValue in
                    if newValue == .active { Task { await state.refreshDue() } }
                }
        }.backgroundTask(.appRefresh("org.freetunnel.ios.refresh")) { await state.refreshDue() }
    }
}
struct RootView: View {
    @State private var tab = ProcessInfo.processInfo.environment["FT_PREVIEW_TAB"] ?? "connection"
    @ObservedObject var state: AppState
    var body: some View {
        ZStack {
            VStack(spacing: 0) {
                navigation
                Group {
                    switch tab {
                    case "servers": ServersView(state: state, vpn: state.vpn)
                    case "routing": RoutingView(state: state)
                    case "settings": SettingsView(state: state)
                    case "logs": LogsView(state: state, vpn: state.vpn)
                    default:
                        HomeView(state: state, vpn: state.vpn) {
                            state.openAddMenu = true
                            tab = "servers"
                        }
                    }
                }.frame(maxWidth: .infinity, maxHeight: .infinity)
            }.accessibilityHidden(state.dialog != nil || state.confirmation != nil)
            if let dialog = state.dialog {
                switch dialog {
                case .server(let id):
                    ServerEditor(
                        state: state, profile: state.preferences.profiles.first { $0.id == id })
                case .importText: ImportView(state: state)
                case .subscription: ImportView(state: state, subscriptionOnly: true)
                }
            }
            if let confirmation = state.confirmation {
                Color.black.opacity(0.45).ignoresSafeArea()
                VStack(alignment: .leading, spacing: 18) {
                    Text(LocalizedStringKey(confirmation.text)).fixedSize(
                        horizontal: false, vertical: true)
                    let actions =
                        confirmation.alternateTitle == nil
                        ? AnyLayout(HStackLayout(spacing: 8))
                        : AnyLayout(VStackLayout(alignment: .trailing, spacing: 8))
                    actions {
                        Button("Cancel") {
                            state.confirmation = nil
                            confirmation.cancelAction?()
                        }.buttonStyle(
                            AppButtonStyle(dialog: true))
                        if let title = confirmation.alternateTitle {
                            Button(LocalizedStringKey(title)) {
                                state.confirmation = nil
                                confirmation.alternateAction?()
                            }.buttonStyle(AppButtonStyle(dialog: true)).accessibilityIdentifier(
                                "confirm-copy")
                        }
                        Button(LocalizedStringKey(confirmation.actionTitle)) {
                            state.confirmation = nil
                            confirmation.action()
                        }.buttonStyle(AppButtonStyle(primary: true, dialog: true))
                            .accessibilityIdentifier("confirm-action")
                    }
                }.padding(18).frame(maxWidth: 340).background(
                    AppStyle.background, in: RoundedRectangle(cornerRadius: 14)
                )
                .overlay(RoundedRectangle(cornerRadius: 14).stroke(AppStyle.border)).padding(18)
            }
            if let message = state.message {
                VStack {
                    Spacer()
                    Text(LocalizedStringKey(message)).accessibilityIdentifier("notification").font(
                        AppStyle.label
                    ).padding(12)
                        .background(AppStyle.surface, in: RoundedRectangle(cornerRadius: 8))
                        .overlay(RoundedRectangle(cornerRadius: 8).stroke(AppStyle.border)).padding(
                            .horizontal, 20
                        ).padding(.bottom, 16)
                }
                .allowsHitTesting(false)
            }
        }.tint(AppStyle.accent).foregroundStyle(AppStyle.text).font(AppStyle.body)
            .background(AppStyle.background).buttonStyle(AppButtonStyle()).toggleStyle(
                AppToggleStyle()
            )
            .modifier(PreviewForms(state: state))
            .task(id: state.message) {
                guard state.message != nil else { return }
                do {
                    try await Task.sleep(for: .seconds(6))
                    state.message = nil
                } catch {}
            }
            .task {
                guard !state.isPreview else { return }
                await state.vpn.prepare()
                if state.preferences.autoConnect && state.vpn.isDisconnected {
                    await state.connect()
                }
            }
    }
    private var navigation: some View {
        HStack(spacing: 8) {
            tabButton("connection", title: "Home", icon: "TunnelLogo")
            tabButton("servers", title: "Servers", icon: "NavConnection")
            tabButton("routing", title: "Routing", icon: "NavNetwork")
            tabButton("settings", title: "Settings", icon: "NavSettings")
            tabButton("logs", title: "Logs", icon: "NavLog")
        }.frame(maxWidth: .infinity).padding(.vertical, 8).background(AppStyle.background)
    }
    private func tabButton(_ id: String, title: LocalizedStringKey, icon: String) -> some View {
        Button {
            tab = id
        } label: {
            Image(icon).resizable().scaledToFit().frame(width: 22, height: 22)
                .opacity(tab == id ? 1 : 0.8).frame(width: 46, height: 38)
                .background(
                    tab == id ? AppStyle.selected : .clear, in: RoundedRectangle(cornerRadius: 8))
        }.buttonStyle(.plain).accessibilityIdentifier("tab-" + id).accessibilityLabel(title)
            .accessibilityAddTraits(tab == id ? [.isSelected] : [])
    }
}
struct HomeView: View {
    @Environment(\.dynamicTypeSize) private var typeSize
    @ObservedObject var state: AppState
    @ObservedObject var vpn: VPNController
    var showServers: () -> Void
    @State private var picker = false
    @State private var pulse = false
    private var transitioning: Bool { !vpn.isDisconnected && vpn.status != .connected }
    var body: some View {
        GeometryReader { geometry in
            ScrollView {
                homeContent.frame(minHeight: geometry.size.height)
            }.scrollBounceBehavior(.basedOnSize)
        }
    }
    private var homeContent: some View {
        VStack(spacing: 0) {
            Spacer(minLength: 12)
            VStack(spacing: 22) {
                VStack(spacing: 6) {
                    Button {
                        guard !state.isPreview else { return }
                        if state.selected == nil {
                            showServers()
                            return
                        }
                        Task {
                            if vpn.isDisconnected {
                                await state.connect()
                            } else {
                                do { try await vpn.disconnect() } catch { state.report(error) }
                            }
                        }
                    } label: {
                        ZStack {
                            Image("TunnelLogo").resizable().scaledToFit()
                            Image("TunnelLogoGreen").resizable().scaledToFit()
                                .opacity(vpn.status == .connected ? 1 : 0)
                                .animation(.linear(duration: 0.22), value: vpn.status)
                        }.frame(width: 132, height: 132)
                            .opacity(vpn.isDisconnected ? 0.5 : transitioning ? 0.7 : 1)
                            .scaleEffect(pulse ? 1.05 : 1)
                            .animation(
                                pulse
                                    ? .easeInOut(duration: 0.75).repeatForever(autoreverses: true)
                                    : .easeOut(duration: 0.12), value: pulse)
                    }.buttonStyle(.plain).disabled(
                        !state.loaded || vpn.status == .disconnecting
                    )
                    .accessibilityLabel(vpn.isDisconnected ? Text("Connect") : Text("Disconnect"))
                    if vpn.status == .connected, let date = vpn.connectedAt {
                        Text(date, style: .timer).font(.custom("Inter-Medium", size: 15))
                            .foregroundStyle(
                                AppStyle.secondary)
                    } else if transitioning {
                        Text(vpn.status == .disconnecting ? "Disconnecting…" : "Connecting…")
                            .font(.custom("Inter-Medium", size: 15)).foregroundStyle(
                                AppStyle.secondary)
                    }
                }
                HStack(spacing: 6) {
                    Button {
                        if state.selected == nil { showServers() } else { picker.toggle() }
                    } label: {
                        HStack(spacing: 6) {
                            Text(state.selected?.name ?? String(localized: "Add a config"))
                                .font(.custom("Inter-Medium", size: 15)).lineLimit(1)
                            if state.selected != nil {
                                Image(systemName: "arrowtriangle.down.fill").font(.system(size: 7))
                            }
                        }
                    }.buttonStyle(.plain).popover(isPresented: $picker) {
                        ScrollView {
                            VStack(spacing: 0) {
                                ForEach(state.preferences.profiles) { profile in
                                    Button {
                                        state.select(profile.id)
                                        picker = false
                                    } label: {
                                        HStack {
                                            Text(profile.name)
                                            Spacer(minLength: 12)
                                            if state.selected?.id == profile.id {
                                                Image(systemName: "checkmark").font(
                                                    .system(size: 10))
                                            }
                                        }
                                        .padding(.horizontal, 12).frame(minHeight: 40)
                                    }.buttonStyle(.plain)
                                }
                            }.padding(6)
                        }.frame(
                            width: 280,
                            height: CGFloat(min(3, state.preferences.profiles.count)) * 40 + 12
                        )
                        .presentationCompactAdaptation(.popover).presentationBackground(
                            AppStyle.background)
                    }
                    if state.selected != nil {
                        Button(action: showServers) {
                            Image(systemName: "plus").font(.system(size: 14)).frame(
                                width: 22, height: 22)
                        }
                        .buttonStyle(.plain).accessibilityLabel("Add a config")
                    }
                }.padding(.horizontal, 18)
            }.animation(.easeOut(duration: 0.28), value: vpn.status)
            Spacer(minLength: 34)
            (typeSize.isAccessibilitySize
                ? AnyLayout(VStackLayout(spacing: 12))
                : AnyLayout(HStackLayout(spacing: 12))) {
                    speedTile(upload: false, value: vpn.downSpeed)
                    speedTile(upload: true, value: vpn.upSpeed)
                }.padding(.bottom, 44)
        }.frame(maxWidth: .infinity).background(AppStyle.background)
            .onChange(of: transitioning) { _, value in pulse = value }
            .onAppear { pulse = transitioning }
            .task {
                guard !state.isPreview else { return }
                while !Task.isCancelled {
                    await vpn.sampleTraffic()
                    do { try await Task.sleep(for: .seconds(1)) } catch { return }
                }
            }
    }
    private func speedTile(upload: Bool, value: Double) -> some View {
        HStack(spacing: 5) {
            Text(upload ? "↑" : "↓").foregroundStyle(upload ? AppStyle.secondary : AppStyle.success)
            Text(String(format: "%.1f", value) + String(localized: " MB/s")).font(
                .custom("Inter-Medium", size: 15))
        }.fixedSize().padding(.horizontal, 12).padding(.vertical, 8)
            .frame(minWidth: 116, minHeight: 44).background(
                AppStyle.tile, in: RoundedRectangle(cornerRadius: 8))
    }
}
