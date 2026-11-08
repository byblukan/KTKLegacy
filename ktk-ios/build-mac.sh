#!/bin/bash
# Сборка KTKLegacy (КТК для iOS 6) на Mac OS X 10.7+ с Xcode 4.5-4.6 (iOS SDK 6.x). Xcode-проект не нужен:
# скрипт сам вызывает clang из Xcode, собирает .app, подписывает его ad-hoc и упаковывает.
#
#   ./build-mac.sh                      собрать build/KTKLegacy.app, build/*.deb и build/KTKLegacy.ipa
#   ./build-mac.sh install root@<ip>    собрать и установить .deb на устройство по SSH
#   ./build-mac.sh clean                удалить build/
#
# Переменные окружения (необязательные):
#   SDK=/путь/к/iPhoneOS6.1.sdk   если SDK не находится сам
#   SIGN_IDENTITY="iPhone Developer: ..."   подписать сертификатом вместо ad-hoc (по умолчанию "-")
set -e
cd "$(dirname "$0")"
ROOT=$(pwd)

NAME=KTKLegacy
ARCH=armv7
MIN_IOS=6.0
BUILD="$ROOT/build"
OBJ="$BUILD/obj"
APP="$BUILD/$NAME.app"

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die() { printf '\033[31mОшибка:\033[0m %s\n' "$*" >&2; exit 1; }

if [ "$1" = clean ]; then
    rm -rf "$BUILD"
    say "build/ удалена"
    exit 0
fi

# --- Xcode, SDK, инструменты -------------------------------------------------------------

DEV=$(xcode-select -print-path 2>/dev/null) || die "Xcode не найден (xcode-select -print-path)."
if [ -z "$SDK" ]; then
    SDK=$(xcrun -sdk iphoneos --show-sdk-path 2>/dev/null || true)
fi
if [ -z "$SDK" ] || [ ! -d "$SDK" ]; then
    SDK=$(ls -d "$DEV"/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS*.sdk 2>/dev/null | tail -1)
fi
[ -n "$SDK" ] && [ -d "$SDK" ] || die "iPhoneOS SDK не найден. Укажите: SDK=/путь/к/iPhoneOS6.1.sdk $0"

tool() {
    local found
    found=$(xcrun -sdk iphoneos -find "$1" 2>/dev/null || true)
    if [ -z "$found" ]; then
        for dir in "$DEV/Toolchains/XcodeDefault.xctoolchain/usr/bin" \
                   "$DEV/Platforms/iPhoneOS.platform/Developer/usr/bin" "$DEV/usr/bin" /usr/bin; do
            if [ -x "$dir/$1" ]; then
                found="$dir/$1"
                break
            fi
        done
    fi
    echo "$found"
}

CC=$(tool clang)
STRIP=$(tool strip)
CODESIGN_ALLOCATE=$(tool codesign_allocate)
[ -n "$CC" ] || die "clang не найден в Xcode."
export CODESIGN_ALLOCATE

say "Xcode: $DEV"
say "SDK:   $SDK"
"$CC" --version | head -1

# --- компиляция ---------------------------------------------------------------------------

rm -rf "$OBJ" "$APP"
mkdir -p "$OBJ" "$APP"

COMMON=(-arch "$ARCH" -isysroot "$SDK" -miphoneos-version-min="$MIN_IOS")
CFLAGS=("${COMMON[@]}" -fobjc-arc -Os -Wall -Wno-unused-function -IVendor/curl/include -ISources)

