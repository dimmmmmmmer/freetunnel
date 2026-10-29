package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Test;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

// The profiles of the Split page and the rules about them, which are the desktop's (src/app/BackendSplit.cpp).
public final class SplitProfilesTest {
    private static List<String> list(String... items) { return Arrays.asList(items); }

    // Starting points.

    @Test public void aFreshOneHasOnlyDefaultAndEditsIt() {
        SplitProfiles p = SplitProfiles.empty();
        assertEquals(list("Default"), p.names());
        assertEquals("Default", p.edited());
        assertEquals("Default", p.mirrored());
        assertTrue(p.domains("Default").isEmpty());
        assertTrue(p.apps("Default").isEmpty());
    }

    @Test public void aNewInstallationsDefaultHoldsTheRecommendedListInItsOrder() {
        SplitProfiles p = SplitProfiles.seeded(list("yandex.ru", "*.yandex.ru", "vk.com", "yandex.ru"));
        assertEquals("a rule twice is one", list("yandex.ru", "*.yandex.ru", "vk.com"), p.domains("Default"));
        assertEquals("Default", p.edited());
    }

    @Test public void theOldListsBecomeDefaultSortedAndEdited() {
        SplitProfiles p = SplitProfiles.migrated(
            new java.util.HashSet<>(list("b.example", "a.example", "10.0.0.0/8", "")), new java.util.HashSet<>(list("org.z", "org.a")));
        assertEquals(list("10.0.0.0/8", "a.example", "b.example"), p.domains("Default"));
        assertEquals(list("org.a", "org.z"), p.apps("Default"));
        assertEquals(list("Default"), p.names());
        assertEquals("Default", p.edited());
    }

    @Test public void theOldListsKeepWhatTheCoreCannotUseForNowToBeDroppedWithANotice() {
        // Dropped when a session is built (SplitStore.rules), not here: this is a copy.
        SplitProfiles p = SplitProfiles.migrated(new java.util.HashSet<>(list("intranet", "example.com")), Collections.<String>emptySet());
        assertEquals(list("example.com", "intranet"), p.domains("Default"));
    }

    // Names.

    @Test public void aProfileIsMadeLastAndEditedAtOnce() {
        SplitProfiles p = SplitProfiles.empty();
        SplitProfiles.NameResult work = p.add("Work");
        assertEquals(SplitProfiles.NameStatus.ADDED, work.status);
        assertEquals(SplitProfiles.NameStatus.ADDED, p.add("Home").status);
        assertEquals(list("Default", "Work", "Home"), p.names());
        assertEquals("Home", p.edited());
        assertTrue(p.has("Work"));
        assertTrue("and it starts empty", p.domains("Work").isEmpty() && p.apps("Work").isEmpty());
    }

    @Test public void aNameIsTrimmedAndNothingOrSpacesIsNothing() {
        SplitProfiles p = SplitProfiles.empty();
        assertEquals(SplitProfiles.NameStatus.ADDED, p.add("  Work  ").status);
        assertEquals(list("Default", "Work"), p.names());
        assertEquals(SplitProfiles.NameStatus.EMPTY, p.add("").status);
        assertEquals(SplitProfiles.NameStatus.EMPTY, p.add("   ").status);
        assertEquals(SplitProfiles.NameStatus.EMPTY, p.add(null).status);
        assertEquals(list("Default", "Work"), p.names());
    }

    @Test public void aNameThatClashesInAnyLetterCaseIsRefusedAndNamesWhatItClashesWith() {
        SplitProfiles p = SplitProfiles.empty();
        p.add("Work");
        p.select("Default");
        for (String clash : list("work", "WORK", "wOrK")) {
            SplitProfiles.NameResult result = p.add(clash);
            assertEquals(clash, SplitProfiles.NameStatus.CASE_CLASH, result.status);
            assertEquals("It says which is there, as that is spelled", "Work", result.existing);
        }
        SplitProfiles.NameResult builtIn = p.add("default");
        assertEquals("Default is a name too", SplitProfiles.NameStatus.CASE_CLASH, builtIn.status);
        assertEquals("Default", builtIn.existing);
        assertEquals(SplitProfiles.NameStatus.CASE_CLASH, p.add("DEFAULT").status);
        assertEquals("Nothing was made, and nothing selected", list("Default", "Work"), p.names());
        assertEquals("Default", p.edited());
    }

