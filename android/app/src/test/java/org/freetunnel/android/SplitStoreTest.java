package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;

// Where the profiles are kept, and the first start of this version: an update from 1.0.0's two lists, a new
// installation, and the old lists that stay a copy of the profile in use.
public final class SplitStoreTest {
    private static List<String> list(String... items) { return Arrays.asList(items); }
    private static Set<String> set(String... items) { return new HashSet<>(Arrays.asList(items)); }
    private static SplitProfiles load(MemoryBacking f) { return SplitStore.load(f, null); }

    // The recommended list asks to be read; whoever has none to give says so with null.
    private static final class Counting implements java.util.function.Supplier<List<String>> {
        final AtomicInteger calls = new AtomicInteger();
        final List<String> rules;
        Counting(String... rules) { this.rules = Arrays.asList(rules); }
        @Override public List<String> get() { calls.incrementAndGet(); return rules; }
    }

    // A new installation.

    @Test public void aNewInstallationIsSplitOnAndDefaultHoldsTheRecommendedList() {
        MemoryBacking f = new MemoryBacking();
        Counting recommended = new Counting("yandex.ru", "*.yandex.ru");
        SplitProfiles p = SplitStore.load(f, recommended);
        assertEquals(list("yandex.ru", "*.yandex.ru"), p.domains("Default"));
        assertEquals("Split tunnelling is on, as on the desktop", Boolean.TRUE, f.data.get("split_enabled"));
        assertNotNull(f.getString(SplitStore.KEY));
        assertEquals("the old lists are a copy of Default", set("yandex.ru", "*.yandex.ru"), f.set("domains"));
        assertEquals(new HashSet<String>(), f.set("apps"));
        assertNull("the mode is left to its default, the desktop's Bypass VPN", f.data.get("through"));
        // Made once: the next read is what was saved, and does not ask for the list.
        SplitProfiles again = SplitStore.load(f, recommended);
        assertEquals(1, recommended.calls.get());
        assertEquals(p.toJson(), again.toJson());
    }

    @Test public void anythingTheAppHasWrittenBeforeMakesItNoNewInstallation() {
        for (String key : list("domains", "apps", "split_enabled", "through", "configs_encrypted", "configs")) {
            MemoryBacking f = new MemoryBacking();
            if (key.equals("domains") || key.equals("apps")) f.with(key, set());
            else if (key.equals("split_enabled") || key.equals("through")) f.with(key, Boolean.FALSE);
            else f.with(key, "[]");
            Counting recommended = new Counting("yandex.ru");
            SplitProfiles p = SplitStore.load(f, recommended);
            assertEquals(key, 0, recommended.calls.get());
            assertTrue(key + ": Default is empty", p.domains("Default").isEmpty());
            assertEquals(key + ": split tunnelling is as it was", key.equals("split_enabled") ? Boolean.FALSE : null, f.data.get("split_enabled"));
        }
    }

    @Test public void whoeverGivesNoRecommendedListNeverMakesANewInstallation() {
        // The VPN service: it has configs by the time it runs, and prefs that are empty are only empty.
        MemoryBacking f = new MemoryBacking();
        SplitProfiles p = load(f);
        assertTrue(p.domains("Default").isEmpty());
        assertNull(f.data.get("split_enabled"));
        assertEquals(list("Default"), p.names());
    }

    @Test public void aNewInstallationThatCannotReadTheListIsStillAnInstallation() {
        MemoryBacking f = new MemoryBacking();
        SplitProfiles p = SplitStore.load(f, () -> Collections.<String>emptyList());
        assertTrue(p.domains("Default").isEmpty());
        assertEquals("and split tunnelling is on", Boolean.TRUE, f.data.get("split_enabled"));
    }

    // An update from 1.0.0.

    @Test public void anUpdateMakesDefaultOfTheTwoOldListsSortedAndTouchesNothingElse() {
        MemoryBacking f = new MemoryBacking().with("domains", set("b.example", "a.example")).with("apps", set("org.z", "org.a"))
            .with("split_enabled", Boolean.FALSE).with("through", Boolean.TRUE).with("configs_encrypted", "x");
        Counting recommended = new Counting("yandex.ru");
        SplitProfiles p = SplitStore.load(f, recommended);
        assertEquals(0, recommended.calls.get());
        assertEquals(list("a.example", "b.example"), p.domains("Default"));
        assertEquals(list("org.a", "org.z"), p.apps("Default"));
        assertEquals("Default", p.edited());
        assertEquals(Boolean.FALSE, f.data.get("split_enabled"));
        assertEquals(Boolean.TRUE, f.data.get("through"));
        assertEquals("the old lists are still there for 1.0.0", set("a.example", "b.example"), f.set("domains"));
        assertEquals(set("org.a", "org.z"), f.set("apps"));
        // It was done once. What 1.0.0 might write to its lists after that is not read.
        f.data.put("domains", set("later.example"));
        assertEquals(list("a.example", "b.example"), SplitStore.load(f, recommended).domains("Default"));
    }

