package android.hardware.security.keymint;

public @interface BlockMode {
    int ECB = 1;
    int CBC = 2;
    int CTR = 3;
    int GCM = 32;
}
