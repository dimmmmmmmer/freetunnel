import SwiftUI

struct LicensesView: View {
    private var documents: [URL] {
        let bundle = Bundle.main
        let common = bundle.urls(forResourcesWithExtension: "txt", subdirectory: "common") ?? []
        let app = bundle.urls(forResourcesWithExtension: "txt", subdirectory: nil) ?? []
        let notices = bundle.url(forResource: "THIRD_PARTY_NOTICES", withExtension: "md")
        return (common + app + [notices].compactMap { $0 }).sorted {
            $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending
        }
    }
    var body: some View {
        List(documents, id: \.absoluteString) { url in
            NavigationLink(url.deletingPathExtension().lastPathComponent) {
                ScrollView {
                    Text((try? String(contentsOf: url, encoding: .utf8)) ?? "")
                        .font(.caption).textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading).padding()
                }.navigationTitle(url.deletingPathExtension().lastPathComponent)
            }
        }.scrollContentBackground(.hidden).background(AppStyle.background)
            .navigationBarTitleDisplayMode(.inline).navigationTitle("Open-source licenses")
    }
}