    @Test public void anyAlphabetsCaseCounts() {
        SplitProfiles p = SplitProfiles.empty();
        p.add("Рабочий");
        assertEquals(SplitProfiles.NameStatus.CASE_CLASH, p.add("рабочий").status);
        assertEquals(SplitProfiles.NameStatus.CASE_CLASH, p.add("РАБОЧИЙ").status);
    }

    @Test public void theSameNameAgainIsNothingAsOnTheDesktop() {
        SplitProfiles p = SplitProfiles.empty();
        p.add("Work");
        p.select("Default");
        assertEquals(SplitProfiles.NameStatus.SAME, p.add("Work").status);
        assertEquals(list("Default", "Work"), p.names());
        assertEquals("and it does not select it", "Default", p.edited());
    }

    @Test public void aNameIsAtMostSixteenCharacters() {
        SplitProfiles p = SplitProfiles.empty();
        assertEquals(SplitProfiles.NameStatus.ADDED, p.add("1234567890123456").status);
        assertEquals(SplitProfiles.NameStatus.TOO_LONG, p.add("12345678901234567").status);
        assertEquals(SplitProfiles.NameStatus.TOO_LONG, p.add("   12345678901234567   ").status);
        assertEquals("spaces around it are not counted", SplitProfiles.NameStatus.ADDED, p.add("  abcdefghijklmnop  ").status);
        assertEquals(3, p.names().size());
    }

    @Test public void selectingOnlyChangesWhichOneIsEdited() {
        SplitProfiles p = SplitProfiles.empty();
        p.add("Work");
        p.addDomains("Work", "work.example");
        p.addDomains("Default", "home.example");
        p.setMirrored("Work");
        assertTrue(p.select("Default"));
        assertEquals("Default", p.edited());
        assertEquals("What the old lists copy is not what is edited", "Work", p.mirrored());
        assertEquals(list("work.example"), p.domains("Work"));
        assertEquals(list("home.example"), p.domains("Default"));
        assertFalse("already the one", p.select("Default"));
        assertFalse("not a profile", p.select("Nope"));
        assertFalse(p.select(null));
        assertEquals("Default", p.edited());
    }

    @Test public void deletingTakesTheListsAndSendsTheEditingBackToDefault() {
        SplitProfiles p = SplitProfiles.empty();
        p.add("Work");
        p.addDomains("Work", "work.example");
        p.addApp("Work", "org.app");
        p.setMirrored("Work");
        assertEquals("the one being edited", "Work", p.edited());
        assertTrue(p.remove("Work"));
        assertEquals(list("Default"), p.names());
        assertEquals("Default", p.edited());
        assertEquals("and the old lists copy Default again", "Default", p.mirrored());
        assertTrue(p.domains("Work").isEmpty() && p.apps("Work").isEmpty());
        assertFalse("it is not there to be deleted twice", p.remove("Work"));
    }

    @Test public void deletingAnotherLeavesTheEditingAlone() {
        SplitProfiles p = SplitProfiles.empty();
        p.add("A");
        p.add("B");
        p.select("A");
        assertTrue(p.remove("B"));
        assertEquals("A", p.edited());
        assertEquals(list("Default", "A"), p.names());
    }

    @Test public void defaultCannotBeDeletedNorCanAnUnknownName() {
        SplitProfiles p = SplitProfiles.empty();
        p.addDomains("Default", "home.example");
        assertFalse(p.remove("Default"));
        assertFalse(p.remove("Nope"));
        assertFalse(p.remove(null));
        assertEquals(list("home.example"), p.domains("Default"));
    }

    @Test public void aConfigOfADeletedProfileUsesDefault() {
        assertEquals("Default", SplitProfiles.afterRemoval("Work", "Work"));
        assertEquals("Home", SplitProfiles.afterRemoval("Home", "Work"));
        assertEquals("Default", SplitProfiles.afterRemoval("Default", "Work"));
        assertEquals("a config with no profile of its own", "Default", SplitProfiles.afterRemoval(null, "Work"));
    }

