#!/bin/bash
# smoke.sh <kit> <graalvm-home (the build host's GraalVM CE, of the kit's release)> <out-dir>
# Builds the smoke test into a native image for the kit's target and links it: <out-dir>/smoke
set -euo pipefail
kit=$(cd "$1" && pwd); graal=$(cd "$2" && pwd); out=$3
smoke=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"; out=$(cd "$out" && pwd)
prop() { sed -n "s/^$1=//p" "$kit/graalvm-mobile.properties"; }
target=$(prop target)
lucene=${LUCENE_VERSION:-10.5.2}
for artifact in lucene-core lucene-analysis-common; do
  [ -f "$out/$artifact.jar" ] || curl -sSfL -o "$out/$artifact.jar" "https://repo1.maven.org/maven2/org/apache/lucene/$artifact/$lucene/$artifact-$lucene.jar"
done
cp="$out/lucene-core.jar:$out/lucene-analysis-common.jar"
"$graal/bin/javac" -d "$out/classes" -cp "$cp" "$smoke/Smoke.java"
# What it reaches by reflection, traced on the build host's JVM
"$graal/bin/java" -agentlib:native-image-agent=config-output-dir="$out/config" -cp "$out/classes:$cp" Smoke "$out" >/dev/null

extra=()
if [[ $target == android-* ]]; then
  extra=(-H:CCompilerPath="$kit/bin/android-cc" -EANDROID_NDK_HOME)
  [ -n "${ANDROID_SERIAL:-}" ] && extra+=(-EANDROID_SERIAL)
fi
# shellcheck disable=SC2046
"$graal/bin/native-image" -cp "$out/classes:$cp" -H:ConfigurationFileDirectories="$out/config" \
  -Dsvm.platform="$(prop native-image.platform)" $(prop native-image.flags) ${extra[@]+"${extra[@]}"} \
  -H:+UnlockExperimentalVMOptions -H:+SharedLibrary -H:+ExitAfterRelocatableImageWrite -H:TempDirectory="$out/tmp" \
  -o "$out/smoke"
object=$(find "$out/tmp" -name smoke.o | head -1)

case $target in
  ios-simulator) cc=(xcrun --sdk iphonesimulator clang -target arm64-apple-ios15.0-simulator) ;;
  ios-device) cc=(xcrun --sdk iphoneos clang -target arm64-apple-ios15.0) ;;
  android-aarch64)
    # Static: runs on any arm64 Linux kernel (and through qemu-user), as well as on Android
    clang=("$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/bin/clang)
    cc=("${clang[0]}" --target=aarch64-linux-android29 -static) ;;
esac
flags=$(prop link.flags)
# No static liblog in the NDK: the static executable goes without
[[ $target == android-* ]] && flags=${flags//-llog/}
# shellcheck disable=SC2086
"${cc[@]}" -I"$out" -o "$out/smoke" "$smoke/main.c" "$object" -L"$kit/lib" $flags
echo "$out/smoke"
