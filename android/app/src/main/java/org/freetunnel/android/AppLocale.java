package org.freetunnel.android;

import android.app.Activity;
import android.app.LocaleManager;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.os.Build;
import android.os.LocaleList;

import java.util.Locale;

/**
 * Settings > Language (qml/pages/SettingsPage.qml): English or Русский, as on the desktop, whatever the system's language is.
 * Chosen here, saved as the preference "language" ("en" or "ru"); where there is none, the app speaks the system's.
 *
 * <ul>
 * <li><b>Android 13 and up</b> keeps an app's language itself (LocaleManager): choosing one changes it for the whole app,
 * the system recreates the activity, and nothing else has to be done.</li>
 * <li><b>Android 8 to 12</b> has nothing of the kind, so every context that shows words, the activity, the VPN service
 * (its notifications) and the quick settings tile, is wrapped in one that has the saved language ({@link #wrap}, from
 * attachBaseContext), and choosing one recreates the activity.</li>
 * </ul>
 */
final class AppLocale {
    static final String KEY = "language";
    static final String ENGLISH = "en";
    static final String RUSSIAN = "ru";
    /** The languages, in the order the row offers them. Each is named in itself, which is never translated. */
    static final String[] LANGUAGES = {ENGLISH, RUSSIAN};

    private AppLocale() { }

    /** A saved language if it is one of ours, else null: no choice was made, or it is one this version does not have. */
    static String valid(String saved) { return ENGLISH.equals(saved) || RUSSIAN.equals(saved) ? saved : null; }

    /** The language of ours that this locale gets: Russian for Russian, English for any other (the strings' default). */
    static String languageOf(Locale locale) { return locale != null && RUSSIAN.equals(locale.getLanguage()) ? RUSSIAN : ENGLISH; }

    /** The language the Language row shows: the one the app is speaking right now, which is what the user can read. */
    static String current(Context context) {
        return languageOf(context.getResources().getConfiguration().getLocales().get(0));
    }

    /**
     * For attachBaseContext, before Android 13: the context in the saved language, which is the same one where there is no
     * saved language. Only the language is overridden, and nothing else of the configuration, so that a turn of the screen or
     * a new window size still reaches the resources.
     */
    static Context wrap(Context base) {
        if (Build.VERSION.SDK_INT >= 33) return base;
        String language = valid(base.getSharedPreferences("freetunnel", Context.MODE_PRIVATE).getString(KEY, null));
        if (language == null) return base;
        Configuration override = new Configuration();
        override.setLocale(Locale.forLanguageTag(language));
        return base.createConfigurationContext(override);
    }

    /**
     * Makes the app speak this language from now on. The preference is written first, since the activity that comes back
     * reads it. Android 13 and up recreates the activity by itself once its locales are set; before that this does.
     */
    static void choose(Activity activity, SharedPreferences prefs, String language) {
        if (valid(language) == null) return;
        prefs.edit().putString(KEY, language).commit();
        if (Build.VERSION.SDK_INT >= 33) {
            activity.getSystemService(LocaleManager.class).setApplicationLocales(new LocaleList(Locale.forLanguageTag(language)));
        } else {
            activity.recreate();
        }
    }
}
