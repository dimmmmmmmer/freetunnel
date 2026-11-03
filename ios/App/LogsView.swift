import SwiftUI

struct LogsView: View {
    @ObservedObject var state: AppState
    @ObservedObject var vpn: VPNController
    @State private var autoScroll = true
    @State private var exporting = false
    var body: some View {
        VStack(spacing: 8) {
            HStack {
                Button("Clear") { Task { await vpn.readLogs(clear: true) } }
                Spacer()
                Button("Copy") {
                    UIPasteboard.general.string = vpn.logs
                    state.message = "Log copied"
                }
            }.buttonStyle(.plain).font(AppStyle.label).foregroundStyle(AppStyle.accent).frame(height: 24)
            if !state.preferences.loggingEnabled {
                HStack {
                    Text("Logging is off.").foregroundStyle(.orange)
                    Spacer()
                    Button("Turn on") {
                        state.preferences.loggingEnabled = true
                        Task {
                            await vpn.configureLogging(enabled: true, verbose: state.preferences.verboseLogs)
                        }
                    }.buttonStyle(.plain)
                }.font(AppStyle.caption)
            }
            ZStack {
                RoundedRectangle(cornerRadius: 8).fill(AppStyle.surface)
                if vpn.logs.isEmpty && state.preferences.loggingEnabled {
                    Text("Logs will appear after connecting").font(AppStyle.label).foregroundStyle(
                        AppStyle.faint)
                }
                ScrollViewReader { reader in
                    ScrollView {
                        VStack(alignment: .leading, spacing: 0) {
                            Text(vpn.logs).font(.system(size: 11, design: .monospaced)).textSelection(
                                .enabled
                            )
                            .frame(maxWidth: .infinity, alignment: .leading)
                            Color.clear.frame(height: 1).id("log-bottom")
                        }.padding(12)
                    }.onChange(of: vpn.logs) { _, _ in
                        if autoScroll { reader.scrollTo("log-bottom", anchor: .bottom) }
                    }
                    .onChange(of: autoScroll) { _, enabled in
                        if enabled { reader.scrollTo("log-bottom", anchor: .bottom) }
                    }
                }
            }
            HStack(spacing: 8) {
                Button("freetunnel.log") { exporting = true }.buttonStyle(.plain).font(.system(size: 11))
                    .foregroundStyle(AppStyle.faint).accessibilityLabel("Export log")
                Spacer(minLength: 0)
                Toggle("Auto-scroll", isOn: $autoScroll).font(AppStyle.caption).foregroundStyle(
                    AppStyle.secondary
                )
                .fixedSize().frame(height: 22)
            }
        }.padding(.horizontal, 18).padding(.top, 6).padding(.bottom, 12).background(AppStyle.background)
            .fileExporter(
                isPresented: $exporting, document: ConfigDocument(text: vpn.logs), contentType: .plainText,
                defaultFilename: "freetunnel.log"
            ) { result in if case .failure(let error) = result { state.report(error) } }
            .task {
                guard !state.isPreview else { return }
                while !Task.isCancelled {
                    await vpn.readLogs()
                    do { try await Task.sleep(for: .seconds(2)) } catch { return }
                }
            }
    }
}
