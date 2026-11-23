package org.freetunnel.android

import java.math.BigInteger
import java.net.IDN
import java.net.InetAddress
import java.util.Locale

object SplitRules {
    fun tokens(raw: String) = raw.split(Regex("[\\s,;]+")).filter { it.isNotBlank() }
    fun subnet(raw: String): String? {
        val parts = raw.trim().split('/')
        if (parts.size > 2) return null
        val host = parts[0]
        val bits = if (host.contains(':')) {
            if (!host.matches(Regex("[0-9a-fA-F:.]+")) || runCatching { InetAddress.getByName(host) }.getOrNull() == null) return null
            128
        } else {
            val octets = host.split('.')
            if (octets.size != 4 || octets.any { !it.matches(Regex("0|[1-9][0-9]{0,2}")) || it.toInt() > 255 }) return null
            32
        }
        val prefix = if (parts.size == 2) parts[1].toIntOrNull()?.takeIf { it in 0..bits } ?: return null else bits
        return "${host.lowercase(Locale.ROOT)}/$prefix"
    }
    /** Numeric CIDRs only; preserve IPv6 mapped addresses as a 128-bit family. */
    fun coversAddressFamily(routes: List<String>): Boolean = listOf(32, 128).any { bits ->
        val intervals = routes.mapNotNull { raw ->
            val cidr = subnet(raw) ?: return@mapNotNull null
            val host = cidr.substringBefore('/')
            if ((if (':' in host) 128 else 32) != bits) return@mapNotNull null
            var bytes = InetAddress.getByName(host).address
            if (bits == 128 && bytes.size == 4) {
                bytes = ByteArray(10) + byteArrayOf(0xff.toByte(), 0xff.toByte()) + bytes
            }
            val remaining = bits - cidr.substringAfter('/').toInt()
            val first = BigInteger(1, bytes).shiftRight(remaining).shiftLeft(remaining)
            first to first.add(BigInteger.ONE.shiftLeft(remaining)).subtract(BigInteger.ONE)
        }.sortedBy { it.first }
        var next = BigInteger.ZERO
        for ((first, last) in intervals) {
            if (first > next) break
            if (last >= next) next = last.add(BigInteger.ONE)
        }
        next >= BigInteger.ONE.shiftLeft(bits)
    }
    fun core(raw: String): String? {
        val v = raw.trim().lowercase(Locale.ROOT)
        subnet(v)?.let {
            if (it.substringAfterLast('/').toInt() == 0) return null
            return if ('/' in v) it else v
        }
        if ('/' in v || ':' in v) return null
        val wild = v.startsWith("*.") || v.startsWith('.')
        val host = v.removePrefix("*.").removePrefix(".").removeSuffix(".")
        val ascii = runCatching { IDN.toASCII(host, IDN.USE_STD3_ASCII_RULES) }.getOrNull() ?: return null
        val labels = ascii.split('.')
        // TrustTunnel 1.1.12 DomainFilter::parse_entry takes a wildcard with one label
        // (*.ru, *.su; its own test accepts *.google) and matches subdomains of that label.
        // A name with no wildcard still needs two labels. The filter would take "ru", but only
        // as that name and www.ru, not as every name under .ru.
        val minLabels = if (wild) 1 else 2
        if (ascii.length > 253 || labels.size < minLabels || labels.last().all { it.isDigit() } || labels.last().length < 2 ||
            labels.any { !it.matches(Regex("[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?")) }) return null
        return if (wild) "*.$ascii" else ascii
    }
    fun normalize(raw: String, routesOnly: Boolean = false): List<String> {
        val values = tokens(raw)
        val invalid = values.filter { (if (routesOnly) subnet(it) else core(it)) == null }
        require(invalid.isEmpty()) { "Проверьте ${if (routesOnly) "IP или подсеть" else "домен или IP"}: ${invalid.take(3).joinToString()}" }
        return values.map { if (routesOnly) subnet(it)!! else it.lowercase(Locale.ROOT).removeSuffix(".") }
            .distinctBy { if (routesOnly) it else core(it) }
    }
}
