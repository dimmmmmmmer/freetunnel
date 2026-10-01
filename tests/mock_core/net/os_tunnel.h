// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include "net/network_manager.h"
#include "vpn/vpn.h" // sockaddr

namespace ag {

// The real one binds the socket to the outbound interface and fails if it
// cannot. Without an interface it applies the core's rule for an unbindable
// socket — fail while a tunnel is up, use ordinary routing while none is — and
// that part is mirrored here, because it is the part the wrapper's own handlers
// on the other platforms are meant to agree with.
inline bool vpn_win_socket_protect(int, const sockaddr *)
{
    if (vpn_network_manager_get_outbound_interface() == 0)
        return !vpn_network_manager_get_tunnel_active();
    return true;
}

} // namespace ag
