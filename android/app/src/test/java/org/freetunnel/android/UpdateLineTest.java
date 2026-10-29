package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

// What the Maintenance row shows and does for each state of the updater (UpdateLine).
public final class UpdateLineTest {
    private static UpdateLine of(UpdateLine.Phase phase) { return UpdateLine.of(phase, false, false); }

    @Test public void idleOffersACheckAndTheVersion() {
        UpdateLine line = of(UpdateLine.Phase.IDLE);
        assertEquals(UpdateLine.Words.CHECK, line.words);
        assertEquals(UpdateLine.Icon.NONE, line.icon);
        assertEquals(UpdateLine.Action.CHECK, line.action);
        assertTrue(line.version);
        assertFalse(line.tick);
    }

    @Test public void upToDateIsTheSameLineWithATickBesideTheVersion() {
        UpdateLine line = of(UpdateLine.Phase.CURRENT);
        assertEquals(UpdateLine.Words.CHECK, line.words);
        assertEquals(UpdateLine.Action.CHECK, line.action);
        assertTrue(line.tick);
        assertTrue(line.version);
        assertEquals(UpdateLine.Icon.NONE, line.icon);
    }

    @Test public void checkingShowsDotsAndDoesNothingWhenClicked() {
        UpdateLine line = of(UpdateLine.Phase.CHECKING);
        assertEquals(UpdateLine.Words.CHECKING, line.words);
        assertEquals(UpdateLine.Icon.DOTS, line.icon);
        assertFalse(line.clickable());
        assertFalse(line.version);
        assertFalse(line.tick);
    }

    @Test public void anOfferedUpdateIsAnArrowDownThatDownloads() {
        UpdateLine line = of(UpdateLine.Phase.AVAILABLE);
        assertEquals(UpdateLine.Words.AVAILABLE, line.words);
        assertEquals(UpdateLine.Icon.DOWNLOAD, line.icon);
        assertEquals(UpdateLine.Action.DOWNLOAD, line.action);
        assertFalse(line.version);
    }

    // The desktop cannot stop a download (its line is not actionable while one runs), so neither can this.
    @Test public void aDownloadIsATurningArrowThatDoesNothingWhenClicked() {
        UpdateLine line = of(UpdateLine.Phase.DOWNLOADING);
        assertEquals(UpdateLine.Words.DOWNLOADING, line.words);
        assertEquals(UpdateLine.Icon.SPINNER, line.icon);
        assertEquals(UpdateLine.Action.NONE, line.action);
        assertFalse(line.clickable());
    }

    @Test public void aDownloadedUpdateInstallsThroughTheConfirmationAndHasNoPicture() {
        UpdateLine line = of(UpdateLine.Phase.READY);
        assertEquals(UpdateLine.Words.READY, line.words);
        assertEquals(UpdateLine.Icon.NONE, line.icon);
        assertEquals(UpdateLine.Action.INSTALL, line.action);
    }

    @Test public void installingWaitsForAndroidAndOnlyInstallsAgainOnceItsWindowIsLost() {
        UpdateLine waiting = UpdateLine.of(UpdateLine.Phase.INSTALLING, false, false);
        assertEquals(UpdateLine.Words.INSTALLING, waiting.words);
        assertEquals(UpdateLine.Action.NONE, waiting.action);
        assertFalse(waiting.clickable());
        UpdateLine lost = UpdateLine.of(UpdateLine.Phase.INSTALLING, true, false);
        assertEquals(UpdateLine.Words.INSTALLING, lost.words);
        assertEquals(UpdateLine.Action.INSTALL, lost.action);
        assertTrue(lost.clickable());
    }

    @Test public void anErrorIsTheDesktopsWordsWithAnArrowThatTriesAgain() {
        UpdateLine check = UpdateLine.of(UpdateLine.Phase.ERROR, false, true);
        assertEquals("a check that failed says so", UpdateLine.Words.CHECK_FAILED, check.words);
        assertEquals(UpdateLine.Icon.RETRY, check.icon);
        assertEquals(UpdateLine.Action.CHECK, check.action);
        UpdateLine other = UpdateLine.of(UpdateLine.Phase.ERROR, false, false);
        assertEquals("a download or an install that failed says why, as it is", UpdateLine.Words.FAILED, other.words);
        assertEquals(UpdateLine.Icon.RETRY, other.icon);
        assertEquals(UpdateLine.Action.CHECK, other.action);
        assertFalse(check.version || other.version || check.tick || other.tick);
    }

    @Test public void everyStateOfTheUpdaterHasALine() {
        for (UpdateLine.Phase phase : UpdateLine.Phase.values()) {
            for (boolean reinstall : new boolean[] {false, true}) for (boolean failedCheck : new boolean[] {false, true}) {
                UpdateLine line = UpdateLine.of(phase, reinstall, failedCheck);
                assertTrue(phase + "", line.words != null && line.icon != null && line.action != null);
            }
        }
    }

    @Test public void onlyTheStatesThatAskNothingOfTheUserShowTheVersion() {
        for (UpdateLine.Phase phase : UpdateLine.Phase.values()) {
            boolean idle = phase == UpdateLine.Phase.IDLE || phase == UpdateLine.Phase.CURRENT;
            assertEquals(phase + "", idle, of(phase).version);
        }
    }
}
