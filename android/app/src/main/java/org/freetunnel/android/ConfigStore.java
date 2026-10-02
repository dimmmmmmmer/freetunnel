package org.freetunnel.android;

import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;

import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.util.Arrays;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** Encrypts imported TOML, which can contain a server password. */
final class ConfigStore {
    private static final String ALIAS = "freetunnel_configs_v1";
    private static final String FIELD = "configs_encrypted";

    static String read(SharedPreferences prefs) {
        String value = prefs.getString(FIELD, null);
        if (value == null) return prefs.getString("configs", "[]");
        String decrypted = decrypt(value);
        return decrypted == null ? "[]" : decrypted;
    }

    static boolean write(SharedPreferences prefs, String json) {
        String encrypted = encrypt(json);
        return encrypted != null && prefs.edit().putString(FIELD, encrypted)
                .remove("configs").commit();
    }

    static String encrypt(String value) {
        try {
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, key());
            byte[] nonce = cipher.getIV();
            byte[] ciphertext = cipher.doFinal(value.getBytes(StandardCharsets.UTF_8));
            byte[] packet = new byte[nonce.length + ciphertext.length];
            System.arraycopy(nonce, 0, packet, 0, nonce.length);
            System.arraycopy(ciphertext, 0, packet, nonce.length, ciphertext.length);
            return Base64.encodeToString(packet, Base64.NO_WRAP);
        } catch (Exception e) { return null; }
    }

    static String decrypt(String value) {
        try {
            byte[] packet = Base64.decode(value, Base64.NO_WRAP);
            if (packet.length <= 12) return null;
            byte[] nonce = Arrays.copyOfRange(packet, 0, 12);
            byte[] ciphertext = Arrays.copyOfRange(packet, 12, packet.length);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(128, nonce));
            return new String(cipher.doFinal(ciphertext), StandardCharsets.UTF_8);
        } catch (Exception e) { return null; }
    }

    private static SecretKey key() throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore"); store.load(null);
        if (store.containsAlias(ALIAS)) return ((KeyStore.SecretKeyEntry) store.getEntry(ALIAS, null)).getSecretKey();
        KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        generator.init(new KeyGenParameterSpec.Builder(ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256).build());
        return generator.generateKey();
    }
}
