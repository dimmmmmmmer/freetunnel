package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

// Settings > Excluded routes: the model (ExcludedRoutes) and the file the defaults come from,
// data/split/default-excluded-routes.txt, one file for this app and the desktop (whose
// tests/test_appsettings.cpp reads it as well).
public final class ExcludedRoutesTest {
    // Gradle runs unit tests in the module's directory, android/app.
    private static final File FILE = new File("../../data/split/default-excluded-routes.txt");

    private static byte[] bytes() throws Exception { return Files.readAllBytes(FILE.toPath()); }
    private static List<String> routes() throws Exception { return ExcludedRoutes.parse(new String(bytes(), StandardCharsets.UTF_8)); }
    private static List<String> list(String... items) { return new ArrayList<>(Arrays.asList(items)); }

    // The file.

    @Test public void theFileIsUtf8WithoutAByteOrderMarkAndOnlyHasWhatTheDesktopBuildAllows() throws Exception {
        byte[] bytes = bytes();
        assertFalse("a byte order mark", bytes.length >= 3 && (bytes[0] & 0xFF) == 0xEF && (bytes[1] & 0xFF) == 0xBB && (bytes[2] & 0xFF) == 0xBF);
        // cmake/SharedData.cmake refuses these in an entry: they would break out of a C++ string or a CMake list.
        StringBuilder body = new StringBuilder();
        for (String line : new String(bytes, StandardCharsets.UTF_8).split("\n")) if (!line.trim().startsWith("#")) body.append(line).append('\n');
        for (char bad : new char[] {'"', '\\', ';', '$', '`', '[', ']'}) assertFalse("an entry has '" + bad + "'", body.indexOf(String.valueOf(bad)) >= 0);
        // An entry has no comment after it, and none has a space inside.
        for (String route : routes()) assertFalse(route, route.contains(" ") || route.contains("#") || route.contains("\t"));
    }

    @Test public void everyRouteInTheFileIsOneThisAppKeepsExactlyAsItIsWritten() throws Exception {
        List<String> routes = routes();
        assertTrue("the list is not nearly empty", routes.size() >= 7);
        for (String route : routes) {
            assertEquals("A subnet is kept as written here, so that listing it by hand changes nothing: " + route, route, ExcludedRoutes.normalize(route));
            assertFalse("a route of every address: " + route, ExcludedRoutes.isEveryAddress(route));
        }
        assertEquals("none twice", new HashSet<>(routes).size(), routes.size());
        assertEquals("Sanitizing the file changes nothing", routes, ExcludedRoutes.sanitized(routes));
    }

