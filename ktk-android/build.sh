#!/bin/sh
# KTK for Android 4.1+ without Gradle: aapt2 -> javac -> d8 -> zipalign -> apksigner.
#   ./build.sh            build/ktk.apk
#   ./build.sh install    build and install on the device/emulator that adb sees
#   ./build.sh clean
set -eu
cd "$(dirname "$0")"

SDK=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-/opt/android-sdk}}
BT=${BUILD_TOOLS:-$(ls -d "$SDK"/build-tools/* 2>/dev/null | sort -V | tail -n 1)}
PLATFORM=${PLATFORM:-$SDK/platforms/android-16/android.jar}
MIN_SDK=16
TARGET_SDK=28
VERSION_CODE=1
VERSION_NAME=1.0.0
KEYSTORE=${KEYSTORE:-$HOME/.android/debug.keystore}
OUT=build

if [ "${1:-}" = clean ]; then
    rm -rf "$OUT"
    exit 0
fi
[ -x "$BT/aapt2" ] || { echo "build-tools не найдены в $SDK/build-tools (yay -S android-sdk-build-tools)" >&2; exit 1; }
[ -f "$PLATFORM" ] || { echo "нет $PLATFORM (yay -S android-platform-16)" >&2; exit 1; }
command -v javac >/dev/null || { echo "нет javac (pacman -S jdk-openjdk)" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/gen" "$OUT/classes" "$OUT/dex"

echo "== resources"
"$BT/aapt2" compile --dir res -o "$OUT/res.zip"
"$BT/aapt2" link -I "$PLATFORM" --manifest AndroidManifest.xml \
    --min-sdk-version $MIN_SDK --target-sdk-version $TARGET_SDK \
    --version-code $VERSION_CODE --version-name $VERSION_NAME \
    --java "$OUT/gen" -o "$OUT/unaligned.apk" "$OUT/res.zip"

echo "== java"
# Java 8 syntax without lambdas: they would need java.lang.invoke, which android-16 lacks.
javac -source 8 -target 8 -Xlint:-options -Xlint:unchecked -encoding UTF-8 \
    -bootclasspath "$PLATFORM" -d "$OUT/classes" \
    $(find "$OUT/gen" src -name '*.java')

echo "== dex"
"$BT/d8" --release --min-api $MIN_SDK --lib "$PLATFORM" --output "$OUT/dex" \
    $(find "$OUT/classes" -name '*.class')
(cd "$OUT/dex" && "$BT/aapt" add -v ../unaligned.apk classes.dex >/dev/null)

echo "== sign"
if [ ! -f "$KEYSTORE" ]; then
    mkdir -p "$(dirname "$KEYSTORE")"
    keytool -genkeypair -keystore "$KEYSTORE" -storepass android -keypass android \
        -alias androiddebugkey -keyalg RSA -keysize 2048 -sigalg SHA256withRSA -validity 10000 \
        -dname "CN=Android Debug,O=Android,C=US" >/dev/null
fi
"$BT/zipalign" -f 4 "$OUT/unaligned.apk" "$OUT/aligned.apk"
# --min-sdk-version 16 keeps a v1 (JAR) signature with SHA-1 digests: Android before 7.0 checks only that.
"$BT/apksigner" -J-enable-native-access=ALL-UNNAMED sign --ks "$KEYSTORE" --ks-pass pass:${KEYSTORE_PASS:-android} \
    --min-sdk-version $MIN_SDK --out "$OUT/ktk.apk" "$OUT/aligned.apk"
"$BT/apksigner" -J-enable-native-access=ALL-UNNAMED verify --min-sdk-version $MIN_SDK "$OUT/ktk.apk"
rm -f "$OUT/unaligned.apk" "$OUT/aligned.apk" "$OUT/ktk.apk.idsig"
ls -l "$OUT/ktk.apk"

if [ "${1:-}" = install ]; then
    adb install -r "$OUT/ktk.apk"
    adb shell am start -n ru.ktkschedule.android/.WeekActivity
fi
