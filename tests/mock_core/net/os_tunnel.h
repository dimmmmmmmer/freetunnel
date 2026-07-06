// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include "vpn/vpn.h" // sockaddr

namespace ag {

inline bool vpn_win_socket_protect(int, const sockaddr *)
{
    return true;
}

} // namespace ag