    @Test public void aNameThatIsGoneIsDefaultForWhoeverUsesIt() {
        SplitProfiles p = SplitProfiles.empty();
        p.add("Work");
        assertEquals("Work", p.effective("Work"));
        assertEquals("Default", p.effective("Gone"));
        assertEquals("Default", p.effective(null));
        p.setMirrored("Gone");
        assertEquals("Default", p.mirrored());
    }

    // Domains.

    @Test public void domainsAreAddedInTheOrderTheyWereTyped() {
        SplitProfiles p = SplitProfiles.empty();
        SplitProfiles.Entered in = p.addDomains("Default", "zeta.example, alpha.example;beta.example\n10.0.0.0/8  *.gamma.example");
        assertEquals(list("zeta.example", "alpha.example", "beta.example", "10.0.0.0/8", "*.gamma.example"), in.added);
        assertTrue(in.refused.isEmpty());
        assertEquals(in.added, p.domains("Default"));
        p.addDomains("Default", "omega.example");
        assertEquals("omega.example", p.domains("Default").get(5));
    }

    @Test public void aRuleIsStoredLowercaseWithoutTheDotAtItsEnd() {
        SplitProfiles p = SplitProfiles.empty();
        p.addDomains("Default", "Example.COM. *.Sub.Example.org");
        assertEquals(list("example.com", "*.sub.example.org"), p.domains("Default"));
    }

    @Test public void aRuleAlreadyThereIsNotAddedAgainInAnySpelling() {
        SplitProfiles p = SplitProfiles.empty();
        p.addDomains("Default", "example.com *.example.org");
        SplitProfiles.Entered again = p.addDomains("Default", "EXAMPLE.com example.com. .example.org example.net example.net");
        assertEquals("only what is new, once", list("example.net"), again.added);
        assertTrue("and what was there is not a refusal", again.refused.isEmpty());
        assertEquals(list("example.com", "*.example.org", "example.net"), p.domains("Default"));
    }

    @Test public void wordsThatAreNoRuleAreNamedAndTheRestIsAdded() {
        SplitProfiles p = SplitProfiles.empty();
        SplitProfiles.Entered in = p.addDomains("Default", "good.example, intranet, example.com:443 also.good, 1.2.3.4.5");
        assertEquals(list("good.example", "also.good"), in.added);
        assertEquals("as they were typed", list("intranet", "example.com:443", "1.2.3.4.5"), in.refused);
        assertEquals(in.added, p.domains("Default"));
    }

    @Test public void nothingTypedIsNothing() {
        SplitProfiles p = SplitProfiles.empty();
        for (String empty : list("", "  ", ",;", "\n")) {
            SplitProfiles.Entered in = p.addDomains("Default", empty);
            assertTrue(in.added.isEmpty() && in.refused.isEmpty());
        }
        assertTrue(p.domains("Default").isEmpty());
    }

    @Test public void domainsGoToTheProfileNamedAndNoOther() {
        SplitProfiles p = SplitProfiles.empty();
        p.add("Work");
        p.addDomains("Work", "work.example");
        assertTrue(p.domains("Default").isEmpty());
        assertEquals(list("work.example"), p.domains("Work"));
        SplitProfiles.Entered nowhere = p.addDomains("Nope", "x.example");
        assertTrue("a profile that is not there takes nothing", nowhere.added.isEmpty());
        assertTrue(p.domains("Nope").isEmpty());
    }

    @Test public void theRecommendedRulesAreAppendedWithoutWhatIsThere() {
        SplitProfiles p = SplitProfiles.empty();
        p.addDomains("Default", "mine.example *.yandex.ru");
        int added = p.addRecommended("Default", list("yandex.ru", "*.yandex.ru", "vk.com", ".vk.com", "vk.com"));
        assertEquals("yandex.ru, vk.com and .vk.com are new; *.yandex.ru was there", 3, added);
        assertEquals(list("mine.example", "*.yandex.ru", "yandex.ru", "vk.com", ".vk.com"), p.domains("Default"));
        assertEquals("a second time there is nothing new", 0, p.addRecommended("Default", list("yandex.ru", "vk.com")));
    }

