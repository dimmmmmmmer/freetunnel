package org.freetunnel.android;

/**
 * What the Maintenance row shows and does for each state of the updater (qml/pages/SettingsPage.qml, "Maintenance"): the words,
 * the picture to their right, whether a click or OK does anything, and what else sits at the right. The words are the desktop's
 * for the states it has, and Android's own for the states only it has (a downloaded update waiting for Android's installer).
 * Plain Java: the activity maps AndroidUpdater.State to {@link Phase} by name, so that this table is checked on a plain JVM
 * (UpdateLineTest).
 */
final class UpdateLine {
    /** AndroidUpdater.State, by name. */
    enum Phase { IDLE, CHECKING, CURRENT, AVAILABLE, DOWNLOADING, READY, INSTALLING, ERROR }
    /** Which words the line says. */
    enum Words { CHECK, CHECKING, AVAILABLE, DOWNLOADING, READY, INSTALLING, CHECK_FAILED, FAILED }
    /** The picture beside the words: none, hopping dots (waiting on an answer), ↓ (a download is offered), a turning ↻ (one is coming down, and cannot be stopped, as the desktop's cannot), ↻ (try again). */
    enum Icon { NONE, DOTS, DOWNLOAD, SPINNER, RETRY }
    /** What a click or OK does. */
    enum Action { NONE, CHECK, DOWNLOAD, INSTALL }

    final Words words;
    final Icon icon;
    final Action action;
    /** ✓ in success, at the right: the app is up to date. */
    final boolean tick;
    /** The app's version, 13 in textFaint, at the right: nothing has been said but that it is current or not yet asked about. */
    final boolean version;

    private UpdateLine(Words words, Icon icon, Action action, boolean tick, boolean version) {
        this.words = words;
        this.icon = icon;
        this.action = action;
        this.tick = tick;
        this.version = version;
    }

    /** Whether the line does anything when it is clicked. It stays a focus stop either way, so that a remote's focus is not lost. */
    boolean clickable() { return action != Action.NONE; }

    /**
     * The line for a state. {@code reinstall}: Android has the install session, but its window was lost, so Install update works
     * again from INSTALLING. {@code failedCheck}: the error is the check's, which the desktop words "Update check failed: ...".
     */
    static UpdateLine of(Phase phase, boolean reinstall, boolean failedCheck) {
        switch (phase) {
            case CHECKING: return new UpdateLine(Words.CHECKING, Icon.DOTS, Action.NONE, false, false);
            case CURRENT: return new UpdateLine(Words.CHECK, Icon.NONE, Action.CHECK, true, true);
            case AVAILABLE: return new UpdateLine(Words.AVAILABLE, Icon.DOWNLOAD, Action.DOWNLOAD, false, false);
            case DOWNLOADING: return new UpdateLine(Words.DOWNLOADING, Icon.SPINNER, Action.NONE, false, false);
            case READY: return new UpdateLine(Words.READY, Icon.NONE, Action.INSTALL, false, false);
            case INSTALLING: return new UpdateLine(Words.INSTALLING, Icon.NONE, reinstall ? Action.INSTALL : Action.NONE, false, false);
            case ERROR: return new UpdateLine(failedCheck ? Words.CHECK_FAILED : Words.FAILED, Icon.RETRY, Action.CHECK, false, false);
            default: return new UpdateLine(Words.CHECK, Icon.NONE, Action.CHECK, false, true);
        }
    }
}