    @Test public void anUpdateWithNoListsAtAllHasAnEmptyDefault() {
        MemoryBacking f = new MemoryBacking().with("configs_encrypted", "x");
        SplitProfiles p = load(f);
        assertTrue(p.domains("Default").isEmpty() && p.apps("Default").isEmpty());
    }

    @Test public void savedProfilesThatCannotBeReadAreMadeAgainFromTheOldLists() throws Exception {
        MemoryBacking f = new MemoryBacking().with(SplitStore.KEY, "{ broken").with("domains", set("a.example")).with("apps", set("org.a"));
        SplitProfiles p = load(f);
        assertEquals(list("a.example"), p.domains("Default"));
        assertEquals(list("org.a"), p.apps("Default"));
        assertEquals("and what is kept reads", list("Default"), SplitProfiles.fromJson(f.getString(SplitStore.KEY)).names());
        assertEquals(list("a.example"), SplitProfiles.fromJson(f.getString(SplitStore.KEY)).domains("Default"));
        assertEquals(list("a.example"), load(f).domains("Default"));
    }

    // A change.

    @Test public void aChangeIsSavedAndReadBack() {
        MemoryBacking f = new MemoryBacking().with("configs", "[]");
        assertTrue(SplitStore.edit(f, null, p -> {
            p.add("Work");
            p.addDomains("Work", "w.example");
            p.addApp("Work", "org.w");
        }));
        SplitProfiles p = load(f);
        assertEquals(list("Default", "Work"), p.names());
        assertEquals("Work", p.edited());
        assertEquals(list("w.example"), p.domains("Work"));
        assertEquals(list("org.w"), p.apps("Work"));
    }

    @Test public void theOldListsAreACopyOfTheProfileInUse() {
        MemoryBacking f = new MemoryBacking().with("configs", "[]");
        SplitStore.edit(f, null, p -> {
            p.addDomains("Default", "home.example");
            p.addApp("Default", "org.home");
            p.add("Work");
            p.addDomains("Work", "work.example");
        });
        assertEquals("while Default is the one in use, a change to Work does not move them", set("home.example"), f.set("domains"));
        assertEquals(set("org.home"), f.set("apps"));
        SplitStore.edit(f, null, p -> p.setMirrored("Work"));
        assertEquals(set("work.example"), f.set("domains"));
        assertEquals(new HashSet<String>(), f.set("apps"));
        SplitStore.edit(f, null, p -> p.addDomains("Work", "more.example"));
        assertEquals("a change to the profile in use is in them", set("work.example", "more.example"), f.set("domains"));
        SplitStore.edit(f, null, p -> p.remove("Work"));
        assertEquals("Deleting it puts Default back", set("home.example"), f.set("domains"));
    }

    @Test public void aChangeThatChangesNothingIsNotSaved() {
        MemoryBacking f = new MemoryBacking().with("configs", "[]");
        load(f);
        int before = f.commits;
        assertTrue(SplitStore.edit(f, null, p -> { p.select("Default"); p.clearDomains("Default"); p.remove("Nope"); }));
        assertEquals(before, f.commits);
    }

    @Test public void aCommitThePreferencesRefuseIsReportedAndLeavesWhatWasSaved() {
        MemoryBacking f = new MemoryBacking().with("configs", "[]");
        load(f);
        f.refuse = true;
        assertFalse(SplitStore.edit(f, null, p -> p.add("Work")));
        f.refuse = false;
        assertEquals("nothing half done", list("Default"), load(f).names());
    }

    @Test public void aProfileDeletedIsGoneWithItsListsForGood() {
        MemoryBacking f = new MemoryBacking().with("configs", "[]");
        SplitStore.edit(f, null, p -> { p.add("Work"); p.addDomains("Work", "w.example"); p.addApp("Work", "org.w"); });
        SplitStore.edit(f, null, p -> p.remove("Work"));
        SplitStore.edit(f, null, p -> p.add("Work"));
        assertTrue("made again, it starts empty", load(f).domains("Work").isEmpty() && load(f).apps("Work").isEmpty());
    }

    @Test public void twoThreadsChangingTogetherLoseNothing() throws Exception {
        final MemoryBacking f = new MemoryBacking().with("configs", "[]");
        load(f);
        Thread[] threads = new Thread[2];
        final List<Throwable> failures = Collections.synchronizedList(new ArrayList<Throwable>());
        for (int t = 0; t < threads.length; t++) {
            final int id = t;
            threads[t] = new Thread(() -> {
                try {
                    for (int i = 0; i < 150; i++) {
                        final String rule = "r" + id + "-" + i + ".example";
                        assertTrue(SplitStore.edit(f, null, p -> p.addDomains("Default", rule)));
                    }
                } catch (Throwable e) { failures.add(e); }
            });
            threads[t].start();
        }
        for (Thread thread : threads) thread.join();
        assertTrue(failures.toString(), failures.isEmpty());
        assertEquals(300, load(f).domains("Default").size());
        assertEquals("the copy is whole too", 300, f.set("domains").size());
    }

