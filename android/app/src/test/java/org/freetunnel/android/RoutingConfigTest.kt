package org.freetunnel.android

import org.junit.Assert.*
import org.junit.Test
import org.tomlj.Toml

class RoutingConfigTest {
    private val source = """
        vpn_mode = "general"
        killswitch_enabled = false
        exclusions = [
          "intranet.example",
        ]
        [endpoint] # table comment
        hostname = "vpn.example.com"
        addresses = ["192.0.2.1:443"]
        username = "test"
        password = 'secret " slash\end'
        upstream_protocol = "http2"
        has_ipv6 = false
        [listener.tun] # another comment
        included_routes = ["0.0.0.0/0", "::/0"]
        excluded_routes = ["192.168.0.0/16"]
        mtu_size = 1350
    """.trimIndent()
    private fun plan(enabled: Boolean = true, through: Boolean = true, domains: List<String> = emptyList(),
                     apps: List<String> = emptyList(), kill: Boolean = true, text: String = source) =
        RoutingConfig.prepare(text, enabled, through, domains, apps, kill)

    @Test fun appsOnlyUseGeneralAndAndroidAllowlist() {
        val result = plan(apps = listOf("test.browser"))
        assertEquals("general", Toml.parse(result.toml).getString("vpn_mode"))
        assertTrue(result.allowApps); assertEquals(listOf("test.browser"), result.apps)
        assertTrue(Toml.parse(result.toml).getArray("exclusions")!!.isEmpty)
    }
    @Test fun domainsOnlyUseSelectiveWithoutAnAndroidAllowlist() {
        val result = plan(domains = listOf("example.org"))
        assertEquals("selective", Toml.parse(result.toml).getString("vpn_mode"))
        assertFalse(result.allowApps); assertTrue(result.apps.isEmpty())
        assertEquals(listOf("example.org"), Toml.parse(result.toml).getArray("exclusions")!!.toList())
    }
    @Test fun mixedThroughRulesProtectAllTrafficWithAnExplicitWarning() {
        val result = plan(domains = listOf("example.org"), apps = listOf("test.browser"))
        val parsed = Toml.parse(result.toml)
        assertEquals("general", parsed.getString("vpn_mode"))
        assertTrue(parsed.getArray("exclusions")!!.isEmpty)
        assertTrue(result.apps.isEmpty()); assertFalse(result.allowApps); assertNotNull(result.warning)
    }
    @Test fun emptySelectiveFallsBackToFullVpnWithWarning() {
        val result = plan()
        assertEquals("general", Toml.parse(result.toml).getString("vpn_mode"))
        assertNotNull(result.warning); assertTrue(result.apps.isEmpty())
    }
    @Test fun bypassRulesWorkForAppsAndDomainsTogether() {
        val result = plan(through = false, domains = listOf("example.org"), apps = listOf("test.browser"))
        assertFalse(result.allowApps)
        assertEquals(listOf("test.browser"), result.apps)
        assertEquals(listOf("intranet.example", "example.org"), Toml.parse(result.toml).getArray("exclusions")!!.toList())
    }
    @Test fun disabledSplitPreservesImportedModeAndExclusions() {
        val original = source.replace("general", "selective")
        val result = plan(enabled = false, domains = listOf("unwanted.example"), apps = listOf("ignored.app"), text = original)
        assertEquals("selective", Toml.parse(result.toml).getString("vpn_mode"))
        assertEquals(listOf("intranet.example"), Toml.parse(result.toml).getArray("exclusions")!!.toList())
        assertTrue(result.apps.isEmpty())
    }
    @Test fun emptyImportedSelectiveAlsoFallsBackWhenUiSplitDisabled() {
        val result = plan(enabled = false, text = source.replace("general", "selective").replace("\"intranet.example\",", ""))
        assertEquals("general", Toml.parse(result.toml).getString("vpn_mode")); assertNotNull(result.warning)
    }
    @Test fun multilineTablesCommentsCredentialsAndRoutesSurvive() {
        val result = plan(enabled = false)
        val parsed = Toml.parse(result.toml)
        assertFalse(parsed.hasErrors())
        assertEquals(Toml.parse(source).getString("endpoint.password"), parsed.getString("endpoint.password"))
        assertEquals(listOf("192.168.0.0/16"), parsed.getArray("listener.tun.excluded_routes")!!.toList())
        assertEquals(1350L, parsed.getLong("listener.tun.mtu_size"))
        assertFalse(parsed.getBoolean("endpoint.has_ipv6")!!)
    }
    @Test fun killSwitchPolicyReplacesOriginalExactlyOnceAndIsIdempotent() {
        val result = plan(enabled = false)
        assertTrue(Toml.parse(result.toml).getBoolean("killswitch_enabled")!!)
        assertEquals(1, Regex("killswitch_enabled").findAll(result.toml).count())
        assertEquals(result.toml, plan(enabled = false, text = result.toml).toml)
        assertFalse(Toml.parse(plan(enabled = false, kill = false).toml).getBoolean("killswitch_enabled")!!)
    }
    @Test fun quotedRoutingKeysCannotBypassReplacement() {
        val result = plan(text = source.replace("killswitch_enabled =", "\"killswitch_enabled\" ="))
        assertTrue(Toml.parse(result.toml).getBoolean("killswitch_enabled")!!)
    }
    @Test fun multilineCertificatesAndNestedInlineTablesSurvive() {
        val text = source.replace("has_ipv6 = false", "has_ipv6 = false\ncertificate = \"\"\"\ncertificate line\n\"\"\"\nextra = { key = [1, 2] }")
        val parsed = Toml.parse(plan(enabled = false, text = text).toml)
        assertEquals("certificate line\n", parsed.getString("endpoint.certificate"))
        assertEquals(listOf(1L, 2L), parsed.getArray("endpoint.extra.key")!!.toList())
    }
    @Test fun substringAndMalformedImportsAreRejected() {
        assertThrows(IllegalArgumentException::class.java) { plan(text = "# [endpoint] [listener.tun]") }
        assertThrows(IllegalArgumentException::class.java) { plan(text = source + "\nnot valid") }
        assertThrows(IllegalArgumentException::class.java) { plan(text = source.replace("443", "99999")) }
    }
    @Test fun invalidAndroidTunSettingsAreRejectedBeforeSaving() {
        assertThrows(IllegalStateException::class.java) { plan(text = source.substringBefore("[listener.tun]")) }
        assertThrows(IllegalArgumentException::class.java) { plan(text = source.replace("1350", "0")) }
        assertThrows(IllegalArgumentException::class.java) { plan(text = source.replace("192.168.0.0/16", "192.168.0.0/99")) }
        assertThrows(IllegalArgumentException::class.java) { plan(text = source.replace("has_ipv6 = false", "has_ipv6 = true").replace("1350", "1200")) }
        assertNotNull(plan(text = source.replace("1350", "1200")))
    }
    @Test fun modeChangeDoesNotReverseImportedRules() {
        assertFalse(Toml.parse(plan(domains = listOf("example.org")).toml).getArray("exclusions")!!.toList().contains("intranet.example"))
    }
    @Test fun idnAndAllDesktopSeparatorsAreAccepted() {
        val normalized = SplitRules.normalize("мвд.рф; example.com,*.example.org\n192.0.2.0/24 2001:db8::/32")
        assertEquals(5, normalized.size)
        assertEquals("xn--b1aew.xn--p1ai", SplitRules.core(normalized.first()))
        val result = plan(domains = normalized)
        assertTrue(Toml.parse(result.toml).getArray("exclusions")!!.toList().contains("xn--b1aew.xn--p1ai"))
    }
    @Test fun invalidRulesAreReportedInsteadOfSilentlyDropped() {
        listOf("999.0.0.1", "192.0.2.0/99", "example.com/path", "*.bad..example", "2001:db8::/129").forEach {
            assertThrows(IllegalArgumentException::class.java) { SplitRules.normalize(it) }
        }
    }

