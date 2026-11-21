package org.freetunnel.android;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.ImageFormat;
import android.graphics.Matrix;
import android.graphics.RectF;
import android.graphics.SurfaceTexture;
import android.hardware.camera2.*;
import android.media.Image;
import android.media.ImageReader;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Size;
import android.view.*;
import android.widget.*;
import java.util.Arrays;

/** Permission is requested only after choosing Scan QR code. All camera resources stop on pause. */
public final class QrScannerActivity extends Activity implements TextureView.SurfaceTextureListener {
    static final String RESULT_LINK = "link";
    private TextureView preview;
    private TextView hint;
    private HandlerThread thread;
    private Handler cameraThread;
    private CameraDevice camera;
    private CameraCaptureSession session;
    private ImageReader reader;
    private final Object frameLock = new Object();
    private Surface previewSurface;
    private volatile boolean active;
    private boolean opening;
    private Size size;
    private int sensorOrientation;
    private long lastFrame;
    private volatile int generation;

    @Override protected void attachBaseContext(Context base) { super.attachBaseContext(AppLocale.wrap(base)); }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xff101010);
        root.setFitsSystemWindows(true);
        TextView back = new TextView(this);
        back.setText("‹  " + getString(R.string.qr_scan)); back.setTextColor(0xffffffff); back.setTextSize(20);
        int padding = (int)(20 * getResources().getDisplayMetrics().density);
        back.setPadding(padding, padding, padding, padding); back.setOnClickListener(v -> finish());
        back.setFocusable(true); back.setContentDescription(getString(R.string.text_back));
        root.addView(back);
        preview = new TextureView(this); preview.setSurfaceTextureListener(this);
        root.addView(preview, new LinearLayout.LayoutParams(-1, 0, 1));
        hint = new TextView(this); hint.setText(R.string.qr_hint); hint.setTextColor(0xffffffff);
        hint.setGravity(Gravity.CENTER); hint.setPadding(padding, padding, padding, padding);
        root.addView(hint); setContentView(root);
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[]{Manifest.permission.CAMERA}, 1);
    }
    @Override protected void onResume() {
        super.onResume(); active = true;
        thread = new HandlerThread("freetunnel-qr"); thread.start(); cameraThread = new Handler(thread.getLooper());
        if (preview.isAvailable()) openCamera();
    }
    @Override protected void onPause() {
        active = false; generation++;
        // Closing the camera first stops the producer before its surfaces disappear.
        if (session != null) { session.close(); session = null; }
        if (camera != null) { camera.close(); camera = null; }
        synchronized (frameLock) { if (reader != null) { reader.close(); reader = null; } }
        if (previewSurface != null) { previewSurface.release(); previewSurface = null; }
        if (thread != null) { thread.quitSafely(); thread = null; }
        opening = false;
        super.onPause();
    }
    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        if (request == 1 && results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) openCamera();
        else unavailable();
    }
    private void unavailable() { runOnUiThread(() -> { if (!isFinishing()) hint.setText(R.string.qr_unavailable); }); }
    @SuppressWarnings("deprecation")
    private void openCamera() {
        if (!active || opening || !preview.isAvailable() || checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) return;
        try {
            CameraManager manager = getSystemService(CameraManager.class);
            String chosen = null;
            for (String id : manager.getCameraIdList()) {
                CameraCharacteristics c = manager.getCameraCharacteristics(id);
                if (Integer.valueOf(CameraCharacteristics.LENS_FACING_BACK).equals(c.get(CameraCharacteristics.LENS_FACING))) { chosen = id; break; }
            }
            if (chosen == null) { unavailable(); return; }
            CameraCharacteristics c = manager.getCameraCharacteristics(chosen);
            android.hardware.camera2.params.StreamConfigurationMap map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
            if (map == null) { unavailable(); return; }
            Size[] sizes = map.getOutputSizes(ImageFormat.YUV_420_888);
            size = Arrays.stream(sizes).filter(s -> s.getWidth() <= 1280 && s.getHeight() <= 1280)
                    .filter(s -> Arrays.asList(map.getOutputSizes(SurfaceTexture.class)).contains(s))
                    .max(java.util.Comparator.comparingInt(s -> s.getWidth() * s.getHeight())).orElse(null);
            if (size == null) { unavailable(); return; }
            Integer orientation = c.get(CameraCharacteristics.SENSOR_ORIENTATION);
            sensorOrientation = orientation == null ? 90 : orientation;
            preview.getSurfaceTexture().setDefaultBufferSize(size.getWidth(), size.getHeight());
            transform();
            previewSurface = new Surface(preview.getSurfaceTexture());
            reader = ImageReader.newInstance(size.getWidth(), size.getHeight(), ImageFormat.YUV_420_888, 2);
            reader.setOnImageAvailableListener(this::decode, cameraThread);
            opening = true;
            final int token = generation;
            manager.openCamera(chosen, new CameraDevice.StateCallback() {
                @Override public void onOpened(CameraDevice device) {
                    if (!active || token != generation) { device.close(); return; }
                    camera = device;
                    try {
                        final Surface output = reader.getSurface();
                        device.createCaptureSession(Arrays.asList(previewSurface, output), new CameraCaptureSession.StateCallback() {
                            @Override public void onConfigured(CameraCaptureSession configured) {
                                if (!active || token != generation || camera != device) { configured.close(); return; }
                                session = configured;
                                try {
                                    CaptureRequest.Builder request = device.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
                                    request.addTarget(previewSurface); request.addTarget(output);
                                    request.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE);
                                    configured.setRepeatingRequest(request.build(), null, cameraThread);
                                } catch (CameraAccessException | IllegalStateException error) { unavailable(); }
                            }
                            @Override public void onConfigureFailed(CameraCaptureSession failed) { unavailable(); }
                        }, new Handler(getMainLooper()));
                    } catch (CameraAccessException | IllegalStateException error) { unavailable(); }
                }
                @Override public void onDisconnected(CameraDevice device) { device.close(); unavailable(); }
                @Override public void onError(CameraDevice device, int error) { device.close(); unavailable(); }
            }, new Handler(getMainLooper()));
        } catch (CameraAccessException | IllegalArgumentException | SecurityException error) { unavailable(); }
    }
    private void decode(ImageReader source) {
        synchronized (frameLock) { decodeFrame(source); }
    }
    private void decodeFrame(ImageReader source) {
        final int token = generation;
        try (Image image = source.acquireLatestImage()) {
            long now = android.os.SystemClock.elapsedRealtime();
            if (image == null || !active || now - lastFrame < 180) return;
            lastFrame = now;
            Image.Plane plane = image.getPlanes()[0];
            String link = QrDecoder.decode(plane.getBuffer(), image.getWidth(), image.getHeight(), plane.getRowStride(), plane.getPixelStride());
            if (link != null) runOnUiThread(() -> {
                if (!active || token != generation || isFinishing()) return;
                active = false;
                setResult(RESULT_OK, new Intent().putExtra(RESULT_LINK, link)); finish();
            });
        } catch (IllegalStateException ignored) { /* Camera closed while processing a frame. */ }
    }
    @SuppressWarnings("deprecation")
    private void transform() {
        if (size == null || preview.getWidth() == 0 || preview.getHeight() == 0) return;
        int degrees = (sensorOrientation - getWindowManager().getDefaultDisplay().getRotation() * 90 + 360) % 360;
        float w = preview.getWidth(), h = preview.getHeight();
        Matrix matrix = new Matrix();
        RectF view = new RectF(0, 0, w, h);
        RectF buffer = new RectF(0, 0, degrees % 180 == 0 ? size.getWidth() : size.getHeight(), degrees % 180 == 0 ? size.getHeight() : size.getWidth());
        buffer.offset(w / 2 - buffer.centerX(), h / 2 - buffer.centerY());
        matrix.setRectToRect(view, buffer, Matrix.ScaleToFit.FILL);
        float scale = Math.max(w / buffer.width(), h / buffer.height());
        matrix.postScale(scale, scale, w / 2, h / 2);
        matrix.postRotate(degrees, w / 2, h / 2);
        preview.setTransform(matrix);
    }
    @Override public void onSurfaceTextureAvailable(SurfaceTexture texture, int width, int height) { openCamera(); }
    @Override public void onSurfaceTextureSizeChanged(SurfaceTexture texture, int width, int height) { transform(); }
    @Override public boolean onSurfaceTextureDestroyed(SurfaceTexture texture) { return true; }
    @Override public void onSurfaceTextureUpdated(SurfaceTexture texture) { }
}
