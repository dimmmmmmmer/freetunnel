// swift-tools-version: 5.9
import PackageDescription
let package = Package(
    name: "FreeTunnelCore",
    platforms: [.iOS(.v17), .macOS(.v13)],
    products: [.library(name: "FreeTunnelCore", targets: ["FreeTunnelCore"])],
    dependencies: [.package(url: "https://github.com/dduan/TOMLDeserializer", revision: "28762efcff64ad1bb482d4e9632b8a4e817edb4a")],
    targets: [
        .target(name: "FreeTunnelCore", dependencies: ["TOMLDeserializer"]),
        .testTarget(name: "FreeTunnelCoreTests", dependencies: ["FreeTunnelCore"])
    ])