    // What a session is given.

    @Test public void aSessionGetsTheRulesOfItsProfile() {
        MemoryBacking f = new MemoryBacking().with("configs", "[]");
        SplitStore.edit(f, null, p -> {
            p.addDomains("Default", "home.example");
            p.addApp("Default", "org.home");
            p.add("Work");
            p.addDomains("Work", "work.example");
            p.addApp("Work", "org.work1");
            p.addApp("Work", "org.work2");
        });
        SplitStore.Rules work = SplitStore.rules(f, null, "Work");
        assertEquals(list("work.example"), work.domains);
        assertEquals(list("org.work1", "org.work2"), work.apps);
        SplitStore.Rules home = SplitStore.rules(f, null, "Default");
        assertEquals(list("home.example"), home.domains);
        assertEquals(list("org.home"), home.apps);
    }

    @Test public void aProfileThatIsGoneIsDefaultForASession() {
        MemoryBacking f = new MemoryBacking().with("configs", "[]");
        SplitStore.edit(f, null, p -> p.addDomains("Default", "home.example"));
        assertEquals(list("home.example"), SplitStore.rules(f, null, "Gone").domains);
        assertEquals(list("home.example"), SplitStore.rules(f, null, null).domains);
    }

    @Test public void rulesTheCoreCannotUseAreDroppedForGoodAndTheSplitPageIsToldOnce() {
        MemoryBacking f = new MemoryBacking().with("split_enabled", Boolean.TRUE)
            .with("domains", set("intranet", "example.com:443", "example.com", "10.0.0.0/8")).with("apps", set("org.a"));
        SplitStore.Rules rules = SplitStore.rules(f, null, "Default");
        assertEquals(list("10.0.0.0/8", "example.com"), rules.domains);
        assertEquals(list("org.a"), rules.apps);
        assertEquals(Boolean.TRUE, f.data.get(SplitStore.RULES_MIGRATED));
        assertEquals("1.0.0's list is the same, for going back", set("example.com", "10.0.0.0/8"), f.set("domains"));
        assertEquals(list("10.0.0.0/8", "example.com"), load(f).domains("Default"));
        int commits = f.commits;
        assertEquals(list("10.0.0.0/8", "example.com"), SplitStore.rules(f, null, "Default").domains);
        assertEquals("nothing is left to drop, so nothing is saved", commits, f.commits);
    }

    @Test public void rulesOnlyTheProfileAskedAboutIsCleaned() {
        MemoryBacking f = new MemoryBacking().with(SplitStore.KEY,
            "{\"profiles\":[{\"name\":\"Default\",\"domains\":[\"intranet\"]},{\"name\":\"Work\",\"domains\":[\"work.example\",\"bad:1\"]}]}");
        assertEquals(list("work.example"), SplitStore.rules(f, null, "Work").domains);
        assertEquals("Default is another profile's business", list("intranet"), load(f).domains("Default"));
        assertEquals(list("work.example"), load(f).domains("Work"));
        assertEquals(Boolean.TRUE, f.data.get(SplitStore.RULES_MIGRATED));
    }

    @Test public void aSessionForCleanRulesSetsNoNotice() {
        MemoryBacking f = new MemoryBacking().with("configs", "[]");
        SplitStore.edit(f, null, p -> p.addDomains("Default", "a.example"));
        SplitStore.rules(f, null, "Default");
        assertNull(f.data.get(SplitStore.RULES_MIGRATED));
    }

    @Test public void theNoticeFlagIsTheOneTheOldVersionWrote() {
        assertEquals("rules_migrated", SplitStore.RULES_MIGRATED);
        assertEquals("split_profiles", SplitStore.KEY);
    }

    @Test public void theRulesHandedOutAreCopies() {
        MemoryBacking f = new MemoryBacking().with("configs", "[]");
        SplitStore.edit(f, null, p -> { p.addDomains("Default", "a.example"); p.addApp("Default", "org.a"); });
        SplitStore.Rules rules = SplitStore.rules(f, null, "Default");
        rules.domains.clear();
        rules.apps.clear();
        assertEquals(list("a.example"), load(f).domains("Default"));
        assertEquals(list("org.a"), load(f).apps("Default"));
    }

    @Test public void aFailedStoreDoesNotMakeASessionFail() {
        MemoryBacking f = new MemoryBacking().with("domains", set("intranet", "a.example"));
        f.refuse = true;
        SplitStore.Rules rules = SplitStore.rules(f, null, "Default");
        assertEquals("it still gets the rules the core can use", list("a.example"), rules.domains);
    }
}
