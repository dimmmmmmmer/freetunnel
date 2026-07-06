// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <string>

namespace ag {

class TrustTunnelClient;

class AutoNetworkMonitor {
public:
    AutoNetworkMonitor(TrustTunnelClient *, const std::string &) {}
    bool start() { return true; }
    void stop() {}
};

} // namespace ag
