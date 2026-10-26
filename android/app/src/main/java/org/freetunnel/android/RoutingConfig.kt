package org.freetunnel.android

import org.tomlj.Toml
import androidx.core.content.edit
import org.tomlj.TomlArray
import org.tomlj.TomlTable

object RoutingConfig {
    internal val platformExcludedRoutes = listOf("0.0.0.0/8", "224.0.0.0/3")
    private fun quote(text: String): String = "\"" + buildString {
        text.forEach { c -> append(when (c) {
            '\\' -> "\\\\"; '"' -> "\\\""; '\n' -> "\\n"; '\r' -> "\\r"; '\t' -> "\\t"
            else -> if (c.code < 32 || c.code == 127) "\\u%04x".format(c.code) else c.toString()
        }) }
    } + "\""
    private fun render(item: Any): String = when (item) {
        is String -> quote(item)
        is TomlTable -> item.keySet().joinToString(", ", "{ ", " }") { "${quote(it)} = ${render(item.get(listOf(it))!!)}" }
        is TomlArray -> (0 until item.size()).joinToString(", ", "[", "]") { render(item.get(it)) }
        is List<*> -> item.joinToString(", ", "[", "]") { render(it!!) }
        is Double -> when { item.isNaN() -> "nan"; item == Double.POSITIVE_INFINITY -> "inf"; item == Double.NEGATIVE_INFINITY -> "-inf"; else -> item.toString() }
        is java.time.LocalTime -> item.format(java.time.format.DateTimeFormatter.ISO_LOCAL_TIME)
        is java.time.LocalDateTime -> item.format(java.time.format.DateTimeFormatter.ISO_LOCAL_DATE_TIME)
        is java.time.OffsetDateTime -> item.format(java.time.format.DateTimeFormatter.ISO_OFFSET_DATE_TIME)
        else -> item.toString()
    }
    private fun key(text: String) = if (text.matches(Regex("[A-Za-z0-9_-]+"))) text else quote(text)
    // Keys at a path in `drop` are left out, along with everything under them.
    private fun document(root: Map<String, Any>, drop: Set<List<String>> = emptySet()): String = buildString {
        fun tableArray(value: Any): List<TomlTable>? {
            if (value !is TomlArray || value.isEmpty) return null
            val values = (0 until value.size()).map { value.get(it) }
            return if (values.all { it is TomlTable }) values.map { it as TomlTable } else null
        }
        fun writeTable(all: Map<String, Any>, path: List<String>, arrayElement: Boolean = false) {
            val values = all.filterKeys { (path + it) !in drop }
            if (path.isNotEmpty()) {
                append(if (arrayElement) "\n[[" else "\n[")
                append(path.joinToString(".", transform = ::key))
                append(if (arrayElement) "]]\n" else "]\n")
            }
            values.filterValues { it !is TomlTable && tableArray(it) == null }.forEach { (name, value) ->
                append(key(name)).append(" = ").append(render(value)).append('\n')
            }
            values.forEach { (name, value) ->
                if (value is TomlTable) writeTable(value.keySet().associateWith { value.get(listOf(it))!! }, path + name)
                tableArray(value)?.forEach { table ->
                    writeTable(table.keySet().associateWith { table.get(listOf(it))!! }, path + name, true)
                }
            }
        }
        writeTable(root, emptyList())
    }
    private fun parse(source: String): TomlTable {
        require(source.length <= 1_048_576) { "Конфигурация слишком большая (максимум 1 МБ)" }
        val parsed = Toml.parse(source)
        require(!parsed.hasErrors()) { "Ошибка TOML: ${parsed.errors().firstOrNull()?.position()}" }
        require(parsed.getTable("endpoint") != null) { "В конфигурации отсутствует раздел [endpoint]" }
        return parsed
    }
    @JvmStatic fun validate(source: String): TomlTable {
        val parsed = parse(source)
        val endpoint = parsed.getTable("endpoint")!!
        require(!endpoint.getString("hostname").isNullOrBlank()) { "Укажите endpoint.hostname" }
        require(endpoint.getString("username") != null && endpoint.getString("password") != null) { "Укажите имя пользователя и пароль сервера" }
        val addresses = endpoint.getArray("addresses")
        require(addresses != null && addresses.size() > 0 && (0 until addresses.size()).all {
            val address = addresses.getString(it)
            val port = address.substringAfterLast(':', "").toIntOrNull()
            port != null && port in 1..65535 && address.substringBeforeLast(':').isNotBlank()
        }) { "Укажите addresses в формате адрес:порт" }
        require(endpoint.getString("upstream_protocol")?.let { it in listOf("http2", "http3") } != false) { "Укажите upstream_protocol: http2 или http3" }
        require(parsed.getString("vpn_mode")?.let { it in listOf("general", "selective") } != false) { "Неизвестный vpn_mode" }
        val rules = parsed.getArray("exclusions")?.let { a -> (0 until a.size()).map { a.getString(it) } }.orEmpty()
        require(rules.none { SplitRules.subnet(it)?.substringAfterLast('/')?.toInt() == 0 }) { "Default routes cannot be destination rules" }
        if (parsed.getString("vpn_mode") != "selective") {
            require(!SplitRules.coversAddressFamily(rules + platformExcludedRoutes)) { "Destination exclusions cover an entire address family" }
        }
        val tun = parsed.getTable("listener.tun") ?: error("Missing listener.tun")
        val included = tun.getArray("included_routes") ?: error("Missing included_routes")
        require(included.size() > 0) { "Empty included_routes" }
        val routes = (0 until included.size()).map { included.getString(it) }
        val excluded = tun.getArray("excluded_routes") ?: error("Missing excluded_routes")
        val excludedRoutes = (0 until excluded.size()).map { excluded.getString(it) }
        val allRoutes = routes + excludedRoutes
        require(allRoutes.all { SplitRules.subnet(it) != null && '/' in it }) { "Invalid TUN route" }
        require(!SplitRules.coversAddressFamily(excludedRoutes + platformExcludedRoutes)) { "Excluded TUN routes cover an entire address family" }
        val ipv6 = (endpoint.getBoolean("has_ipv6") ?: false) && routes.any { ':' in it }
        val mtu = tun.getLong("mtu_size") ?: error("Missing mtu_size")
        require(mtu in (if (ipv6) 1280L else 576L)..65535L) { "Invalid TUN MTU" }
        return parsed
    }

