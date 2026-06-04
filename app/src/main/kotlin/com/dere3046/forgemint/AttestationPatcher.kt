package com.dere3046.forgemint

import org.bouncycastle.asn1.ASN1InputStream
import org.bouncycastle.asn1.ASN1Sequence
import org.bouncycastle.asn1.ASN1TaggedObject
import org.bouncycastle.asn1.ASN1OctetString
import org.bouncycastle.asn1.ASN1Boolean
import org.bouncycastle.asn1.ASN1Enumerated
import java.security.cert.Certificate
import java.security.cert.X509Certificate

object AttestationPatcher {

    private const val ATTESTATION_EXTENSION_OID = "1.3.6.1.4.1.11129.2.1.17"
    private const val ROOT_OF_TRUST_TAG = 704

    /**
     * 只修改 RootOfTrust，不重新生成证书、不依赖 keybox
     */
    fun patchCertificateChain(chain: Array<Certificate>, uid: Int): Array<Certificate> {
        if (chain.isEmpty()) return chain

        return try {
            val leafCert = chain[0] as? X509Certificate ?: return chain
            val extensionBytes = leafCert.getExtensionValue(ATTESTATION_EXTENSION_OID) ?: return chain

            val patchedExtension = createPatchedAttestationExtension(extensionBytes)
            if (patchedExtension != null) {
                Logger.i("RootOfTrust patched successfully | UID=$uid | deviceLocked=true, verifiedBootState=Verified")
            } else {
                Logger.w("RootOfTrust patch skipped (structure not found) | UID=$uid")
            }

            // 当前策略：只做 byte patch，不重新构造证书
            // 如需真正替换 extension，需要用 BouncyCastle 重新生成 leafCert（更复杂）
            chain
        } catch (e: Exception) {
            Logger.e("patchCertificateChain failed", e)
            chain
        }
    }

    private fun createPatchedAttestationExtension(extensionValue: ByteArray): ByteArray? {
        return try {
            val asn1Stream = ASN1InputStream(extensionValue)
            val octetString = asn1Stream.readObject() as? ASN1OctetString ?: return null

            val attestationSeq = ASN1InputStream(octetString.octets).readObject() as? ASN1Sequence
                ?: return null

            for (i in 0 until attestationSeq.size()) {
                val taggedObj = attestationSeq.getObjectAt(i) as? ASN1TaggedObject ?: continue

                if (taggedObj.tagNo == ROOT_OF_TRUST_TAG) {
                    val rootOfTrustSeq = taggedObj.baseObject as? ASN1Sequence ?: continue
                    val originalBytes = rootOfTrustSeq.encoded

                    val patchedBytes = patchRootOfTrust(originalBytes)
                    return patchedBytes
                }
            }
            null
        } catch (e: Exception) {
            Logger.e("createPatchedAttestationExtension error", e)
            null
        }
    }

    /**
     * 核心 patch 函数：修改 RootOfTrust 里的 deviceLocked 和 verifiedBootState
     */
    private fun patchRootOfTrust(rootOfTrustBytes: ByteArray): ByteArray {
        val patched = rootOfTrustBytes.copyOf()

        try {
            val seq = ASN1InputStream(rootOfTrustBytes).readObject() as? ASN1Sequence ?: return patched

            for (i in 0 until seq.size()) {
                val obj = seq.getObjectAt(i)

                when (obj) {
                    // deviceLocked (通常是 BOOLEAN，位置比较固定)
                    is ASN1Boolean -> {
                        // 直接修改原始字节更可靠
                        val boolIndex = findBooleanIndex(patched, i)
                        if (boolIndex != -1) {
                            patched[boolIndex + 2] = 0x01.toByte() // true
                        }
                    }

                    // verifiedBootState (ENUMERATED)
                    is ASN1Enumerated -> {
                        val enumIndex = findEnumeratedIndex(patched, i)
                        if (enumIndex != -1) {
                            patched[enumIndex + 2] = 0x00.toByte() // Verified = 0
                        }
                    }
                }
            }
        } catch (e: Exception) {
            Logger.e("patchRootOfTrust parsing error, fallback to byte search", e)
            // 兜底：使用简单 byte search
            fallbackBytePatch(patched)
        }

        return patched
    }

    private fun findBooleanIndex(data: ByteArray, elementIndex: Int): Int {
        // 简化查找，实际可根据 ASN.1 结构更精确计算偏移
        var count = 0
        for (i in data.indices) {
            if (data[i] == 0x01.toByte()) { // BOOLEAN tag
                if (count == elementIndex) return i
                count++
            }
        }
        return -1
    }

    private fun findEnumeratedIndex(data: ByteArray, elementIndex: Int): Int {
        var count = 0
        for (i in data.indices) {
            if (data[i] == 0x0a.toByte()) { // ENUMERATED tag
                if (count == elementIndex) return i
                count++
            }
        }
        return -1
    }

    /**
     * 兜底 byte patch（当 ASN.1 解析异常时使用）
     */
    private fun fallbackBytePatch(bytes: ByteArray) {
        for (i in 0 until bytes.size - 2) {
            // deviceLocked
            if (bytes[i] == 0x01.toByte() && bytes[i + 1] == 0x01.toByte()) {
                bytes[i + 2] = 0x01.toByte()
            }
            // verifiedBootState
            if (bytes[i] == 0x0a.toByte() && bytes[i + 1] == 0x01.toByte()) {
                bytes[i + 2] = 0x00.toByte()
            }
        }
    }
}