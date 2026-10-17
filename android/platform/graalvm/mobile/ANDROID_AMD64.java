package graalvm.mobile;

import org.graalvm.nativeimage.Platform;

/** Android on x86 64-bit (the emulators): GraalVM only declares ANDROID_AARCH64, native-image takes any leaf platform. */
public final class ANDROID_AMD64 implements Platform.ANDROID, Platform.LINUX_AMD64_BASE {
    public ANDROID_AMD64() {
    }
}
