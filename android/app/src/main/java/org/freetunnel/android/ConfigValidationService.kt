package org.freetunnel.android

import android.app.Service
import android.content.Intent
import android.os.*
import com.adguard.trusttunnel.VpnClient

/** Private, unprivileged process: no network permission, files, or active VPN state. */
class ConfigValidationService : Service() {
    override fun onBind(intent: Intent): IBinder = Messenger(Handler(Looper.getMainLooper()) { message ->
        validate(message.data, message.replyTo)
        true
    }).binder

    private fun validate(data: Bundle, result: Messenger?) {
        @Suppress("DEPRECATION")
        val input = data.getParcelable<ParcelFileDescriptor>("input")
        Thread({
            val accepted = runCatching {
                require(input != null)
                val bytes = ParcelFileDescriptor.AutoCloseInputStream(input).use { stream ->
                    val output = java.io.ByteArrayOutputStream()
                    val buffer = ByteArray(16384)
                    while (true) {
                        val count = stream.read(buffer)
                        if (count < 0) break
                        require(output.size() + count <= 16_777_216)
                        output.write(buffer, 0, count)
                    }
                    output.toByteArray()
                }
                val source = bytes.toString(Charsets.UTF_8)
                RoutingConfig.validateGenerated(source)
                val owner = VpnClient(source, null)
                val create = VpnClient::class.java.getDeclaredMethod("createNative", String::class.java).apply { isAccessible = true }
                val destroy = VpnClient::class.java.getDeclaredMethod("destroyNative", java.lang.Long.TYPE).apply { isAccessible = true }
                val pointer = create.invoke(owner, source) as Long
                require(pointer != 0L && pointer != 1L)
                destroy.invoke(owner, pointer)
            }.isSuccess
            runCatching { result?.send(Message.obtain(null, if (accepted) 1 else 0)) }
        }, "validate-config").start()
    }

    override fun onUnbind(intent: Intent): Boolean {
        Process.killProcess(Process.myPid())
        return false
    }
}
