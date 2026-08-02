// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <string>

namespace ag {

class TrustTunnelClient;

class AutoNetworkMonitor {
public:
    // explicit, and takes bound_if BY VALUE — same as the real core. A mock that
    // is more permissive than the thing it stands in for lets code compile here
    // and fail the real build; that already cost one red CI run.
    explicit AutoNetworkMonitor(TrustTunnelClient *, std::string) {}
    bool start() { return true; }
    void stop() {}
};

} // namespace ag
