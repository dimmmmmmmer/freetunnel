import FreeTunnelCore
import SwiftUI

// Sizes shared by Home's speed tiles and the graph over them, as on desktop (tileHeight) and
// Android (HOME_TILE_HEIGHT, HOME_GRAPH_HEIGHT).
enum HomeMetrics {
    static let tileHeight: CGFloat = 44
    static let tileMinWidth: CGFloat = 116
    static let tileSpacing: CGFloat = 12
    static let graphGap: CGFloat = 8
    static let graphHeight = tileHeight
    static let tileRadius: CGFloat = 8
}

// The last minute of speeds, as wide as both tiles: download filled, upload a line, the newest at
// the right edge. A minute fills the width; until then the lines start part of the way in. It is
// no control and nothing to read out: the tiles under it say the numbers.
struct SpeedGraph: View {
    let history: SpeedHistory
    var body: some View {
        Canvas { context, size in
            let count = history.count
            guard count >= 2 else { return }
            let pad: CGFloat = 4
            let left = pad
            let right = size.width - pad
            let top = pad
            let bottom = size.height - pad
            let step = (right - left) / CGFloat(SpeedHistory.capacity - 1)
            let firstX = right - CGFloat(count - 1) * step
            let scale = history.scale
            func trace(_ value: (Int) -> Double) -> Path {
                var path = Path()
                for index in 0..<count {
                    let level = CGFloat(min(1, value(index) / scale))
                    let point = CGPoint(
                        x: firstX + CGFloat(index) * step, y: bottom - (bottom - top) * level)
                    if index == 0 { path.move(to: point) } else { path.addLine(to: point) }
                }
                return path
            }
            let down = trace(history.down)
            let upload = trace(history.up)
            // Under what came in, a faint fill down to the baseline.
            var fill = down
            fill.addLine(to: CGPoint(x: right, y: bottom))
            fill.addLine(to: CGPoint(x: firstX, y: bottom))
            fill.closeSubpath()
            context.fill(fill, with: .color(AppStyle.success.opacity(Double(0x2E) / 255)))
            let style = StrokeStyle(lineWidth: 1.5, lineCap: .round, lineJoin: .round)
            context.stroke(down, with: .color(AppStyle.success), style: style)
            context.stroke(upload, with: .color(AppStyle.secondary), style: style)
        }
        .frame(height: HomeMetrics.graphHeight)
        .background(
            AppStyle.tile, in: RoundedRectangle(cornerRadius: HomeMetrics.tileRadius)
        )
        .allowsHitTesting(false)
        .accessibilityIdentifier("home.graph")
        .accessibilityHidden(true)
    }
}

// Where Home's gap between the config selector and the graph is, for the toast to sit in.
struct HomeGapKey: PreferenceKey {
    static var defaultValue: CGRect?
    static func reduce(value: inout CGRect?, nextValue: () -> CGRect?) {
        value = nextValue() ?? value
    }
}

struct ToastHeightKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) {
        value = max(value, nextValue())
    }
}
