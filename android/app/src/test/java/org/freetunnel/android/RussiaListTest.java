package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

// "Recommended for Russia" is data/split/recommended-russia.txt, one file for this app and the desktop (whose
// tests/test_appsettings.cpp reads it as well). This reads the repository's own file, as it is checked in, and
// holds every line of it to what the app does with a rule.
public final class RussiaListTest {
    // Gradle runs unit tests in the module's directory, android/app.
    private static final File FILE = new File("../../data/split/recommended-russia.txt");

    private static byte[] bytes() throws Exception { return Files.readAllBytes(FILE.toPath()); }
    private static List<String> rules() throws Exception { return RussiaList.parse(new String(bytes(), StandardCharsets.UTF_8)); }

    // The reading.

    @Test public void aLineIsARuleUnlessItIsBlankOrBeginsWithAHash() {
        assertEquals(Arrays.asList("a.example", "*.b.example", "мвд.рф"),
            RussiaList.parse("# header\n\n  a.example  \r\n# a.example\n*.b.example\n   \n  # indented comment\nмвд.рф"));
    }

    @Test public void aByteOrderMarkIsNotPartOfTheFirstRule() {
        assertEquals(Arrays.asList("a.example"), RussiaList.parse("\uFEFFa.example\n"));
    }

    @Test public void nothingInItIsNoRules() {
        assertTrue(RussiaList.parse("").isEmpty());
        assertTrue(RussiaList.parse("# only\n#comments\n\n").isEmpty());
    }

    @Test public void theListIsReadOnly() {
        try {
            RussiaList.parse("a.example").add("b.example");
            org.junit.Assert.fail("the list can be changed");
        } catch (UnsupportedOperationException expected) { }
    }

    // The file.

    @Test public void theFileIsUtf8WithoutAByteOrderMarkAndOnlyHasWhatTheDesktopBuildAllows() throws Exception {
        byte[] bytes = bytes();
        assertFalse("a byte order mark", bytes.length >= 3 && (bytes[0] & 0xFF) == 0xEF && (bytes[1] & 0xFF) == 0xBB && (bytes[2] & 0xFF) == 0xBF);
        // cmake/SharedData.cmake refuses these in a rule: they would break out of a C++ string or a CMake list.
        String text = new String(bytes, StandardCharsets.UTF_8);
        StringBuilder body = new StringBuilder();
        for (String line : text.split("\n")) if (!line.trim().startsWith("#")) body.append(line).append('\n');
        for (char bad : new char[] {'"', '\\', ';', '$', '`', '[', ']'}) assertFalse("a rule has '" + bad + "'", body.indexOf(String.valueOf(bad)) >= 0);
        // A rule has no comment after it, and none has a space inside.
        for (String rule : rules()) assertFalse(rule, rule.contains(" ") || rule.contains("#") || rule.contains("\t"));
    }

    @Test public void everyRuleInTheFileIsOneTheAppTakes() throws Exception {
        List<String> rules = rules();
        assertTrue("the list is not nearly empty", rules.size() > 50);
        for (String rule : rules) {
            String core = SplitRules.INSTANCE.core(rule);
            assertNotNull("not a rule: " + rule, core);
            assertEquals("A rule is as the Split page stores it, so that adding it by hand changes nothing: " + rule,
                rule, rule.toLowerCase(java.util.Locale.ROOT));
            // Never a top-level domain alone, which the core's domain filter refuses.
            assertFalse("a top-level domain: " + rule, rule.replaceFirst("^\\*?\\.?", "").indexOf('.') < 0);
        }
    }

    @Test public void noRuleIsListedTwiceInAnySpelling() throws Exception {
        Set<String> seen = new HashSet<>();
        for (String rule : rules()) assertTrue("twice: " + rule, seen.add(SplitRules.INSTANCE.core(rule)));
    }

    @Test public void theWholeListGoesThroughTheRulesRoutingConfigAppliesWithoutAnError() throws Exception {
        List<String> rules = rules();
        // What RoutingConfig.prepare does with a profile's domains: normalize throws on one it cannot take.
        List<String> normalized = SplitRules.INSTANCE.normalize(String.join("\n", rules), false);
        assertEquals(rules.size(), normalized.size());
        for (String rule : normalized) assertNotNull(SplitRules.INSTANCE.core(rule));
    }

    @Test public void theCyrillicDomainReachesTheCoreInItsAsciiForm() throws Exception {
        assertTrue(rules().contains("мвд.рф"));
        assertEquals("xn--b1aew.xn--p1ai", SplitRules.INSTANCE.core("мвд.рф"));
        assertEquals("*.xn--b1aew.xn--p1ai", SplitRules.INSTANCE.core("*.мвд.рф"));
    }

    @Test public void theCommonProfileDefaultsHoldTheFileAsItIs() throws Exception {
        List<String> rules = rules();
        SplitProfiles fresh = SplitProfiles.seeded(rules);
        assertEquals("A new installation's Default is the whole file, in its order", rules, fresh.domains("Default"));
        SplitProfiles other = SplitProfiles.empty();
        other.add("Work");
        assertEquals(rules.size(), other.addRecommended("Work", rules));
        assertEquals(rules, other.domains("Work"));
        assertEquals("and a second time adds nothing", 0, other.addRecommended("Work", rules));
        List<String> copy = new ArrayList<>(rules);
        assertEquals(copy, rules);
    }
}