    @Test public void theRecommendedRulesGoToTheProfileNamedAndAreAsTheFileHasThem() {
        SplitProfiles p = SplitProfiles.empty();
        p.add("Work");
        assertEquals(2, p.addRecommended("Work", list("  yandex.ru  ", "*.yandex.ru")));
        assertEquals("trimmed, otherwise as written", list("yandex.ru", "*.yandex.ru"), p.domains("Work"));
        assertTrue(p.domains("Default").isEmpty());
        assertEquals(0, p.addRecommended("Nope", list("yandex.ru")));
        assertEquals("a rule the core cannot take is not added", 0, p.addRecommended("Work", list("intranet", "*.ru")));
    }

    @Test public void aRuleIsRemovedByItsPlaceAndAllAreCleared() {
        SplitProfiles p = SplitProfiles.empty();
        p.addDomains("Default", "a.example b.example c.example");
        assertTrue(p.removeDomain("Default", 1));
        assertEquals(list("a.example", "c.example"), p.domains("Default"));
        assertFalse(p.removeDomain("Default", 2));
        assertFalse(p.removeDomain("Default", -1));
        assertFalse(p.removeDomain("Nope", 0));
        assertTrue(p.clearDomains("Default"));
        assertTrue(p.domains("Default").isEmpty());
        assertFalse("nothing to clear", p.clearDomains("Default"));
        assertFalse(p.clearDomains("Nope"));
    }

    @Test public void rulesTheCoreCannotUseAreDroppedAndTheOthersStayWhereTheyAre() {
        SplitProfiles p = SplitProfiles.migrated(new java.util.HashSet<>(list("intranet", "example.com:443", "example.com", "10.0.0.0/8")),
            Collections.<String>emptySet());
        assertEquals(list("10.0.0.0/8", "example.com", "example.com:443", "intranet"), p.domains("Default"));
        assertEquals(2, p.dropUnusableDomains("Default"));
        assertEquals(list("10.0.0.0/8", "example.com"), p.domains("Default"));
        assertEquals("nothing is left to drop", 0, p.dropUnusableDomains("Default"));
        assertEquals(0, p.dropUnusableDomains("Nope"));
    }

    @Test public void theFirstRuleIsLookedAtAsMuchAsTheLast() throws Exception {
        SplitProfiles p = SplitProfiles.fromJson("{\"profiles\":[{\"name\":\"Default\",\"domains\":[\"bad:1\",\"good.example\",\"intranet\"]}]}");
        assertEquals(2, p.dropUnusableDomains("Default"));
        assertEquals(list("good.example"), p.domains("Default"));
    }

    @Test public void aRuleTheCoreCannotUseStandsInTheWayOfNoOther() throws Exception {
        SplitProfiles p = SplitProfiles.fromJson("{\"profiles\":[{\"name\":\"Default\",\"domains\":[\"intranet\",\"good.example\"]}]}");
        SplitProfiles.Entered in = p.addDomains("Default", "other.example good.example intranet");
        assertEquals("only what is new and usable", list("other.example"), in.added);
        assertEquals("and the old one is a word that is refused, as it would be typed", list("intranet"), in.refused);
        assertEquals(list("intranet", "good.example", "other.example"), p.domains("Default"));
    }

    // What a refusal says.

    @Test public void aRefusalIsPlainUnlessItHasAReasonWorthSaying() {
        assertEquals(SplitProfiles.Refusal.PLAIN, SplitProfiles.refusal(list("intranet", "example.com:443")));
        assertEquals(SplitProfiles.Refusal.PLAIN, SplitProfiles.refusal(Collections.<String>emptyList()));
        // The address is fine; it is the wildcard that is not.
        assertEquals(SplitProfiles.Refusal.WILDCARD_ON_ADDRESS, SplitProfiles.refusal(list("*.1.2.3.4")));
        assertEquals(SplitProfiles.Refusal.WILDCARD_ON_ADDRESS, SplitProfiles.refusal(list(".10.0.0.0/8")));
        assertEquals(SplitProfiles.Refusal.WILDCARD_ON_ADDRESS, SplitProfiles.refusal(list("*.2001:db8::/32")));
        // A subnet of every address is every address.
        assertEquals(SplitProfiles.Refusal.EVERY_ADDRESS, SplitProfiles.refusal(list("0.0.0.0/0")));
        assertEquals(SplitProfiles.Refusal.EVERY_ADDRESS, SplitProfiles.refusal(list("::/0")));
        // A wildcard on that is neither: it is not an address with a wildcard, and not a subnet.
        assertEquals(SplitProfiles.Refusal.PLAIN, SplitProfiles.refusal(list("*.0.0.0.0/0")));
        // A bare address is no refusal at all; "0.0.0.0" is a host.
        assertEquals(SplitProfiles.Refusal.PLAIN, SplitProfiles.refusal(list("0.0.0.0")));
    }

