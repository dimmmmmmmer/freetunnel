package org.freetunnel.android

import android.net.ConnectivityManager
import android.os.Build
import java.io.File
import java.net.InetAddress
import java.net.InetSocketAddress

/** The app decision precedes destination rules, matching the desktop callback. */
internal object AppConnectionRouting {
    const val DEFAULT = 0
    const val VPN = 2
    const val REJECT = 3

    fun decide(uid: Int, selected: Set<Int>): Int = when {
        selected.isEmpty() -> DEFAULT
        uid < 0 -> REJECT
        uid in selected -> VPN
        else -> DEFAULT
    }

    fun owner(manager: ConnectivityManager, protocol: Int, local: String, localPort: Int,
              remote: String, remotePort: Int): Int {
        if (protocol != 6 && protocol != 17 || localPort !in 1..65535 || remotePort !in 1..65535 ||
            local.isBlank() || remote.isBlank()) return -1
        return try {
            // These come from inet_ntop in JNI, never a hostname or user input.
            val source = InetAddress.getByName(local)
            val target = InetAddress.getByName(remote)
            if (Build.VERSION.SDK_INT >= 29) {
                manager.getConnectionOwnerUid(protocol, InetSocketAddress(source, localPort),
                    InetSocketAddress(target, remotePort))
            } else {
                // Android 8/9 expose socket UIDs in /proc/net. Newer Android
                // restricts those files and must use the active VpnService API.
                val name = if (protocol == 6) "tcp" else "udp"
                val rows = listOf(name, name + "6").flatMap { File("/proc/net/$it").readLines().drop(1) }
                legacyOwner(rows, source.address, localPort, target.address, remotePort)
            }
        } catch (_: Exception) { -1 }
    }

    private fun address(raw: String): ByteArray? {
        if (raw.length != 8 && raw.length != 32) return null
        return try {
            val bytes = raw.chunked(2).map { it.toInt(16).toByte() }
            // /proc/net prints each 32-bit word in host order on Android ABIs.
            bytes.chunked(4).flatMap { it.reversed() }.toByteArray()
        } catch (_: NumberFormatException) { null }
    }

    private fun matches(socket: ByteArray, flow: ByteArray, wildcard: Boolean): Boolean {
        if (wildcard && socket.all { it == 0.toByte() }) return true
        if (socket.contentEquals(flow)) return true
        return socket.size == 16 && flow.size == 4 && socket.take(10).all { it == 0.toByte() } &&
            socket[10] == (-1).toByte() && socket[11] == (-1).toByte() &&
            (socket.takeLast(4).toByteArray().contentEquals(flow) ||
                wildcard && socket.takeLast(4).all { it == 0.toByte() })
    }

    /** Full endpoint pair; connected sockets win over wildcard UDP listeners. */
    fun legacyOwner(rows: List<String>, local: ByteArray, localPort: Int,
                    remote: ByteArray, remotePort: Int): Int {
        var best = -1
        var owner = -1
        for (line in rows) {
            val fields = line.trim().split(Regex("\\s+"))
            if (fields.size < 10 || fields[9].toLongOrNull() == 0L) continue
            val src = fields[1].split(':'); val dst = fields[2].split(':')
            if (src.size != 2 || dst.size != 2 || src[1].toIntOrNull(16) != localPort) continue
            val sa = address(src[0]) ?: continue
            val da = address(dst[0]) ?: continue
            val dp = dst[1].toIntOrNull(16) ?: continue
            val uid = fields[7].toIntOrNull()?.takeIf { it >= 0 } ?: continue
            val unconnected = dp == 0 && da.all { it == 0.toByte() }
            if (!matches(sa, local, true) || !(unconnected || dp == remotePort && matches(da, remote, false))) continue
            val rank = (if (unconnected) 0 else 2) + (if (sa.all { it == 0.toByte() }) 0 else 1)
            if (rank > best) { best = rank; owner = uid }
            else if (rank == best && owner != uid) owner = -1 // ambiguous SO_REUSEPORT owners
        }
        return owner
    }
}