say "Компиляция"
for src in Sources/*.m; do
    echo "    $src"
    "$CC" "${CFLAGS[@]}" -c "$src" -o "$OBJ/$(basename "${src%.m}").o"
done

say "Компоновка"
"$CC" "${COMMON[@]}" -fobjc-arc -dead_strip "$OBJ"/*.o \
    -LVendor/curl/lib -lcurl -lssl -lcrypto -lz \
    -framework UIKit -framework Foundation -framework CoreGraphics -framework QuartzCore \
    -framework QuickLook -framework Security \
    -o "$APP/$NAME"
if [ -n "$STRIP" ]; then
    "$STRIP" -x "$APP/$NAME"
fi

# --- бандл --------------------------------------------------------------------------------

say "Ресурсы"
for item in Resources/*; do
    [ "$(basename "$item")" = Info.plist ] || cp -R "$item" "$APP/"
done
cp Resources/Info.plist "$APP/Info.plist"
plutil -convert binary1 "$APP/Info.plist"
printf 'APPL????' > "$APP/PkgInfo"

say "Подпись (${SIGN_IDENTITY:--})"
codesign -f -s "${SIGN_IDENTITY:--}" --entitlements entitlements.plist "$APP"

# --- .deb ---------------------------------------------------------------------------------
# .deb = ar-архив из debian-binary, control.tar.gz, data.tar.gz. dpkg на Mac нет, поэтому
# ar пишется вручную, а tar - в формате ustar без AppleDouble-файлов (их понимает dpkg на iOS).

field() { awk -F': ' -v key="$1" '$1 == key { print $2 }' control; }
PACKAGE=$(field Package)
VERSION=$(field Version)
DEB="$BUILD/${PACKAGE}_${VERSION}_iphoneos-arm.deb"
STAGE="$BUILD/deb"

say "Пакет $DEB"
rm -rf "$STAGE" "$DEB"
mkdir -p "$STAGE/data/Applications" "$STAGE/control"
cp -R "$APP" "$STAGE/data/Applications/"
{
    grep -v '^[[:space:]]*$' control
    echo "Installed-Size: $(du -sk "$STAGE/data" | cut -f1)"
} > "$STAGE/control/control"
cp layout/DEBIAN/postinst "$STAGE/control/postinst"
chmod 755 "$STAGE/control/postinst"
printf '2.0\n' > "$STAGE/debian-binary"

export COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1
(cd "$STAGE/control" && tar --format ustar -czf "$STAGE/control.tar.gz" .)
(cd "$STAGE/data" && tar --format ustar -czf "$STAGE/data.tar.gz" .)

ar_member() {
    local size
    size=$(stat -f %z "$1")
    printf '%-16s%-12s%-6s%-6s%-8s%-10s`\n' "$(basename "$1")" "$(date +%s)" 0 0 100644 "$size" >> "$DEB"
    cat "$1" >> "$DEB"
    if [ $((size % 2)) -eq 1 ]; then
        printf '\n' >> "$DEB"
    fi
}
printf '!<arch>\n' > "$DEB"
ar_member "$STAGE/debian-binary"
ar_member "$STAGE/control.tar.gz"
ar_member "$STAGE/data.tar.gz"

# --- .ipa (для AppSync / IPA Installer) ---------------------------------------------------

say "build/$NAME.ipa"
rm -rf "$BUILD/Payload" "$BUILD/$NAME.ipa"
mkdir -p "$BUILD/Payload"
cp -R "$APP" "$BUILD/Payload/"
(cd "$BUILD" && zip -qry "$NAME.ipa" Payload)
rm -rf "$BUILD/Payload"

# --- итог ---------------------------------------------------------------------------------

say "Готово"
lipo -info "$APP/$NAME" 2>/dev/null || true
otool -l "$APP/$NAME" | grep -A3 LC_VERSION_MIN_IPHONEOS | grep version || true
ls -lh "$DEB" "$BUILD/$NAME.ipa" | awk '{ print "    " $5 "  " $NF }'

if [ "$1" = install ]; then
    TARGET=${2:-root@iphone.local}
    say "Установка на $TARGET"
    scp "$DEB" "$TARGET:/tmp/ktk.deb"
    ssh "$TARGET" 'dpkg -i /tmp/ktk.deb && rm -f /tmp/ktk.deb'
    say "Установлено. Если иконка не появилась: ssh $TARGET uicache (или перезапустите SpringBoard)."
fi