    @Test public void theWildcardReasonWinsWhenBothAreAmongTheRefused() {
        assertEquals(SplitProfiles.Refusal.WILDCARD_ON_ADDRESS, SplitProfiles.refusal(list("0.0.0.0/0", "*.1.2.3.4", "intranet")));
        assertEquals(SplitProfiles.Refusal.EVERY_ADDRESS, SplitProfiles.refusal(list("intranet", "::/0")));
    }

    @Test public void theRulesThatAreRefusedWhenTypedAreTheOnesWithAReason() {
        SplitProfiles p = SplitProfiles.empty();
        assertEquals(SplitProfiles.Refusal.WILDCARD_ON_ADDRESS, SplitProfiles.refusal(p.addDomains("Default", "*.1.2.3.4").refused));
        assertEquals(SplitProfiles.Refusal.EVERY_ADDRESS, SplitProfiles.refusal(p.addDomains("Default", "0.0.0.0/0").refused));
        assertTrue("and neither was added", p.domains("Default").isEmpty());
    }

    // Applications.

    @Test public void anApplicationIsAddedOnceAtTheEnd() {
        SplitProfiles p = SplitProfiles.empty();
        assertTrue(p.addApp("Default", "org.b"));
        assertTrue(p.addApp("Default", "org.a"));
        assertFalse("listed already", p.addApp("Default", "org.b"));
        assertFalse(p.addApp("Default", ""));
        assertFalse(p.addApp("Default", "  "));
        assertFalse(p.addApp("Default", null));
        assertEquals("in the order they were added", list("org.b", "org.a"), p.apps("Default"));
    }

    @Test public void applicationsBelongToTheirProfile() {
        SplitProfiles p = SplitProfiles.empty();
        p.add("Work");
        assertTrue(p.addApp("Work", "org.work"));
        assertTrue(p.apps("Default").isEmpty());
        assertTrue(p.addApp("Default", "org.work"));
        assertEquals(list("org.work"), p.apps("Work"));
        assertFalse(p.addApp("Nope", "org.x"));
        assertTrue(p.apps("Nope").isEmpty());
    }

    @Test public void anApplicationIsRemovedByItsPlaceAndAllAreCleared() {
        SplitProfiles p = SplitProfiles.empty();
        p.addApp("Default", "org.a");
        p.addApp("Default", "org.b");
        p.addApp("Default", "org.c");
        assertTrue(p.removeApp("Default", 0));
        assertEquals(list("org.b", "org.c"), p.apps("Default"));
        assertFalse(p.removeApp("Default", 2));
        assertFalse(p.removeApp("Default", -1));
        assertTrue(p.clearApps("Default"));
        assertFalse(p.clearApps("Default"));
        assertTrue(p.apps("Default").isEmpty());
    }

    @Test public void theListsAProfileHandsOutCannotBeChangedFromOutside() {
        SplitProfiles p = SplitProfiles.empty();
        p.addDomains("Default", "a.example");
        p.addApp("Default", "org.a");
        try { p.domains("Default").add("x"); fail("domains are a copy to read"); } catch (UnsupportedOperationException expected) { }
        try { p.apps("Default").add("x"); fail("apps are a copy to read"); } catch (UnsupportedOperationException expected) { }
        try { p.names().add("x"); fail("names too"); } catch (UnsupportedOperationException expected) { }
    }

    // Saved form.