    @Test public void theFileCoversThePrivateRangesAndNotLoopback() throws Exception {
        List<String> routes = routes();
        for (String range : new String[] {"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "100.64.0.0/10", "169.254.0.0/16"})
            assertTrue(range + " must stay out of the tunnel", routes.contains(range));
        // 127/8 never reaches a tunnel: the OS routes it to loopback ahead of any default route.
        assertFalse(routes.contains("127.0.0.0/8"));
    }

    @Test public void theDefaultsAreAPlanThatTheCoreIsNotRefusedFor() throws Exception {
        // What RoutingConfig.prepare validates after adding them: together with the platform's own, they do not cover a family.
        List<String> all = new ArrayList<>(routes());
        all.addAll(Arrays.asList("0.0.0.0/8", "224.0.0.0/3"));
        assertFalse(SplitRules.INSTANCE.coversAddressFamily(all));
    }

    // The reading.

    @Test public void aLineIsARouteUnlessItIsBlankOrBeginsWithAHash() {
        assertEquals(Arrays.asList("10.0.0.0/8", "fd00::/8"), ExcludedRoutes.parse("# header\n\n  10.0.0.0/8  \r\n# 1.2.3.4/32\nfd00::/8\n   \n  # indented"));
        assertEquals(Arrays.asList("10.0.0.0/8"), ExcludedRoutes.parse("\uFEFF10.0.0.0/8\n"));
        assertTrue(ExcludedRoutes.parse("# only\n\n").isEmpty());
    }

    // One subnet.

    @Test public void aSubnetIsKeptWithItsPrefixAndALowerCaseHost() {
        assertEquals("10.0.0.0/8", ExcludedRoutes.normalize("10.0.0.0/8"));
        assertEquals("1.2.3.4/32", ExcludedRoutes.normalize("1.2.3.4"));
        assertEquals("fd00::/8", ExcludedRoutes.normalize("FD00::/8"));
        assertEquals("fe80::1/128", ExcludedRoutes.normalize(" fe80::1 "));
        // A leading zero in the prefix is the same prefix.
        assertEquals("10.0.0.0/8", ExcludedRoutes.normalize("10.0.0.0/08"));
    }

    @Test public void whatIsNoSubnetIsRefused() {
        for (String bad : new String[] {"", "x", "example.com", "10.0.0", "10.0.0.256/8", "10.0.0.0/33", "10.0.0.0/-1", "10.0.0.0/8/8", "10.0.0.0/a",
                "::1/129", "1.2.3.4:443", "*.example.com", "10.0.0.0 /8"}) {
            assertNull(bad, ExcludedRoutes.normalize(bad));
            assertFalse(bad, ExcludedRoutes.isEveryAddress(bad));
        }
        assertNull(ExcludedRoutes.normalize(null));
    }

    @Test public void aRouteOfEveryAddressIsRefusedForThatInEverySpelling() {
        for (String every : new String[] {"0.0.0.0/0", "10.1.2.3/0", "::/0", "2001:db8::1/0", "1.2.3.4/00"}) {
            assertNull(every, ExcludedRoutes.normalize(every));
            assertTrue(every, ExcludedRoutes.isEveryAddress(every));
        }
        assertFalse(ExcludedRoutes.isEveryAddress("0.0.0.0/1"));
        assertFalse(ExcludedRoutes.isEveryAddress("0.0.0.0"));
    }

    // Typing.

    @Test public void aListTypedAtOnceIsAddedInItsOrderWhateverSeparatesIt() {
        ExcludedRoutes.Entered entered = ExcludedRoutes.add(list("10.0.0.0/8"), "172.16.0.0/12, 192.168.0.0/16;1.2.3.4\n  ::1 ");
        assertEquals(Arrays.asList("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "1.2.3.4/32", "::1/128"), entered.routes);
        assertEquals(Arrays.asList("172.16.0.0/12", "192.168.0.0/16", "1.2.3.4/32", "::1/128"), entered.added);
        assertTrue(entered.refused.isEmpty());
    }

    @Test public void aRouteTheListHasIsNeitherAddedNorRefused() {
        // Written another way, it is the same route.
        ExcludedRoutes.Entered entered = ExcludedRoutes.add(list("10.0.0.0/8", "1.2.3.4/32"), "10.0.0.0/8 1.2.3.4 10.0.0.0/8");
        assertEquals(Arrays.asList("10.0.0.0/8", "1.2.3.4/32"), entered.routes);
        assertTrue(entered.added.isEmpty());
        assertTrue(entered.refused.isEmpty());
        // Nor twice in one go.
        assertEquals(Arrays.asList("5.5.5.5/32"), ExcludedRoutes.add(list(), "5.5.5.5, 5.5.5.5/32").added);
    }

    @Test public void theWordsThatAreNoSubnetAreNamedAndTheRestIsStillAdded() {
        ExcludedRoutes.Entered entered = ExcludedRoutes.add(list(), "10.0.0.0/8 example.com 300.1.1.1 example.com");
        assertEquals(Arrays.asList("10.0.0.0/8"), entered.routes);
        assertEquals(Arrays.asList("example.com", "300.1.1.1"), entered.refused);
        assertFalse(entered.refusedEveryAddress());
    }

    @Test public void aRouteOfEveryAddressIsNamedAndSaysWhy() {
        ExcludedRoutes.Entered entered = ExcludedRoutes.add(list("10.0.0.0/8"), "0.0.0.0/0, 192.168.0.0/16");
        assertEquals(Arrays.asList("10.0.0.0/8", "192.168.0.0/16"), entered.routes);
        assertEquals(Arrays.asList("0.0.0.0/0"), entered.refused);
        assertTrue(entered.refusedEveryAddress());
        assertTrue(ExcludedRoutes.add(list(), "::/0").refusedEveryAddress());
        // A typo in the same input says nothing of /0.
        assertFalse(ExcludedRoutes.add(list(), "10.0.0/8").refusedEveryAddress());
    }

    @Test public void nothingTypedChangesNothing() {
        for (String typed : new String[] {null, "", "  ", ",;"}) {
            ExcludedRoutes.Entered entered = ExcludedRoutes.add(list("10.0.0.0/8"), typed);
            assertEquals(Arrays.asList("10.0.0.0/8"), entered.routes);
            assertTrue(entered.added.isEmpty());
            assertTrue(entered.refused.isEmpty());
        }
    }

    @Test public void typingNeverChangesTheListItWasGiven() {
        List<String> current = list("10.0.0.0/8");
        ExcludedRoutes.add(current, "1.1.1.1");
        assertEquals(Arrays.asList("10.0.0.0/8"), current);
    }

    @Test public void aRouteIsRemovedByItsPlaceAndAPlaceThatIsNoneRemovesNothing() {
        List<String> routes = list("a", "b", "c");
        assertEquals(Arrays.asList("a", "c"), ExcludedRoutes.removeAt(routes, 1));
        assertEquals(Arrays.asList("b", "c"), ExcludedRoutes.removeAt(routes, 0));
        assertEquals(Arrays.asList("a", "b"), ExcludedRoutes.removeAt(routes, 2));
        assertEquals(routes, ExcludedRoutes.removeAt(routes, 3));
        assertEquals(routes, ExcludedRoutes.removeAt(routes, -1));
        assertEquals("the list it was given is not changed", Arrays.asList("a", "b", "c"), routes);
    }

    // Keeping the list.

    @Test public void whereNothingIsSavedTheListIsTheDefaults() {
        List<String> defaults = list("10.0.0.0/8", "192.168.0.0/16");
        List<String> loaded = ExcludedRoutes.load(null, defaults);
        assertEquals(defaults, loaded);
        loaded.add("1.1.1.1/32");
        assertEquals("a copy: changing it leaves the defaults", 2, defaults.size());
    }

    @Test public void aClearedListIsSavedAsEmptyAndStaysEmpty() {
        assertEquals("[]", ExcludedRoutes.toJson(Collections.<String>emptyList()));
        assertTrue(ExcludedRoutes.load("[]", list("10.0.0.0/8")).isEmpty());
    }

    @Test public void aListIsSavedAndReadBackAsItWas() {
        List<String> routes = list("192.168.0.0/16", "10.0.0.0/8", "fd00::/8");
        assertEquals(routes, ExcludedRoutes.load(ExcludedRoutes.toJson(routes), list("1.1.1.1/32")));
    }

    @Test public void whatIsSavedThatCannotBeUsedIsLeftOutWhenItIsRead() {
        // A hand-edited file, or a preference from some other version: no /0, no nonsense, no duplicates, no numbers.
        String saved = "[\"10.0.0.0/8\", \"0.0.0.0/0\", \"::/0\", \"nonsense\", 5, null, \"10.0.0.0/8\", \"1.2.3.4\", {\"a\": 1}, \"FD00::/8\"]";
        assertEquals(Arrays.asList("10.0.0.0/8", "1.2.3.4/32", "fd00::/8"), ExcludedRoutes.load(saved, list("9.9.9.9/32")));
    }

    @Test public void aListThatCannotBeReadIsTheDefaults() {
        for (String broken : new String[] {"", "[", "{\"a\": 1}", "not json", "\"10.0.0.0/8\"", "10.0.0.0/8"})
            assertEquals(broken, Arrays.asList("9.9.9.9/32"), ExcludedRoutes.load(broken, list("9.9.9.9/32")));
    }

    // What the VPN is given.

    @Test public void theConfigsOwnRoutesComeFirstAsTheyAreAndTheSavedOnesFollow() {
        assertEquals(Arrays.asList("192.168.0.0/16", "10.0.0.0/8", "172.16.0.0/12"),
            ExcludedRoutes.forPlan(list("192.168.0.0/16"), list("10.0.0.0/8", "172.16.0.0/12"), false));
    }

    @Test public void aSavedRouteTheConfigHasAlreadyIsNotRepeatedHoweverItIsWritten() {
        assertEquals(Arrays.asList("10.0.0.0/8", "1.2.3.4/32"), ExcludedRoutes.forPlan(list("10.0.0.0/8", "1.2.3.4/32"), list("10.0.0.0/8", "1.2.3.4", "10.0.0.0/8"), false));
        // The config's own are left exactly as it wrote them.
        assertEquals(Arrays.asList("FD00::/8"), ExcludedRoutes.forPlan(list("FD00::/8"), list("fd00::/8"), true));
    }

    @Test public void aRouteTheSavedListHasTwiceGoesToThePlanOnce() {
        assertEquals(Arrays.asList("1.2.3.4/32", "10.0.0.0/8"), ExcludedRoutes.forPlan(list(), list("1.2.3.4", "1.2.3.4/32", "10.0.0.0/8", "10.0.0.0/8"), false));
    }

    @Test public void aSavedRouteGoesToThePlanAsTheCoreIsGivenIt() {
        // With its prefix, and as the Split page's subnets are written: a bare address is a /32.
        assertEquals(Arrays.asList("1.2.3.4/32", "fd00::/8"), ExcludedRoutes.forPlan(list(), list("1.2.3.4", "FD00::/8"), true));
    }

    @Test public void aRouteOfEveryAddressAndWhatIsNoSubnetNeverGoesToThePlan() {
        assertEquals(Arrays.asList("10.0.0.0/8"), ExcludedRoutes.forPlan(list(), list("0.0.0.0/0", "::/0", "x", "10.0.0.0/8"), true));
    }

    @Test public void ipv6RoutesGoOnlyWhereTheSessionHasIpv6() {
        assertEquals(Arrays.asList("10.0.0.0/8"), ExcludedRoutes.forPlan(list(), list("10.0.0.0/8", "fd00::/8"), false));
        assertEquals(Arrays.asList("10.0.0.0/8", "fd00::/8"), ExcludedRoutes.forPlan(list(), list("10.0.0.0/8", "fd00::/8"), true));
    }

    @Test public void thePlanNeverChangesTheListsItWasGiven() {
        List<String> own = list("192.168.0.0/16");
        List<String> saved = list("10.0.0.0/8");
        ExcludedRoutes.forPlan(own, saved, true);
        assertEquals(Arrays.asList("192.168.0.0/16"), own);
        assertEquals(Arrays.asList("10.0.0.0/8"), saved);
    }

    @Test public void noRouteIsListedTwiceInTheDefaultsInAnySpelling() throws Exception {
        Set<String> seen = new HashSet<>();
        for (String route : routes()) assertTrue("twice: " + route, seen.add(ExcludedRoutes.normalize(route)));
    }
}
