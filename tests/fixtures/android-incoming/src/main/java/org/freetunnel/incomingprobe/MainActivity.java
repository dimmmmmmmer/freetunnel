package org.freetunnel.incomingprobe;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;
import java.io.File;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Collections;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Test-only server under its own UID, never packaged with FreeTunnel. */
public final class MainActivity extends Activity {
    private static final String TAG = "IncomingProbe";
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        TextView view = new TextView(this);
        view.setText("Incoming TCP/UDP routing probe");
        setContentView(view);
        new Thread(() -> {
            try {
                InetAddress local = null;
                for (NetworkInterface iface : Collections.list(NetworkInterface.getNetworkInterfaces()))
                    for (InetAddress address : Collections.list(iface.getInetAddresses()))
                        if (address instanceof Inet4Address && address.getHostAddress().startsWith("10.0.2.")) local = address;
                if (local == null) throw new IllegalStateException("No emulator uplink address");
                CountDownLatch ready = new CountDownLatch(4);
                tcp(local, 18081, ready); tcp(null, 18082, ready);
                udp(local, 18083, ready); udp(null, 18084, ready);
                if (!ready.await(10, TimeUnit.SECONDS)) throw new IllegalStateException("Servers not ready");
                Files.write(new File(getFilesDir(), "ready").toPath(), local.getHostAddress().getBytes(StandardCharsets.UTF_8));
                Log.i(TAG, "ready uid=" + android.os.Process.myUid() + " address=" + local);
            } catch (Exception e) { Log.e(TAG, "startup failed", e); }
        }, "probe-start").start();
    }
    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if ("outgoing".equals(intent.getStringExtra("command"))) new Thread(() -> {
            try (DatagramSocket socket = new DatagramSocket()) {
                byte[] data = "new-outgoing".getBytes(StandardCharsets.UTF_8);
                socket.send(new DatagramPacket(data, data.length, InetAddress.getByName("10.0.2.2"), 19999));
            } catch (Exception e) { Log.e(TAG, "outgoing", e); }
        }, "probe-outgoing").start();
    }
    private static void tcp(InetAddress local, int port, CountDownLatch ready) {
        new Thread(() -> {
            try (ServerSocket server = new ServerSocket()) {
                server.bind(new InetSocketAddress(local, port)); ready.countDown();
                while (true) try (Socket socket = server.accept()) {
                    socket.setSoTimeout(3000);
                    byte[] data = new byte[128]; int size = socket.getInputStream().read(data);
                    if (size > 0) socket.getOutputStream().write(data, 0, size);
                } catch (Exception e) { Log.w(TAG, "TCP exchange " + port, e); }
            } catch (Exception e) { Log.e(TAG, "TCP server " + port, e); }
        }, "probe-tcp-" + port).start();
    }
    private static void udp(InetAddress local, int port, CountDownLatch ready) {
        new Thread(() -> {
            try (DatagramSocket socket = new DatagramSocket(new InetSocketAddress(local, port))) {
                ready.countDown();
                while (true) {
                    DatagramPacket packet = new DatagramPacket(new byte[128], 128);
                    socket.receive(packet);
                    try { socket.send(packet); } catch (Exception e) { Log.w(TAG, "UDP exchange " + port, e); }
                }
            } catch (Exception e) { Log.e(TAG, "UDP server " + port, e); }
        }, "probe-udp-" + port).start();
    }
}
