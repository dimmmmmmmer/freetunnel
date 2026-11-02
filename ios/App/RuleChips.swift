import SwiftUI
import FreeTunnelCore

struct RuleChips: View {
    let title: LocalizedStringKey
    @Binding var rules: String
    @State private var input = ""
    @State private var invalid = false
    private var entries: [String] {
        var seen = Set<String>()
        return rules.split(whereSeparator: { $0.isNewline }).map(String.init).filter { seen.insert($0).inserted }
    }
    private func add() {
        let values = input.split(whereSeparator: { $0.isWhitespace || $0 == "," }).map(String.init)
        let parsed = values.compactMap(GeoParser.manual)
        guard !values.isEmpty, parsed.count == values.count else { invalid = true; return }
        var updated = entries
        for value in parsed where !updated.contains(value) { updated.append(value) }
        rules = updated.joined(separator: "\n")
        input = ""
        invalid = false
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title).font(AppStyle.label)
            ChipFlow {
                ForEach(entries, id: \.self) { entry in
                    HStack(spacing: 4) {
                        Text(entry).font(AppStyle.label).lineLimit(1)
                        Button {
                            rules = entries.filter { $0 != entry }.joined(separator: "\n")
                        } label: {
                            Image(systemName: "xmark").font(.system(size: 11))
                                .frame(width: 32, height: 44).contentShape(Rectangle())
                        }.buttonStyle(.plain).accessibilityLabel(Text("Remove") + Text(" " + entry))
                    }.padding(.leading, 11)
                        .background(AppStyle.surface, in: Capsule())
                }
            }
            HStack(spacing: 6) {
                TextField("Domain or IP range", text: $input)
                    .textFieldStyle(AppFieldStyle()).textInputAutocapitalization(.never).autocorrectionDisabled()
                    .submitLabel(.done).onSubmit(add)
                Button(action: add) { Image(systemName: "plus").frame(width: 44, height: 44) }
                    .buttonStyle(.plain).accessibilityLabel("Add rule").disabled(input.isEmpty)
            }
            if invalid { Text("Invalid domain or IP range").font(AppStyle.caption).foregroundStyle(AppStyle.danger) }
        }
    }
}

private struct ChipFlow: Layout {
    private func positions(_ subviews: Subviews, width: CGFloat) -> (points: [CGPoint], height: CGFloat) {
        var points: [CGPoint] = []
        var x: CGFloat = 0
        var y: CGFloat = 0
        var rowHeight: CGFloat = 0
        for view in subviews {
            let size = view.sizeThatFits(ProposedViewSize(width: width, height: nil))
            if x > 0 && x + size.width > width { x = 0; y += rowHeight + 6; rowHeight = 0 }
            points.append(CGPoint(x: x, y: y))
            x += size.width + 6
            rowHeight = max(rowHeight, size.height)
        }
        return (points, y + rowHeight)
    }
    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let width = max(1, proposal.width ?? 320)
        return CGSize(width: width, height: positions(subviews, width: width).height)
    }
    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        let layout = positions(subviews, width: bounds.width)
        for (index, view) in subviews.enumerated() {
            let point = layout.points[index]
            view.place(at: CGPoint(x: bounds.minX + point.x, y: bounds.minY + point.y), anchor: .topLeading,
                proposal: ProposedViewSize(width: bounds.width, height: nil))
        }
    }
}