    /** An imported file as it will be saved, and whether a subscription was dropped from it. */
    data class Import(val toml: String, val subscriptionNotKept: Boolean)

    /** A file refused for a reason the user can act on; the message is shown as it is. */
    class ImportRefused(@JvmField val messageResource: Int) : IllegalArgumentException("Import refused")

    // Where TrustTunnel 1.3 writes a subscription address: the client's own config
    // as an [endpoint.subscription] table, the endpoint's export as subscription_url,
    // which ends up at the top or inside [endpoint]. The address carries the VPN
    // password, and this version cannot follow a subscription, so it is not kept.
    private val subscriptionKeys = setOf(listOf("subscription_url"), listOf("endpoint", "subscription"),
        listOf("endpoint", "subscription_url"))

    private fun lookup(table: TomlTable, path: List<String>): Any? {
        var current = table
        for (name in path.dropLast(1)) current = current.get(listOf(name)) as? TomlTable ?: return null
        return current.get(listOf(path.last()))
    }

    // A server that can be connected to without its subscription: the four settings
    // a link has to carry, none of them empty, as DeepLinkImport reads them.
    private fun hasStaticServer(endpoint: TomlTable): Boolean {
        fun text(name: String) = endpoint.get(listOf(name)) as? String
        val addresses = endpoint.get(listOf("addresses")) as? TomlArray
        return !text("hostname").isNullOrBlank() && !text("username").isNullOrEmpty() &&
            !text("password").isNullOrEmpty() && addresses != null && !addresses.isEmpty
    }

    // Whether `b` holds exactly what `a` does, apart from the keys at a path in `drop`.
    private fun same(a: Any?, b: Any?, path: List<String> = emptyList(), drop: Set<List<String>> = emptySet()): Boolean =
        when {
            a is TomlTable && b is TomlTable -> {
                val kept = a.keySet().filter { (path + it) !in drop }
                kept.toSet() == b.keySet() && kept.all { same(a.get(listOf(it)), b.get(listOf(it)), path + it, drop) }
            }
            a is TomlArray && b is TomlArray -> a.size() == b.size() && (0 until a.size()).all { same(a.get(it), b.get(it)) }
            else -> a == b
        }

