package com.dere3046.forgemint

import android.os.IBinder
import android.os.Parcel
import android.system.keystore2.IKeystoreService
import android.system.keystore2.KeyDescriptor
import android.system.keystore2.KeyEntryResponse

class Keystore2Interceptor : BinderInterceptor() {

    override fun onPreTransact(
        txId: Long,
        target: IBinder,
        code: Int,
        flags: Int,
        callingUid: Int,
        callingPid: Int,
        data: Parcel
    ): TransactionResult {
        if (code == GET_KEY_ENTRY_TRANSACTION) {
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

        if (code == GET_KEY_ENTRY_TRANSACTION) {
            return handlePostGetKeyEntry(callingUid, reply)
        }

        return TransactionResult.Skip
    }

    private fun handlePostGetKeyEntry(uid: Int, reply: Parcel): TransactionResult {
        if (!ConfigManager.shouldPatch(uid)) return TransactionResult.Skip

        try {
            reply.readException()
            val response = reply.readTypedObject(KeyEntryResponse.CREATOR) ?: return TransactionResult.Skip

            val originalChain = CertificateHelper.getCertificateChain(response.metadata) ?: return TransactionResult.Skip
            if (originalChain.size <= 1) return TransactionResult.Skip

            AttestationPatcher.patchCertificateChain(originalChain, uid)

            Logger.i("Keystore2: RootOfTrust patch applied for UID=$uid")

            val override = Parcel.obtain()
            override.writeNoException()
            override.writeTypedObject(response, 0)
            return TransactionResult.OverrideReply(override)
        } catch (e: Exception) {
            Logger.e("handlePostGetKeyEntry failed", e)
            return TransactionResult.Skip
        }
    }

    companion object {
        val GET_KEY_ENTRY_TRANSACTION: Int by lazy {
            resolveCode("TRANSACTION_getKeyEntry")
        }

        private fun resolveCode(name: String): Int {
            return try {
                IKeystoreService.Stub::class.java
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