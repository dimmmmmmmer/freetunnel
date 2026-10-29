package org.freetunnel.android;

/**
 * What Home shows for the state of the VPN, as the desktop's HomePage does for its
 * backend: how bright the logo is, whether it pulses, and what the line under it says.
 * The states are FreeTunnelVpnService's: 0 off, 1 connecting, 2 connected, 3 to 5 the core
 * recovering, 6 traffic blocked by the kill switch while it reconnects. {@code stopping}
 * is the time between the user's Disconnect and the service reporting 0, which the
 * desktop calls disconnecting.
 *
 * <p>The desktop has no blocked state of its own: while its tunnel reconnects (Reconnecting, WaitingForNetwork) its backend is
 * "connecting", and Home says Connecting… with the logo pulsing, so state 6, which is the service reconnecting by itself with
 * its backoff, looks the same as states 1 and 3 to 5. Nothing else is on Home, no link to reconnect now: the logo disconnects.
 *
 * <p>No Android classes: a plain JVM can load and test it.
 */
final class HomeState {
    /** Off, busy and connected: the logo's opacity. */
    static final float OFF = 0.5f;
    static final float BUSY = 0.7f;
    static final float ON = 1.0f;

    /** What the line under the logo holds. */
    enum Line { NONE, CONNECTING, DISCONNECTING, TIMER }

    private HomeState() { }

    /** A session exists: it is up, on its way up or down, or blocked. The logo sits up, and the line shows. */
    static boolean sessionActive(int vpnState, boolean stopping) {
        return vpnState != 0 || stopping;
    }

    static float alpha(int vpnState, boolean stopping) {
        if (vpnState == 2 && !stopping) return ON;
        return sessionActive(vpnState, stopping) ? BUSY : OFF;
    }

    /** The desktop pulses the logo while it connects (reconnecting too) or disconnects. */
    static boolean pulsing(int vpnState, boolean stopping) {
        return stopping || vpnState == 1 || vpnState == 3 || vpnState == 4 || vpnState == 5 || vpnState == 6;
    }

    static Line line(int vpnState, boolean stopping) {
        if (stopping) return Line.DISCONNECTING;
        switch (vpnState) {
            case 1: case 3: case 4: case 5: case 6: return Line.CONNECTING;
            case 2: return Line.TIMER;
            default: return Line.NONE;
        }
    }
}
