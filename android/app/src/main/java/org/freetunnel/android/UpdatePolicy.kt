package org.freetunnel.android

import org.bouncycastle.crypto.params.Ed25519PublicKeyParameters
import org.bouncycastle.crypto.signers.Ed25519Signer
import java.math.BigInteger
import java.net.URI
import java.nio.charset.StandardCharsets
import java.security.MessageDigest

/** Pure policy shared by discovery, download verification and installation. */
object UpdatePolicy {
    const val REPOSITORY = "dimmmmmmmer/freetunnel"
    private val versionPattern = Regex("[0-9]{1,20}\\.[0-9]{1,20}\\.[0-9]{1,20}")
    private val pinPattern = Regex("[0-9a-f]{64}")

    /**
     * The rule of scripts/check-android-signer.sh: exactly one signer, whose
     * DER certificate has the pinned SHA-256 (64 lowercase hex characters). An
     * empty or malformed pin matches nothing.
     */
    fun signedBy(certificates: List<ByteArray>, pin: String): Boolean =
        pinPattern.matches(pin) && certificates.size == 1 && hex(MessageDigest.getInstance("SHA-256").digest(certificates[0])) == pin

    private fun hex(bytes: ByteArray) = bytes.joinToString("") { (it.toInt() and 0xff).toString(16).padStart(2, '0') }
    data class Candidate(val tag: String, val draft: Boolean, val prerelease: Boolean, val assets: Map<String, String>)
    data class Release(val version: String, val apk: String, val manifest: String, val signature: String) {
        val tag get() = "android-$version"
        val filename get() = "freetunnel-android-$version.apk"
    }
    private fun version(value: String): List<BigInteger>? =
        if (versionPattern.matches(value)) value.split('.').map(::BigInteger) else null
    private fun compare(first: List<BigInteger>, second: List<BigInteger>): Int {
        for (i in 0..2) if (first[i] != second[i]) return first[i].compareTo(second[i])
        return 0
    }
    fun trustedAsset(url: String, tag: String, filename: String): Boolean = runCatching {
        val uri = URI(url)
        uri.scheme == "https" && uri.host == "github.com" && uri.port == -1 && uri.rawUserInfo == null &&
            uri.rawQuery == null && uri.rawFragment == null &&
            uri.rawPath == "/$REPOSITORY/releases/download/$tag/$filename"
    }.getOrDefault(false)

    fun select(candidates: List<Candidate>, installed: String): Release? {
        val current = version(installed) ?: error("Invalid installed version")
        return candidates.mapNotNull { candidate ->
            if (candidate.draft || candidate.prerelease || !candidate.tag.startsWith("android-")) return@mapNotNull null
            val text = candidate.tag.removePrefix("android-")
            val parsed = version(text) ?: return@mapNotNull null
            if (compare(parsed, current) <= 0) return@mapNotNull null
            val names = listOf("freetunnel-android-$text.apk", "SHA256SUMS-android.txt", "SHA256SUMS-android.txt.sig")
            val urls = names.map { name -> candidate.assets[name]?.takeIf { trustedAsset(it, candidate.tag, name) } ?: return@mapNotNull null }
            Release(text, urls[0], urls[1], urls[2])
        }.maxWithOrNull { a, b -> compare(version(a.version)!!, version(b.version)!!) }
    }

    fun verifyManifest(release: Release, bytes: ByteArray, signature: ByteArray, rawPublicKey: ByteArray): ByteArray {
        require(bytes.size in 1..65_536 && signature.size == 64 && rawPublicKey.size == 32) { "Invalid signed manifest" }
        val verifier = Ed25519Signer()
        verifier.init(false, Ed25519PublicKeyParameters(rawPublicKey, 0))
        verifier.update(bytes, 0, bytes.size)
        require(verifier.verifySignature(signature)) { "Invalid update signature" }
        // Strict UTF-8 and version binding are checked on the exact signed bytes.
        val text = StandardCharsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(bytes)).toString()
        val lines = text.lines()
        require(lines.firstOrNull() == "#version=${release.version}") { "Manifest version does not match the release" }
        require(lines.count { it.startsWith("#version=") } == 1) { "Ambiguous manifest version" }
        val entries = lines.drop(1).filter { it.isNotBlank() && !it.startsWith('#') }.map { line ->
            val match = Regex("([0-9a-fA-F]{64}) [ *]([^\\r\\n]+)").matchEntire(line) ?: error("Invalid checksum entry")
            match.groupValues[2] to match.groupValues[1]
        }
        val matching = entries.filter { it.first == release.filename }
        require(matching.size == 1) { "Missing or ambiguous APK checksum" }
        return matching.single().second.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    }

    fun verifyDigest(expected: ByteArray, actual: ByteArray) {
        require(expected.size == 32 && MessageDigest.isEqual(expected, actual)) { "APK checksum mismatch" }
    }
}
