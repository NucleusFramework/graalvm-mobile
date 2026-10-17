#!/bin/bash
# build.sh <target> <graalvm-home> <out-dir> <asset>
#
# What an app needs, besides GraalVM CE itself on the build host, to build a native image for <target> and link it:
# GraalVM's static libraries (Substrate VM's and the JDK's) for that target, from the GraalVM CE of the same release
# (<graalvm-home>, the one for the target's architecture and kernel: macOS aarch64 for iOS, Linux aarch64 for Android).
#   ios-simulator, ios-device: the macOS aarch64 libraries labeled for iOS (same ABI, same SDK headers; the objects
#                              only carry their platform in their build version)
#   android-aarch64,           the Linux (glibc) libraries of the architecture, with libglibc_compat.a for bionic, and
#   android-x64:               android-cc, native-image's C compiler for Android; for x64, graalvm-mobile-platform.jar
#                              (GraalVM declares no Android x64 platform)
# Writes <out-dir>/<asset>.tar.gz and its .sha256.
set -euo pipefail
target=$1; graal=$(cd "$2" && pwd); out=$3; asset=$4
recipe=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"; out=$(cd "$out" && pwd)
kit=$(mktemp -d)/$asset
mkdir -p "$kit/lib"

classpath=""
# The JDK's libraries an image may link (the desktop's AWT, sound... aren't)
jdk_libs="java nio net zip extnet jimage verify management management_ext prefs syslookup"

relabel() { # <platform> <in.a> <out.a>
  local work; work=$(mktemp -d)
  (cd "$work" && ar x "$2" && python3 -I "$recipe/ios/machoplat.py" "$1" ./*.o && libtool -static -o "$3" ./*.o 2>/dev/null)
}

case $target in
  ios-simulator|ios-device)
    [ "$target" = ios-simulator ] && platform=7 || platform=2
    for lib in "$graal"/lib/svm/clibraries/darwin-aarch64/*.a; do relabel $platform "$lib" "$kit/lib/$(basename "$lib")"; done
    for name in $jdk_libs; do relabel $platform "$graal/lib/static/darwin-aarch64/lib$name.a" "$kit/lib/lib$name.a"; done
    platformClass=org.graalvm.nativeimage.Platform\$IOS_AARCH64
    flags='-Dsvm.targetName=iOS -Dsvm.targetArch=arm64 -H:PageSize=16384'
    link='-ljvm -llibchelper -ldarwin -ljava -lnio -lnet -lzip -lextnet -ljimage -lverify -lmanagement_ext -lmanagement -lprefs -framework Foundation -framework CoreFoundation -lz -lc++'
    ;;
  android-aarch64|android-x64)
    if [ "$target" = android-aarch64 ]; then
      arch=aarch64; triple=aarch64-linux-android; platformClass=org.graalvm.nativeimage.Platform\$ANDROID_AARCH64; march=armv8-a
    else
      arch=amd64; triple=x86_64-linux-android; platformClass=graalvm.mobile.ANDROID_AMD64; march=x86-64-v2
    fi
    cp "$graal"/lib/svm/clibraries/linux-$arch/glibc/*.a "$kit/lib/"
    for name in $jdk_libs; do cp "$graal/lib/static/linux-$arch/glibc/lib$name.a" "$kit/lib/"; done
    ndk=${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME}
    cc=("$ndk"/toolchains/llvm/prebuilt/*/bin/clang)
    work=$(mktemp -d)
    "${cc[0]}" --target=${triple}29 -O2 -fPIC -c -o "$work/glibc_compat.o" "$recipe/android/glibc_compat.c"
    "$ndk"/toolchains/llvm/prebuilt/*/bin/llvm-ar rcs "$kit/lib/libglibc_compat.a" "$work/glibc_compat.o"
    mkdir -p "$kit/bin"
    sed "s/@TRIPLE@/$triple/" "$recipe/android/android-cc" > "$kit/bin/android-cc"; chmod +x "$kit/bin/android-cc"
    if [ "$arch" = amd64 ]; then
      "$graal/bin/javac" -d "$work/platform" "$recipe/android/platform/graalvm/mobile/ANDROID_AMD64.java"
      "$graal/bin/jar" --create --file "$kit/lib/graalvm-mobile-platform.jar" -C "$work/platform" .
      classpath=lib/graalvm-mobile-platform.jar
    fi
    # No foreign function ABI for Android in Substrate VM: the FFM API (unused on Android, where Lucene reads through NIO)
    # is left out
    flags="-Dsvm.targetName=Android -Dsvm.targetArch=$arch --libc=bionic -march=$march -H:-CheckToolchain -H:-ForeignAPISupport"
    link='-ljvm -llibchelper -lsvm_container -ljava -lnio -lnet -lzip -lextnet -ljimage -lverify -lmanagement_ext -lmanagement -lprefs -lglibc_compat -lz -llog -ldl -lm -lc++_static -lc++abi'
    ;;
  *) echo "Unknown target $target" >&2; exit 1 ;;
esac
mkdir -p "$kit/include"; cp "$graal"/lib/svm/clibraries/include/*.h "$kit/include/"

release=$(sed -n 's/^GRAALVM_VERSION="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' "$graal/release" | head -1)
java=$(sed -n 's/^JAVA_VERSION="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' "$graal/release" | head -1)
cat > "$kit/graalvm-mobile.properties" <<PROPS
# The GraalVM CE these libraries come from (its graalvm/graalvm-ce-builds release): native-image must be that release's
graalvm.tag=${GRAALVM_TAG:-}
graalvm.version=$release
java.version=$java
target=$target
# native-image's options for this target (with -H:+SharedLibrary -H:+ExitAfterRelocatableImageWrite: a relocatable object)
native-image.platform=$platformClass
native-image.flags=$flags
# Added to native-image's class path (relative to the kit)
native-image.classpath=$classpath
# The object's link, with -L<kit>/lib
link.flags=$link
PROPS
tar -C "$kit" -czf "$out/$asset.tar.gz" .
(cd "$out" && shasum -a 256 "$asset.tar.gz" > "$asset.tar.gz.sha256")
echo "$out/$asset.tar.gz"
