package org.freetunnel.android

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.*
import com.adguard.trusttunnel.VpnServiceConfig
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** Native validation runs in a disposable isolated process, never in the VPN process. */
object NativeConfigValidator {
    private val lock = Any()

    fun validate(context: Context, source: String) {
        check(Looper.myLooper() != Looper.getMainLooper()) { "Validate configurations on a worker thread" }
        RoutingConfig.validateGenerated(source)
        require(VpnServiceConfig.parseToml(RoutingConfig.adapterSettings(source)) != null) { "Android adapter rejected the configuration" }
        val app = context.applicationContext
        synchronized(lock) {
            val done = CountDownLatch(1)
            val died = CountDownLatch(1)
            var accepted = false
            val pipe = ParcelFileDescriptor.createPipe()
            val response = Messenger(Handler(Looper.getMainLooper()) { message ->
                accepted = message.what == 1
                done.countDown()
                true
            })
            val connection = object : ServiceConnection {
                override fun onServiceConnected(name: ComponentName, binder: IBinder) {
                    try {
                        binder.linkToDeath({ died.countDown(); done.countDown() }, 0)
                        Messenger(binder).send(Message.obtain().apply {
                            data = Bundle().apply { putParcelable("input", pipe[0]) }
                            replyTo = response
                        })
                        pipe[0].close()
                    } catch (_: Exception) { died.countDown(); done.countDown() }
                }
                override fun onServiceDisconnected(name: ComponentName) { died.countDown(); done.countDown() }
                override fun onNullBinding(name: ComponentName) { done.countDown() }
                override fun onBindingDied(name: ComponentName) { died.countDown(); done.countDown() }
            }
            var bound = false
            try {
                bound = app.bindService(Intent(app, ConfigValidationService::class.java), connection, Context.BIND_AUTO_CREATE)
                check(bound) { "Could not bind configuration validator" }
                // Pipe content stays in memory and avoids Binder's transaction-size limit.
                Thread({ runCatching {
                    ParcelFileDescriptor.AutoCloseOutputStream(pipe[1]).use { it.write(source.toByteArray(Charsets.UTF_8)) }
                }.onFailure { done.countDown() } }, "validation-input").apply { isDaemon = true }.start()
                check(done.await(20, TimeUnit.SECONDS)) { "Configuration validation timed out" }
                require(accepted) { "VPN core rejected the server configuration" }
            } finally {
                pipe.forEach { runCatching { it.close() } }
                if (bound) {
                    app.unbindService(connection)
                    // onUnbind destroys the process, releasing the adapter's leaked
                    // UTF-8 copy and all process-global native configuration.
                    check(died.await(10, TimeUnit.SECONDS)) { "Configuration validator did not exit" }
                }
            }
        }
    }
}
