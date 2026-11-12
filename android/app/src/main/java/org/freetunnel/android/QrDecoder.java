package org.freetunnel.android;

import com.google.zxing.BinaryBitmap;
import com.google.zxing.PlanarYUVLuminanceSource;
import com.google.zxing.ReaderException;
import com.google.zxing.common.HybridBinarizer;
import com.google.zxing.qrcode.QRCodeReader;
import java.nio.ByteBuffer;

/** Reads only the luminance plane, including cameras with padded rows. No image leaves the device. */
final class QrDecoder {
    static String decode(ByteBuffer buffer, int width, int height, int rowStride, int pixelStride) {
        if (width < 1 || height < 1 || width > 1920 || height > 1920 || pixelStride < 1
                || rowStride < width || (long)(height - 1) * rowStride + (long)(width - 1) * pixelStride >= buffer.remaining()) return null;
        byte[] pixels = new byte[width * height];
        int start = buffer.position();
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++)
            pixels[y * width + x] = buffer.get(start + y * rowStride + x * pixelStride);
        try {
            PlanarYUVLuminanceSource source = new PlanarYUVLuminanceSource(pixels, width, height, 0, 0, width, height, false);
            String text = new QRCodeReader().decode(new BinaryBitmap(new HybridBinarizer(source))).getText();
            return text.length() <= ImportBatch.MAX_BYTES && DeepLinkImport.looksLikeLink(text) ? text : null;
        } catch (ReaderException invalid) { return null; }
    }
}
