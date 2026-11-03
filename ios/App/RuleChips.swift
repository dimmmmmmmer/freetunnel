import FreeTunnelCore
import SwiftUI

struct RuleChips: View {
    @Binding var rules: String
    var placeholder: LocalizedStringKey = "domains, comma-separated, then Enter"
    var ipOnly = false
    @State private var input = ""
    @State private var invalid = false
    private var entries: [String] {
        var seen = Set<String>()
        return rules.split(whereSeparator: { $0.isNewline }).map(String.init).filter {
            seen.insert($0).inserted
        }
    }
    private func add() {
        let values = input.split(whereSeparator: { $0.isWhitespace || $0 == "," }).map(String.init)
        let parsed = values.compactMap(GeoParser.manual)
        guard !values.isEmpty, parsed.count == values.count,
            !ipOnly || parsed.allSatisfy({ IPRoutes.isValid($0) })
        else {
            invalid = true
            return
        }
        var updated = entries
        for value in parsed where !updated.contains(value) { updated.append(value) }
        rules = updated.joined(separator: "\n")
        input = ""
        invalid = false
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            ChipFlow {
                ForEach(entries, id: \.self) { entry in
                    RuleChip(text: entry) { rules = entries.filter { $0 != entry }.joined(separator: "\n") }
                }
            }
            TextField(placeholder, text: $input).textFieldStyle(AppFieldStyle())
                .textInputAutocapitalization(.never).autocorrectionDisabled().submitLabel(.done).onSubmit(add)
            if invalid {
                Text("Invalid domain or IP range").font(AppStyle.caption).foregroundStyle(AppStyle.danger)
            }
        }
    }
}
struct RuleChip: View {
    let text: String
    var selected = false
    var remove: (() -> Void)?
    var body: some View {
        HStack(spacing: 5) {
            Text(LocalizedStringKey(text)).font(AppStyle.label).lineLimit(1).truncationMode(.middle)
            if let remove {
                Button(action: remove) {
                    Image(systemName: "xmark").font(.system(size: 9)).frame(width: 18, height: 28)
                }
                .buttonStyle(.plain).accessibilityLabel(Text("Remove") + Text(" " + text))
            }
        }.padding(.leading, 11).padding(.trailing, remove == nil ? 11 : 5).frame(height: 28)
            .foregroundStyle(selected ? AppStyle.accentText : AppStyle.text)
            .background(selected ? AppStyle.accent : AppStyle.surface, in: Capsule())
    }
}
struct ChipFlow: Layout {
    private func positions(_ subviews: Subviews, width: CGFloat) -> (points: [CGPoint], height: CGFloat) {
        var points: [CGPoint] = []
        var horizontal: CGFloat = 0
        var vertical: CGFloat = 0
        var rowHeight: CGFloat = 0
        for view in subviews {
            let size = view.sizeThatFits(ProposedViewSize(width: width, height: nil))
            if horizontal > 0 && horizontal + size.width > width {
                horizontal = 0
                vertical += rowHeight + 6
                rowHeight = 0
            }
            points.append(CGPoint(x: horizontal, y: vertical))
            horizontal += size.width + 6
            rowHeight = max(rowHeight, size.height)
        }
        return (points, vertical + rowHeight)
    }
    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let width = max(1, proposal.width ?? 320)
        return CGSize(width: width, height: positions(subviews, width: width).height)
    }
    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        let layout = positions(subviews, width: bounds.width)
        for (index, view) in subviews.enumerated() {
            let point = layout.points[index]
            view.place(
                at: CGPoint(
                    x: bounds.minX + point.x, y: bounds.minY + point.y),
                anchor: .topLeading,
                proposal: ProposedViewSize(width: bounds.width, height: nil))
        }
    }
}
