import SwiftUI
import FreeTunnelCore

struct NewServerView: View {
    @ObservedObject var state: AppState
    @Environment(\.dismiss) private var dismiss
    @State private var name = ""
    @State private var hostname = ""
    @State private var address = ""
    @State private var username = ""
    @State private var password = ""
    @State private var proto = "http2"
    var body: some View {
        NavigationStack {
            Form {
                TextField("Name", text: $name)
                TextField("Hostname", text: $hostname).keyboardType(.URL)
                TextField("Address and port", text: $address).keyboardType(.URL)
                TextField("Username", text: $username).textContentType(.username)
                SecureField("Password", text: $password).textContentType(.password)
                Picker("Protocol", selection: $proto) { Text("HTTP/2").tag("http2"); Text("HTTP/3").tag("http3") }
            }.textInputAutocapitalization(.never).autocorrectionDisabled()
                .navigationTitle("New configuration")
                .toolbar {
                    ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Save") {
                            do {
                                let endpoint: [String: Any] = ["name": name.isEmpty ? hostname : name,
                                    "hostname": hostname, "addresses": [address], "username": username,
                                    "password": password, "upstream_protocol": proto, "skip_verification": false]
                                try state.add(toml: Configuration.serialize(["endpoint": endpoint]))
                                dismiss()
                            } catch { state.report(error) }
                        }.disabled(hostname.isEmpty || address.isEmpty || username.isEmpty || password.isEmpty)
                    }
                }
        }
    }
}
