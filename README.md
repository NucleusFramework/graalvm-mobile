# graalvm-mobile

GraalVM CE's native images on iOS and Android: this branch (`mobile-runtime`) is a recipe, not GraalVM's sources. Its
CI follows GraalVM CE's releases ([graalvm/graalvm-ce-builds](https://github.com/graalvm/graalvm-ce-builds)) and
publishes, for each release, a kit per target: what an app needs, besides that release's GraalVM CE on the build host,
to build a native image for the target and link it into the app.

GraalVM itself still knows these targets (`Platform.IOS_AARCH64`, `Platform.ANDROID_AARCH64`, `--libc=bionic`): it
cross-compiles a relocatable object (`-H:+SharedLibrary -H:+ExitAfterRelocatableImageWrite`), which the app's build links
with the target's static libraries, the ones GraalVM CE ships only for its own platforms. That is what Gluon did up to
GraalVM 23; the kits do it for the current GraalVM CE.

| Target | Asset | Build host | Libraries |
|---|---|---|---|
| iOS simulator, arm64 | `ios-simulator-aarch64` | macOS arm64 | GraalVM CE macOS arm64's, labeled for the iOS simulator |
| iOS device, arm64 | `ios-device-aarch64` | macOS arm64 | GraalVM CE macOS arm64's, labeled for iOS |
| Android, arm64 | `android-aarch64` | Linux (x64 or arm64) | GraalVM CE Linux arm64's (glibc), with `libglibc_compat.a` for bionic |
| Android, x86_64 (emulators) | `android-x64` | Linux (x64 or arm64) | GraalVM CE Linux x64's (glibc), with `libglibc_compat.a` for bionic |

- iOS and macOS share the arm64 ABI and the SDK's headers: a macOS object only differs by the platform of its build
  version, which `ios/machoplat.py` rewrites.
- Android: native-image compiles its host JVM's Java classes, so an Android image is built on Linux (a macOS JDK's
  `sun.nio.ch` isn't Linux's). Its C header queries are compiled with the NDK (`bin/android-cc`, native-image's
  `-H:CCompilerPath`) into static programs run on the host (through qemu-user on x64), or, with `ANDROID_SERIAL` set,
  on that device through adb. GraalVM declares no Android x86_64 platform: the kit brings one
  (`graalvm.mobile.ANDROID_AMD64`, in `lib/graalvm-mobile-platform.jar`, for native-image's class path).

## Releases

`graalvm-mobile-<GraalVM version>b<Java build>.<revision>`, e.g. `graalvm-mobile-25.4.4.1.1b1.1`, its assets
`graalvm-mobile-<version>-<target>-b<build>.<revision>.tar.gz` with their `.sha256`. A kit holds:

- `lib/`: the static libraries (Substrate VM's, the JDK's);
- `include/`: Substrate VM's C headers;
- `graalvm-mobile.properties`: the GraalVM release it is for, native-image's options for the target
  (`native-image.platform`, `native-image.flags`, `native-image.classpath`) and the object's link (`link.flags`, with `-L<kit>/lib`);
- `bin/android-cc` (Android).

## Building and testing

```bash
./build.sh <ios-simulator|ios-device|android-aarch64|android-x64> <GraalVM CE home> <out-dir> <asset>
smoke/smoke.sh <kit> <the build host's GraalVM CE home> <out-dir>   # builds smoke/Smoke.java, links <out-dir>/smoke
```

The smoke test (the JDK's files, threads and time zones, a Lucene index with phrase, point and KNN queries) runs in the
CI in the iOS simulator and, for Android, as a static executable on the Linux runner (through qemu-user for arm64).