    @Test fun defaultDestinationRulesAreRejectedInBothModes() {
        val variants = listOf("0.0.0.0/0", "192.0.2.1/00", "::/0", "2001:db8::1/+0", "0:0:0:0:0:0:0:0/-0")
        for (rule in variants) {
            assertNull(SplitRules.core(rule))
            assertThrows(IllegalArgumentException::class.java) { SplitRules.normalize(rule) }
            assertThrows(IllegalArgumentException::class.java) { plan(through = false, domains = listOf(rule)) }
            assertThrows(IllegalArgumentException::class.java) { plan(text = source.replace("intranet.example", rule)) }
        }
        assertNotNull(plan(enabled = false)) // included_routes defaults remain valid.
    }
    @Test fun excludedRoutesCannotRemoveAnEntireAddressFamily() {
        val exclusions = listOf(
            "\"0.0.0.0/0\"", "\"::/0\"", "\"192.0.2.1/+0\"", "\"2001:db8::1/00\"",
            "\"0.0.0.1/1\", \"128.0.0.9/1\"",
            "\"8000::1/1\", \"::1/1\""
        )
        for (routes in exclusions) assertThrows(IllegalArgumentException::class.java) {
            plan(text = source.replace("\"192.168.0.0/16\"", routes))
        }
        assertFalse(SplitRules.coversAddressFamily(listOf("0.0.0.0/2", "128.0.0.0/1", "0.0.0.0/2")))
        assertFalse(SplitRules.coversAddressFamily(listOf("::ffff:0.0.0.0/96")))
        assertFalse(SplitRules.coversAddressFamily(listOf("::/1", "::ffff:128.0.0.0/97")))
        assertNotNull(plan(enabled = false))
    }
    @Test fun effectiveBypassRulesCannotCoverAnEntireFamilyButSelectiveUnionMay() {
        val halves = listOf("0.0.0.0/1", "128.0.0.0/1")
        assertThrows(IllegalArgumentException::class.java) { plan(through = false, domains = halves) }
        assertEquals("selective", Toml.parse(plan(domains = halves).toml).getString("vpn_mode"))
    }
    @Test fun adapterRequiredTunFieldsAreRequiredBeforeSaving() {
        assertThrows(IllegalStateException::class.java) { plan(text = source.replace("excluded_routes = [\"192.168.0.0/16\"]", "")) }
        assertThrows(IllegalStateException::class.java) { plan(text = source.replace("mtu_size = 1350", "")) }
    }
    @Test fun credentialsAreWrittenAsAssignmentsUnderOrdinaryTableHeaders() {
        val password = "secret\\"
        val text = source.replace("password = 'secret \" slash\\end'", "password = '" + password + "'")
        val result = plan(enabled = false, text = text).toml
        assertTrue(result.contains("[endpoint]\n")); assertTrue(result.contains("[listener.tun]\n"))
        assertFalse(result.contains("endpoint = {"))
        assertEquals(password, Toml.parse(result).getString("endpoint.password"))
        val tables = text + "\n[[extra]]\nvalue = 'one\\'\n[[extra]]\nvalue = 'two'\n"
        val parsed = Toml.parse(plan(enabled = false, text = tables).toml)
        assertEquals("one\\", parsed.getArray("extra")!!.getTable(0).getString("value"))
        assertEquals("two", parsed.getArray("extra")!!.getTable(1).getString("value"))
    }
    private fun withEndpointKey(line: String) = source.replace("has_ipv6 = false", "has_ipv6 = false\n$line")

