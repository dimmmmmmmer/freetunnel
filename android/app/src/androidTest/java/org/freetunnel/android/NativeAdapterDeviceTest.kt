package org.freetunnel.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.adguard.trusttunnel.VpnClient
import org.junit.Assert.assertFalse
import org.junit.Test
import org.junit.runner.RunWith

/** No VPN consent or network is needed: malformed input must fail before native startup. */
@RunWith(AndroidJUnit4::class)
class NativeAdapterDeviceTest {
    @Test fun malformedTomlDoesNotCrashOrLeaveANativeClient() {
        for (source in listOf("[", "endpoint =", "")) {
            VpnClient(source, null).use { client ->
                repeat(3) { assertFalse(client.start(null)) }
                client.stop()
            }
        }
    }
}
