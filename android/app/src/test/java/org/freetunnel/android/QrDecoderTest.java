package org.freetunnel.android;
import org.junit.Test;
import static org.junit.Assert.*;
import java.nio.ByteBuffer;
import com.google.zxing.BarcodeFormat;
import com.google.zxing.qrcode.QRCodeWriter;
import com.google.zxing.common.BitMatrix;
public class QrDecoderTest {
    @Test public void readsPaddedCameraRowsAndRejectsUnrelatedCodes() throws Exception {
        for (String text : new String[]{"tt://?AAEBAQtkZW1vLnNlcnZlcg", "https://trusttunnel.org/qr.html#tt=abc", "https://unrelated.example"}) {
            BitMatrix qr = new QRCodeWriter().encode(text, BarcodeFormat.QR_CODE, 300, 300);
            ByteBuffer plane = ByteBuffer.allocate(320 * 300);
            for (int y = 0; y < 300; y++) for (int x = 0; x < 300; x++) plane.put(y * 320 + x, (byte)(qr.get(x,y) ? 0 : 255));
            assertEquals(text.startsWith("https://unrelated") ? null : text, QrDecoder.decode(plane,300,300,320,1));
        }
        assertNull(QrDecoder.decode(ByteBuffer.allocate(3), 300,300,320,1));
    }
}