    @Test public void theJsonKeepsEverythingInItsOrder() throws Exception {
        SplitProfiles p = SplitProfiles.empty();
        p.addDomains("Default", "zeta.example 10.0.0.0/8 *.alpha.example мвд.рф");
        p.addApp("Default", "org.z");
        p.addApp("Default", "org.a");
        p.add("Work");
        p.addDomains("Work", "w.example");
        p.add("Рабочий");
        p.select("Work");
        p.setMirrored("Рабочий");
        SplitProfiles back = SplitProfiles.fromJson(p.toJson());
        assertEquals(list("Default", "Work", "Рабочий"), back.names());
        assertEquals(list("zeta.example", "10.0.0.0/8", "*.alpha.example", "мвд.рф"), back.domains("Default"));
        assertEquals(list("org.z", "org.a"), back.apps("Default"));
        assertEquals(list("w.example"), back.domains("Work"));
        assertTrue(back.domains("Рабочий").isEmpty());
        assertEquals("Work", back.edited());
        assertEquals("Рабочий", back.mirrored());
        assertEquals("and it writes what it read", p.toJson(), back.toJson());
    }

    @Test public void anEmptyOneRoundTrips() throws Exception {
        SplitProfiles back = SplitProfiles.fromJson(SplitProfiles.empty().toJson());
        assertEquals(list("Default"), back.names());
        assertEquals("Default", back.edited());
    }

    @Test public void whatIsReadIsMadeToFitRatherThanRefused() throws Exception {
        String json = "{\"v\":1,\"edit\":\"work\",\"mirror\":\"Gone\",\"profiles\":["
            + "{\"name\":\"Work\",\"domains\":[\"a.example\",\"a.example\",\"\",\" b.example \"],\"apps\":[\"org.a\",\"org.a\"]},"
            + "{\"name\":\"WORK\",\"domains\":[\"lost.example\"]},"
            + "{\"name\":\"  \"},"
            + "{\"domains\":[\"nameless.example\"]},"
            + "7,"
            + "{\"name\":\"Default\",\"domains\":[\"d.example\"]},"
            + "{\"name\":\"Default\",\"domains\":[\"twice.example\"]}]}";
        SplitProfiles p = SplitProfiles.fromJson(json);
        assertEquals("Default first however it was written; the second Work, the nameless and the blank are gone",
            list("Default", "Work"), p.names());
        assertEquals(list("d.example"), p.domains("Default"));
        assertEquals("duplicates and blanks out, spaces trimmed", list("a.example", "b.example"), p.domains("Work"));
        assertEquals(list("org.a"), p.apps("Work"));
        assertEquals("'work' is not a name that is there", "Default", p.edited());
        assertEquals("Default", p.mirrored());
    }

    @Test public void aListWithNoDefaultGetsOne() throws Exception {
        SplitProfiles p = SplitProfiles.fromJson("{\"profiles\":[{\"name\":\"Work\",\"domains\":[\"w.example\"]}],\"edit\":\"Work\"}");
        assertEquals(list("Default", "Work"), p.names());
        assertTrue(p.domains("Default").isEmpty());
        assertEquals("Work", p.edited());
    }

    @Test public void textThatIsNoProfileListIsRefused() {
        for (String bad : list("", "not json", "[]", "{}", "{\"profiles\":3}", "{\"profiles\":")) {
            try {
                SplitProfiles.fromJson(bad);
                fail("accepted: " + bad);
            } catch (Exception expected) {
                // Refused, whatever it is called.
            }
        }
    }

    @Test public void nothingInTheJsonNeedsToBeEscapedByTheReader() throws Exception {
        // A rule with a slash is written as \/ by Android's JSON, and read back the same.
        SplitProfiles p = SplitProfiles.empty();
        p.addDomains("Default", "192.168.0.0/16");
        assertEquals(list("192.168.0.0/16"), SplitProfiles.fromJson(p.toJson()).domains("Default"));
        SplitProfiles slash = SplitProfiles.fromJson("{\"profiles\":[{\"name\":\"Default\",\"domains\":[\"10.0.0.0\\/8\"]}]}");
        assertEquals(list("10.0.0.0/8"), slash.domains("Default"));
    }
}
