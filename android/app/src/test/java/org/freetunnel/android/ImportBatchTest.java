package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import java.util.Arrays;
import java.util.Collections;
import org.junit.Test;

public final class ImportBatchTest {
    @Test public void separatesLinksInPasteOrderIncludingInvalidEntries() {
        assertEquals(Arrays.asList("tt://one", "bad", "tt://two", "https://trusttunnel.org/qr.html#three"),
                ImportBatch.split("\uFEFF tt://one\r\nbad\ttt://two https://trusttunnel.org/qr.html#three "));
    }

    @Test public void preservesMultilineToml() {
        String toml = "[endpoint]\nname = \"\"\"line one\nline two\"\"\"\npassword = \"tt://not-a-link\"";
        assertEquals(Collections.singletonList(toml), ImportBatch.split(toml));
    }

    @Test public void boundsBatchCountAndUtf8Bytes() {
        assertEquals(100, ImportBatch.split(String.join("\n", Collections.nCopies(100, "tt://one"))).size());
        assertThrows(IllegalArgumentException.class,
                () -> ImportBatch.split(String.join("\n", Collections.nCopies(101, "tt://one"))));
        assertThrows(IllegalArgumentException.class,
                () -> ImportBatch.split(String.join("", Collections.nCopies(ImportBatch.MAX_BYTES / 2 + 1, "я"))));
    }
}
