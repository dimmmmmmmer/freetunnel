package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import org.junit.Test;

import java.util.Arrays;
import java.util.Locale;

// Settings > Language: which saved values are a language of the app's, and which language a locale gets. The
// rest of AppLocale is Android's (a wrapped context, the system's LocaleManager) and is checked on a device.
public final class AppLocaleTest {
    @Test public void onlyTheTwoLanguagesAreChoices() {
        assertEquals("en", AppLocale.valid("en"));
        assertEquals("ru", AppLocale.valid("ru"));
        assertEquals(Arrays.asList("en", "ru"), Arrays.asList(AppLocale.LANGUAGES));
    }

    @Test public void anythingElseSavedMeansTheSystemsLanguage() {
        for (String saved : new String[] {null, "", "EN", "ru-RU", "de", "system", " ru", "ru "}) assertNull("[" + saved + "]", AppLocale.valid(saved));
    }

    @Test public void russianIsRussianAndEveryOtherLocaleGetsTheDefaultEnglish() {
        assertEquals("ru", AppLocale.languageOf(new Locale("ru")));
        assertEquals("ru", AppLocale.languageOf(new Locale("ru", "RU")));
        assertEquals("ru", AppLocale.languageOf(Locale.forLanguageTag("ru-UA")));
        assertEquals("en", AppLocale.languageOf(Locale.ENGLISH));
        assertEquals("en", AppLocale.languageOf(Locale.US));
        assertEquals("en", AppLocale.languageOf(Locale.GERMAN));
        assertEquals("en", AppLocale.languageOf(Locale.forLanguageTag("uk-UA")));
        assertEquals("en", AppLocale.languageOf(Locale.ROOT));
        assertEquals("en", AppLocale.languageOf(null));
    }
}
