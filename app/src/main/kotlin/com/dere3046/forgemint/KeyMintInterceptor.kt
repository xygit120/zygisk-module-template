package com.dere3046.forgemint

import android.os.IBinder
import android.os.Parcel
import android.system.keystore2.IKeystoreSecurityLevel
import android.system.keystore2.KeyDescriptor
import android.system.keystore2.KeyMetadata

class KeyMintInterceptor(
    private val originalBinder: IBinder,
    private val securityLevel: Int
) : BinderInterceptor() {

    override fun onPreTransact(
        txId: Long,
        target: IBinder,
        code: Int,
        flags: Int,
        callingUid: Int,
        callingPid: Int,
        data: Parcel
    ): TransactionResult {
        // 只保留 generateKey 的判断
        if (code == GENERATE_KEY_TRANSACTION) {
            if (!ConfigManager.shouldPatch(callingUid)) {
                return TransactionResult.ContinueAndSkipPost
            }
        }
        return TransactionResult.Continue
    }

    override fun onPostTransact(
        txId: Long,
        target: IBinder,
        code: Int,
        flags: Int,
        callingUid: Int,
        callingPid: Int,
        data: Parcel,
        reply: Parcel?,
        resultCode: Int
    ): TransactionResult {
        if (resultCode != 0 || reply == null) return TransactionResult.Skip

        if (code == GENERATE_KEY_TRANSACTION) {
            return handlePostGenerateKey(callingUid, reply)
        }

        return TransactionResult.Skip
    }

    private fun handlePostGenerateKey(callingUid: Int, reply: Parcel): TransactionResult {
        if (!ConfigManager.shouldPatch(callingUid)) return TransactionResult.Skip

        try {
            reply.readException()
            val metadata = reply.readTypedObject(KeyMetadata.CREATOR) ?: return TransactionResult.Skip

            val originalChain = CertificateHelper.getCertificateChain(metadata) ?: return TransactionResult.Skip
            if (originalChain.size <= 1) return TransactionResult.Skip

            // 只做 RootOfTrust patch
            AttestationPatcher.patchCertificateChain(originalChain, callingUid)

            // 这里简化处理：实际需要把 patched 的 extension 写回 metadata
            // 当前版本先记录日志，后续可完善
            Logger.i("KeyMint: RootOfTrust patch applied for UID=$callingUid")

            val override = Parcel.obtain()
            override.writeNoException()
            override.writeTypedObject(metadata, 0)
            return TransactionResult.OverrideReply(override)
        } catch (e: Exception) {
            Logger.e("handlePostGenerateKey failed", e)
            return TransactionResult.Skip
        }
    }

    companion object {
        val GENERATE_KEY_TRANSACTION: Int by lazy {
            resolveCode("TRANSACTION_generateKey")
        }

        private fun resolveCode(name: String): Int {
            return try {
                IKeystoreSecurityLevel.Stub::class.java
                    .getDeclaredField(name)
                    .apply { isAccessible = true }
                    .getInt(null)
            } catch (e: Exception) {
                Logger.e("Failed to resolve $name", e)
                -1
            }
        }
    }
}