    @Test fun anEmptyOrMissingAuthKeyIsImportedAsWritten() {
        for (text in listOf(source, withEndpointKey("client_random_auth_key = \"\""))) {
            val imported = RoutingConfig.cleanImport(text)
            assertEquals(text, imported.toml)
            assertFalse(imported.subscriptionNotKept)
        }
        // At the top level the core never reads it, so it is just another unknown key.
        val topLevel = "client_random_auth_key = \"aabbccdd\"\n$source"
        assertEquals(topLevel, RoutingConfig.cleanImport(topLevel).toml)
    }
    @Test fun aRealAuthKeyIsRefusedWithItsOwnMessage() {
        // Only the empty string is no key. TrustTunnel's client reads the key as a
        // string, untrimmed, so a blank one or one of another type is not read as none.
        for (value in listOf("\"aabbccdd\"", "\" \"", "'00'", "5", "[\"aa\"]")) {
            val refused = assertThrows(RoutingConfig.ImportRefused::class.java) {
                RoutingConfig.cleanImport(withEndpointKey("client_random_auth_key = $value"))
            }
            assertEquals(R.string.text_this_server_needs_a_newer_trusttunnel_core, refused.messageResource)
            assertFalse(refused.message!!.contains("aabbccdd"))
        }
        // Refused before anything is dropped: the subscription changes nothing.
        assertThrows(RoutingConfig.ImportRefused::class.java) {
            RoutingConfig.cleanImport("subscription_url = \"https://a:b@vpn.example.com/s\"\n" +
                withEndpointKey("client_random_auth_key = \"aabbccdd\""))
        }
        // A dotted key at the top sets the same [endpoint] key.
        val dotted = assertThrows(RoutingConfig.ImportRefused::class.java) {
            RoutingConfig.cleanImport("endpoint.hostname = \"vpn.example.com\"\nendpoint.client_random_auth_key = \"aabbccdd\"\n")
        }
        assertEquals(R.string.text_this_server_needs_a_newer_trusttunnel_core, dotted.messageResource)
    }
    @Test fun subscriptionAddressesAreDroppedBeforeSaving() {
        val url = "https://alice:s3cret@vpn.example.com/subscription"
        val variants = listOf(
            "subscription_url = \"$url\"\n$source",
            withEndpointKey("subscription_url = '$url'"),
            source.replace("[listener.tun]", "[endpoint.subscription]\nurl = \"$url\"\n[listener.tun]"),
            withEndpointKey("subscription = { url = \"$url\" }"),
            withEndpointKey("subscription.url = \"\"\"\n$url\"\"\""),
            source.replace("[listener.tun]", "[[endpoint.subscription]]\nurl = \"$url\"\n[listener.tun]"),
        )
        for (text in variants) {
            assertFalse(Toml.parse(text).hasErrors())
            val imported = RoutingConfig.cleanImport(text)
            assertTrue(imported.subscriptionNotKept)
            // Only the subscription is taken out: the rest is the file as written.
            assertEquals(source, imported.toml)
            assertFalse(imported.toml.contains("subscription"))
            assertFalse(imported.toml.contains("s3cret"))
            val parsed = Toml.parse(imported.toml)
            assertFalse(parsed.hasErrors())
            assertEquals(Toml.parse(source).getString("endpoint.password"), parsed.getString("endpoint.password"))
            assertEquals("vpn.example.com", parsed.getString("endpoint.hostname"))
            assertEquals(1350L, parsed.getLong("listener.tun.mtu_size"))
            assertNotNull(plan(enabled = false, text = imported.toml))
        }
    }
    @Test fun droppingASubscriptionKeepsEveryOtherKeyAndTable() {
        val text = "subscription_url = \"https://a:b@vpn.example.com/s\"\nextra_root = 7\n" +
            withEndpointKey("client_random_auth_key = \"\"\nsubscription_note = \"kept\"\nnested = { keep = [1, 2] }") +
            "\n[extra]\nsubscription = \"not the endpoint's\"\n[[servers]]\nname = 'one'\n"
        val imported = RoutingConfig.cleanImport(text)
        assertTrue(imported.subscriptionNotKept)
        assertEquals(text.substringAfter('\n'), imported.toml)
        val parsed = Toml.parse(imported.toml)
        val original = Toml.parse(text)
        assertFalse(parsed.hasErrors())
        assertNull(parsed.get(listOf("subscription_url")))
        assertEquals(original.keySet() - "subscription_url", parsed.keySet())
        assertEquals(original.getTable("endpoint")!!.keySet(), parsed.getTable("endpoint")!!.keySet())
        assertEquals(7L, parsed.getLong("extra_root"))
        assertEquals("", parsed.getString("endpoint.client_random_auth_key"))
        assertEquals("kept", parsed.getString("endpoint.subscription_note"))
        assertEquals(listOf(1L, 2L), parsed.getArray("endpoint.nested.keep")!!.toList())
        assertEquals("not the endpoint's", parsed.getString("extra.subscription"))
        assertEquals("one", parsed.getArray("servers")!!.getTable(0).getString("name"))
        assertEquals(listOf("intranet.example"), parsed.getArray("exclusions")!!.toList())
    }
    // [endpoint] with a multi-line certificate between two comments.
    private fun withCertificate(pem: String, after: String = "") =
        withEndpointKey("# The server's own certificate\ncertificate = \"\"\"\n$pem\"\"\"\n# Last comment$after")
    private val key = CertificateVectors.keyPem()