    /**
     * Checks an imported TOML file before it is confirmed. A file is kept as it was
     * written unless it names a subscription; then only the subscription is taken
     * out of it, and everything else stays as written.
     */
    @JvmStatic fun cleanImport(source: String): Import {
        // Anything unreadable is left to validate(), which says what is wrong.
        if (source.length > 1_048_576) return Import(source, false)
        val parsed = Toml.parse(source)
        if (parsed.hasErrors()) return Import(source, false)
        val endpoint = parsed.get(listOf("endpoint")) as? TomlTable ?: return Import(source, false)
        // A real auth key needs TrustTunnel 1.3: core 1.1.7 ignores it, and the server
        // then refuses every connection without saying why. The endpoint writes an
        // empty one into every export, so only a key with something in it is refused.
        // Anything else that is there, blank or not a string, is refused too: no core
        // would read it as no key.
        val authKey = endpoint.get(listOf("client_random_auth_key"))
        if (authKey != null && authKey != "") throw ImportRefused(R.string.text_this_server_needs_a_newer_trusttunnel_core)
        if (subscriptionKeys.none { lookup(parsed, it) != null }) return Import(source, false)
        // With a subscription the server's own settings are optional, as in a link,
        // and a file without them could only work once the subscription is fetched.
        if (!hasStaticServer(endpoint)) throw ImportRefused(R.string.text_subscription_links_are_not_supported_yet)
        // Line by line first, which keeps comments, layout and certificates as they
        // were. A spelling that misses, such as an inline [endpoint] table, shows up
        // when the result is compared with the file as parsed. Such a file is written
        // out again without the subscription: every value stays, its layout does not.
        val lines: String? = TomlLines.without(source, subscriptionKeys)
        val kept = lines?.let { Toml.parse(it) }
        if (lines != null && kept != null && !kept.hasErrors() && same(parsed, kept, emptyList(), subscriptionKeys))
            return Import(lines, true)
        val root = linkedMapOf<String, Any>()
        parsed.keySet().forEach { root[it] = parsed.get(listOf(it))!! }
        return Import(document(root, subscriptionKeys), true)
    }

    data class Plan(val toml: String, val apps: List<String>, val allowApps: Boolean, val warning: Int?)

    @JvmStatic fun prepare(context: android.content.Context, source: String): Plan {
        val prefs = context.getSharedPreferences("freetunnel", android.content.Context.MODE_PRIVATE)
        val apps = prefs.getStringSet("apps", emptySet()).orEmpty().filter { it != context.packageName }.filter {
            try { context.packageManager.getApplicationInfo(it, 0); true }
            catch (_: android.content.pm.PackageManager.NameNotFoundException) { false }
        }
        val saved = prefs.getStringSet("domains", emptySet()).orEmpty()
        val valid = saved.filter { SplitRules.core(it) != null }.toSet()
        if (valid.size != saved.size) {
            prefs.edit { putStringSet("domains", valid); putBoolean("rules_migrated", true) }
        }
        return prepare(source, prefs.getBoolean("split_enabled", false), prefs.getBoolean("through", false),
            valid.toList(), apps, prefs.getBoolean("killswitch", true))
    }

    @JvmStatic fun prepare(source: String, enabled: Boolean, through: Boolean,
                           domains: List<String>, apps: List<String>, killSwitch: Boolean): Plan {
        val parsed = validate(source)
        val originalMode = parsed.getString("vpn_mode") ?: "general"
        val original = parsed.getArray("exclusions")?.let { a -> (0 until a.size()).map { a.getString(it) } }.orEmpty()
        val requestedMode = if (enabled) { if (through) "selective" else "general" } else originalMode
        var rules = ((if (!enabled || originalMode == requestedMode) original else emptyList()) +
            if (enabled) SplitRules.normalize(domains.joinToString("\n")).map { SplitRules.core(it)!! } else emptyList()).distinct()
        var selectedApps = if (enabled) apps.distinct().sorted() else emptyList()
        var allowApps = enabled && through && selectedApps.isNotEmpty()
        var mode = requestedMode
        var warning: Int? = null
        if (mode == "selective") {
            when {
                selectedApps.isNotEmpty() && rules.isNotEmpty() -> {
                    // Android's per-app allowlist intersects with destination routing.
                    // The adapter cannot express a union. Protect all traffic and tell
                    // the user explicitly instead of silently losing either ruleset.
                    mode = "general"; rules = emptyList(); selectedApps = emptyList(); allowApps = false
                    warning = R.string.text_applications_and_addresses_are_selected_full_vpn_pro
                }
                selectedApps.isNotEmpty() -> mode = "general"
                rules.isEmpty() -> {
                    mode = "general"
                    warning = R.string.text_no_through_vpn_rules_full_vpn_is_enabled
                }
            }
        }
        val root = linkedMapOf<String, Any>()
        parsed.keySet().forEach { root[it] = parsed.get(listOf(it))!! }
        root["vpn_mode"] = mode
        root["exclusions"] = rules
        root["killswitch_enabled"] = killSwitch
        // ktoml 0.7.0 cannot reliably decode escaped strings in inline tables.
        // Keep configuration tables as ordinary headers and scalar assignments.
        // A config saved by an earlier version can still hold a subscription address;
        // the core does nothing with it, so it is not handed the password in it.
        val result = document(root, subscriptionKeys)
        validate(result)
        return Plan(result, selectedApps, allowApps, warning)
    }
}
