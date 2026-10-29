package org.freetunnel.android

import org.junit.Assert.assertEquals
import org.junit.Test

// Settings' update line (UpdateLine, plain Java, tested on its own) takes the updater's state by its name: the two lists
// have to stay the same list. Only the nested enum is loaded here, not the updater, which needs Android.
class UpdateStatesTest {
    @Test fun theUpdateLineKnowsEveryStateOfTheUpdaterByName() {
        assertEquals(AndroidUpdater.State.values().map { it.name }, UpdateLine.Phase.values().map { it.name })
    }
}