    @Test fun commentsAndMultilineCertificatesStayAsWritten() {
        // A real certificate: only certificates are kept, so a made-up one that is not
        // a whole certificate would now be refused.
        val kept = withCertificate(CertificateVectors.LEAF_PEM)
        val text = kept.replace("# Last comment", "# Last comment\nsubscription_url = \"https://a:b@vpn.example.com/s\"")
        val imported = RoutingConfig.cleanImport(text)
        assertTrue(imported.subscriptionNotKept)
        assertFalse(imported.certificateIgnored)
        assertEquals(kept, imported.toml)
    }
    @Test fun aCombinedCertificateKeepsTheFileAsWritten() {
        val leaf = CertificateVectors.LEAF_PEM
        val bag = CertificateVectors.BAG_ATTRIBUTES
        for ((combined, kept) in listOf(leaf + key to leaf, key + leaf to leaf,
            leaf + key + CertificateVectors.CA_PEM to CertificateVectors.CHAIN_PEM, bag + leaf + bag + key to leaf)) {
            val imported = RoutingConfig.cleanImport(withCertificate(combined))
            assertTrue(imported.certificateIgnored)
            assertFalse(imported.subscriptionNotKept)
            // Only the certificate's lines change: comments and every other key stay.
            assertEquals(withCertificate(kept), imported.toml)
            assertFalse(imported.toml.contains(CertificateVectors.keyLabel()))
            assertEquals(kept, Toml.parse(imported.toml).getString("endpoint.certificate"))
            assertNotNull(plan(enabled = false, text = imported.toml))
        }
        // With a subscription as well, both go, and both are said.
        val both = RoutingConfig.cleanImport(withCertificate(leaf + key, "\nsubscription_url = \"https://a:b@vpn.example.com/s\""))
        assertTrue(both.certificateIgnored)
        assertTrue(both.subscriptionNotKept)
        assertEquals(withCertificate(leaf), both.toml)
    }
    @Test fun aCertificateIsCleanedInEverySpelling() {
        val leaf = CertificateVectors.LEAF_PEM
        // A dotted key at the top still names [endpoint] once rewritten.
        val dotted = "endpoint.hostname = \"vpn.example.com\"\nendpoint.certificate = \"\"\"\n$leaf$key\"\"\"\n"
        val fromDotted = RoutingConfig.cleanImport(dotted)
        assertTrue(fromDotted.certificateIgnored)
        assertEquals("endpoint.hostname = \"vpn.example.com\"\nendpoint.certificate = \"\"\"\n$leaf\"\"\"\n", fromDotted.toml)
        // A literal string.
        val literal = RoutingConfig.cleanImport(withEndpointKey("certificate = '''\n$key$leaf'''"))
        assertTrue(literal.certificateIgnored)
        assertEquals(withEndpointKey("certificate = \"\"\"\n$leaf\"\"\""), literal.toml)
        // One line with escaped CRLF line breaks, as the endpoint's export can write
        // it. The certificate is kept as it was written, line breaks included.
        val escaped = (leaf + key).replace("\n", "\\r\\n")
        val crlf = RoutingConfig.cleanImport(withEndpointKey("certificate = \"$escaped\""))
        assertTrue(crlf.certificateIgnored)
        assertFalse(crlf.toml.contains(CertificateVectors.keyLabel()))
        assertEquals(leaf.replace("\n", "\r\n").removeSuffix("\r\n") + "\n", Toml.parse(crlf.toml).getString("endpoint.certificate"))
        assertNotNull(plan(enabled = false, text = crlf.toml))
    }
    @Test fun anInlineEndpointTableIsWrittenOutWithItsCertificatesAlone() {
        val escaped = (CertificateVectors.LEAF_PEM + key).replace("\n", "\\n")
        val text = "endpoint = { hostname = \"vpn.example.com\", addresses = [\"192.0.2.1:443\"], username = \"u\", " +
            "password = \"p\", certificate = \"$escaped\" }\n[listener.tun]\nincluded_routes = [\"0.0.0.0/0\"]\n" +
            "excluded_routes = [\"192.168.0.0/16\"]\nmtu_size = 1350\n"
        assertFalse(Toml.parse(text).hasErrors())
        val imported = RoutingConfig.cleanImport(text)
        assertTrue(imported.certificateIgnored)
        assertFalse(imported.subscriptionNotKept)
        assertFalse(imported.toml.contains(CertificateVectors.keyLabel()))
        val parsed = Toml.parse(imported.toml)
        assertFalse(parsed.hasErrors())
        assertEquals(CertificateVectors.LEAF_PEM, parsed.getString("endpoint.certificate"))
        assertEquals("p", parsed.getString("endpoint.password"))
        assertEquals(1350L, parsed.getLong("listener.tun.mtu_size"))
    }
    @Test fun aCertificateWithNoCertificateIsRefused() {
        for (pem in listOf(key, CertificateVectors.pemBlock("CERTIFICATE", CertificateVectors.PKCS8_DER),
            CertificateVectors.pemBlock("CERTIFICATE", CertificateVectors.concat(CertificateVectors.LEAF_DER, CertificateVectors.PKCS8_DER)),
            "-----BEGIN CERTIFICATE-----\nMIIBszCCAVmgAwIBAgIUQ0FGRQ==\n-----END CERTIFICATE-----\n", "certificate line\n")) {
            val refused = assertThrows(RoutingConfig.ImportRefused::class.java) { RoutingConfig.cleanImport(withCertificate(pem)) }
            assertEquals(R.string.text_the_configuration_contains_an_invalid_certificate, refused.messageResource)
        }
        // An empty or blank one pins nothing, and it is kept as written, as a clean one
        // is, with nothing said, as on the desktop. So is a chain in one block, as the
        // desktop's FreeTunnel 1.1.9 and earlier wrote a link's: it holds nothing but
        // certificates, and the editor saves it again unchanged.
        for (pem in listOf("", "\n", " ", "\t\n\n", CertificateVectors.CA_PEM, CertificateVectors.CHAIN_PEM,
            CertificateVectors.pemBlock("X509 CERTIFICATE", CertificateVectors.CA_DER),
            CertificateVectors.pemBlock("CERTIFICATE", CertificateVectors.CHAIN_DER))) {
            val text = withCertificate(pem)
            val imported = RoutingConfig.cleanImport(text)
            assertEquals(text, imported.toml)
            assertFalse(imported.certificateIgnored)
            assertFalse(imported.subscriptionNotKept)
        }
        // A real auth key, then a missing server, are what is said first, as for a link.
        val keyed = assertThrows(RoutingConfig.ImportRefused::class.java) {
            RoutingConfig.cleanImport(withCertificate(key).replace("has_ipv6 = false", "has_ipv6 = false\nclient_random_auth_key = \"aa\""))
        }
        assertEquals(R.string.text_this_server_needs_a_newer_trusttunnel_core, keyed.messageResource)
        val onlySubscription = assertThrows(RoutingConfig.ImportRefused::class.java) {
            RoutingConfig.cleanImport(withCertificate(key, "\nsubscription_url = \"https://a:b@vpn.example.com/s\"")
                .replace("hostname = \"vpn.example.com\"", "hostname = \"\""))
        }
        assertEquals(R.string.text_subscription_links_are_not_supported_yet, onlySubscription.messageResource)
    }
    @Test fun aSubscriptionTheLinesMissIsStillTakenOut() {
        val url = "https://alice:s3cret@vpn.example.com/subscription"
        val tun = "\n# Gone with the rewrite\n[listener.tun]\nincluded_routes = [\"0.0.0.0/0\"]\n" +
            "excluded_routes = [\"192.168.0.0/16\"]\nmtu_size = 1350\n"
        // In an inline [endpoint] table, and behind a quoted key with an escape.
        for (text in listOf(
            "endpoint = { hostname = \"vpn.example.com\", addresses = [\"192.0.2.1:443\"], username = \"u\", " +
                "password = \"p\", subscription_url = \"$url\" }$tun",
            withEndpointKey("\"subscription\\u005furl\" = \"$url\""),
        )) {
            assertFalse(Toml.parse(text).hasErrors())
            val imported = RoutingConfig.cleanImport(text)
            assertTrue(imported.subscriptionNotKept)
            assertFalse(imported.toml.contains("subscription"))
            assertFalse(imported.toml.contains("s3cret"))
            val parsed = Toml.parse(imported.toml)
            assertFalse(parsed.hasErrors())
            assertEquals("vpn.example.com", parsed.getString("endpoint.hostname"))
            assertEquals(1350L, parsed.getLong("listener.tun.mtu_size"))
        }
    }
    @Test fun aFileWithOnlyASubscriptionIsRefusedWithItsOwnMessage() {
        val url = "https://alice:s3cret@vpn.example.com/subscription"
        val table = "[endpoint.subscription]\nurl = \"$url\"\n[listener.tun]"
        for (text in listOf(
            source.replace("[listener.tun]", table).replace("hostname = \"vpn.example.com\"", "hostname = \"\""),
            source.replace("[listener.tun]", table).replace("addresses = [\"192.0.2.1:443\"]", "addresses = []"),
            source.replace("username = \"test\"\n", "").replace("[listener.tun]", table),
            "subscription_url = \"$url\"\n" + source.replace("password = 'secret \" slash\\end'\n", "password = \"\"\n"),
        )) {
            assertFalse(Toml.parse(text).hasErrors())
            val refused = assertThrows(RoutingConfig.ImportRefused::class.java) { RoutingConfig.cleanImport(text) }
            assertEquals(R.string.text_subscription_links_are_not_supported_yet, refused.messageResource)
        }
        // Without a subscription such a file is only incomplete, and validation says so.
        val incomplete = source.replace("hostname = \"vpn.example.com\"", "hostname = \"\"")
        assertEquals(incomplete, RoutingConfig.cleanImport(incomplete).toml)
        // A real auth key is what is said first, as for a link.
        val both = assertThrows(RoutingConfig.ImportRefused::class.java) {
            RoutingConfig.cleanImport(source.replace("[listener.tun]", table)
                .replace("hostname = \"vpn.example.com\"", "hostname = \"\"\nclient_random_auth_key = \"aa\""))
        }
        assertEquals(R.string.text_this_server_needs_a_newer_trusttunnel_core, both.messageResource)
    }
    @Test fun aSubscriptionSavedByAnEarlierVersionNeverReachesTheCore() {
        val url = "https://alice:s3cret@vpn.example.com/subscription"
        for (text in listOf("subscription_url = \"$url\"\n$source", withEndpointKey("subscription_url = \"$url\""),
            source.replace("[listener.tun]", "[endpoint.subscription]\nurl = \"$url\"\n[listener.tun]"))) {
            val result = plan(enabled = false, text = text).toml
            assertFalse(result.contains("subscription"))
            assertFalse(result.contains("s3cret"))
            assertEquals("vpn.example.com", Toml.parse(result).getString("endpoint.hostname"))
        }
    }
    @Test fun unreadableFilesAreLeftForValidation() {
        for (text in listOf("not toml [", "subscription_url = \"https://a:b@x/s\"\n", "endpoint = 5\nsubscription_url = \"x\"")) {
            val imported = RoutingConfig.cleanImport(text)
            assertEquals(text, imported.toml); assertFalse(imported.subscriptionNotKept)
        }
    }
    @Test fun validationIncludesAutomaticPlatformExclusions() {
        val rules = listOf("0.0.0.0/1", "128.0.0.0/2", "192.0.0.0/3")
        assertFalse(SplitRules.coversAddressFamily(rules))
        assertTrue(SplitRules.coversAddressFamily(rules + RoutingConfig.platformExcludedRoutes))
        assertThrows(IllegalArgumentException::class.java) { plan(through = false, domains = rules) }
        assertThrows(IllegalArgumentException::class.java) {
            plan(text = source.replace("has_ipv6 = false", "has_ipv6 = true")
                .replace("\"192.168.0.0/16\"", rules.joinToString(", ") { "\"$it\"" }))
        }
        assertNotNull(plan(enabled = false))
    }
}
