#!/bin/bash
# KTKLegacy for Symbian - builds the .sis on Linux: the Qt SDK 1.2.1 toolchain (GCCE 4.4.1, Raptor,
# Perl) and build-symbian.cmd run under wine. Needs wine, 7z (p7zip) and python3.
#
#   ./build-symbian-wine.sh setup <QtSdk-offline-win-x86-v1.2.1.exe> <Belle_SDK_for_QtSDK_v1.2.1_SymbianSR1Qt474.7z>
#                                   once: unpacks the toolchain and the SDK, prepares the wine prefix
#   ./build-symbian-wine.sh         KTKLegacy_<version>.sis and KTKLegacy_installer_<version>.sis here
#   ./build-symbian-wine.sh bump    the same, after raising the patch number in KTKLegacy.pro
#   ./build-symbian-wine.sh check   UID, version and capabilities of the built KTKLegacy_<version>.sis
#   ./build-symbian-wine.sh clean   removes the build output
#
# The installer is never run: the packages it carries (7z archives inside the .exe) are cut out
# and unpacked. Everything goes to $KTK_SYMBIAN_SDK (default ~/.local/share/ktk-symbian-sdk):
# QtSDK/ laid out like C:\QtSDK, and wineprefix/ where C:\QtSDK points at it. The project is built
# in a copy at C:\work\ktk-symbian (Symbian builds in the source folder, and the SDK and the
# project must share a drive).
set -euo pipefail
cd "$(dirname "$0")"
PROJECT=$(pwd)

ROOT=${KTK_SYMBIAN_SDK:-$HOME/.local/share/ktk-symbian-sdk}
QTSDK=$ROOT/QtSDK
SDK=$QTSDK/Symbian/SDKs/SymbianSR1Qt474
export WINEPREFIX=$ROOT/wineprefix
export WINEDEBUG=-all
export WINEDLLOVERRIDES="mscoree,mshtml="   # no Mono/Gecko install prompts
WORK=$WINEPREFIX/drive_c/work/ktk-symbian

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die() { printf '\033[31mОшибка:\033[0m %s\n' "$*" >&2; exit 1; }

need() {
    for tool in "$@"; do
        command -v "$tool" >/dev/null || die "нет $tool (Arch: sudo pacman -S wine p7zip python)"
    done
}

# Runs a Windows command line in a folder of the wine C: drive; output without CR, minus the
# graphics-driver noise some tools print.
wcmd() {
    local dir=$1; shift
    # stdin from an empty pipe: with a plain </dev/null Raptor's mingw32-make fails under wine
    # with "create_child_process: DuplicateHandle(In) failed".
    (cd "$dir" && : | wine cmd /c "$*" 2>&1) | tr -d '\r' | grep -v -e '^MESA' -e '^pci id for fd' || true
}

# --- setup -------------------------------------------------------------------------------------

# Cuts the 7z archives out of the installer and unpacks those holding the wanted paths.
carve_toolchain() {
    local exe=$1 tmp=$2
    python3 -I - "$exe" "$tmp" "$QTSDK" <<'EOF'
import mmap, os, struct, subprocess, sys, zlib
exe, tmp, dest = sys.argv[1:4]
WANTED = ('Symbian/tools/gcce4/', 'Symbian/tools/perl/', 'Symbian/tools/sbs/',
          'Symbian/cache/common-symbian-belle-gcce.7z')
os.makedirs(tmp, exist_ok=True)
f = open(exe, 'rb'); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
sig = b'7z\xbc\xaf\x27\x1c'; pos = 0; found = set()
while True:
    i = m.find(sig, pos)
    if i < 0:
        break
    pos = i + 1
    head = m[i:i + 32]
    if len(head) < 32 or zlib.crc32(head[12:32]) & 0xffffffff != struct.unpack('<I', head[8:12])[0]:
        continue                                   # not a real 7z start header
    offset, size = struct.unpack('<QQ', head[12:28])
    end = i + 32 + offset + size
    if end > len(m):
        continue
    part = os.path.join(tmp, 'part.7z')
    with open(part, 'wb') as out:
        out.write(m[i:end])
    names = subprocess.run(['7z', 'l', '-ba', '-slt', part], capture_output=True, text=True).stdout
    paths = [l[7:].replace('\\', '/') for l in names.splitlines() if l.startswith('Path = ')]
    hits = {w for w in WANTED for p in paths if p.startswith(w) or p == w.rstrip('/')}
    if hits:
        subprocess.run(['7z', 'x', '-y', '-o' + dest, part], check=True, stdout=subprocess.DEVNULL)
        found |= hits
        print('   unpacked:', ', '.join(sorted(h.rstrip('/') for h in hits)), flush=True)
    os.remove(part)
    pos = end
missing = [w for w in WANTED if w not in found]
if missing:
    sys.exit('not found in the installer: ' + ', '.join(missing))
EOF
}

setup() {
    local exe=${1:-} belle=${2:-}
    [ -f "$exe" ] && [ -f "$belle" ] || die "укажите оба файла: $0 setup <QtSdk-offline-win-x86-v1.2.1.exe> <Belle_SDK_...7z> (где взять - QT-SDK.txt)"
    exe=$(realpath "$exe"); belle=$(realpath "$belle")
    need wine 7z python3
    case "$ROOT" in *' '*) die "в пути $ROOT не должно быть пробелов" ;; esac

    say "Пакеты из установщика -> $QTSDK"
    mkdir -p "$QTSDK"
    carve_toolchain "$exe" "$ROOT/.carve"
    rmdir "$ROOT/.carve" 2>/dev/null || true

    say "Belle SDK"
    rm -rf "$SDK"
    mkdir -p "$QTSDK/Symbian/SDKs"
    7z x -y -o"$QTSDK/Symbian/SDKs" "$belle" >/dev/null
    [ -f "$SDK/env.bat" ] || die "в $belle нет SymbianSR1Qt474"
    # Qt Quick Components for Belle, as the installer adds them to the SDK.
    for archive in "$QTSDK"/Symbian/cache/*-belle-gcce.7z; do
        7z x -y -o"$SDK" "$archive" >/dev/null
    done

    say "wine: $WINEPREFIX"
    wineboot -i >/dev/null 2>&1 || true
    [ -d "$WINEPREFIX/drive_c" ] || die "wineboot не создал $WINEPREFIX"
    ln -sfn "$QTSDK" "$WINEPREFIX/drive_c/QtSDK"

    say "Пути qmake (patch.qmake.paths.bat)"
    # Entered through the C:\QtSDK link: the script writes its working folder into qt.conf, and
    # entered by the real path wine would call it Z:\home\...
    wcmd "$WINEPREFIX/drive_c/QtSDK/Symbian/SDKs/SymbianSR1Qt474" patch.qmake.paths.bat >/dev/null
    grep -q 'C:/QtSDK/Symbian/SDKs/SymbianSR1Qt474' "$SDK/bin/qt.conf" || die "qt.conf не обновился"
    say "Готово. Сборка: $0"
}

# --- build -------------------------------------------------------------------------------------

sync_project() {
    mkdir -p "$WORK"
    # The project's own files (tracked and new, without what .gitignore lists); the generated
    # files of earlier builds stay in the copy, so Raptor rebuilds only what changed.
    if git -C "$PROJECT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        (cd "$PROJECT" && git ls-files -co --exclude-standard -z -- . \
            | while IFS= read -r -d '' f; do [ -e "$f" ] && printf '%s\0' "$f"; done \
            | xargs -0 cp --parents -t "$WORK")
    else
        cp -r "$PROJECT"/. "$WORK"/
    fi
}

check_setup() {
    need wine
    [ -f "$SDK/bin/qt.conf" ] && [ -d "$QTSDK/Symbian/tools/sbs" ] && [ -L "$WINEPREFIX/drive_c/QtSDK" ] \
        || die "SDK не подготовлен: $0 setup <QtSdk-offline-win-x86-v1.2.1.exe> <Belle_SDK_...7z>"
}

version() { awk '$1 == "VERSION" && $2 == "=" { print $3 }' "$1/KTKLegacy.pro" | tr -d '\r'; }

build() {
    local arg=${1:-}
    check_setup
    sync_project
    say "Сборка KTKLegacy $(version "$WORK")${arg:+ ($arg)}"
    rm -f "$WORK"/KTKLegacy*.sis
    wcmd "$WORK" "set QTSDK=C:\\QtSDK&& build-symbian.cmd $arg" | tee "$WORK/build.log" \
        | grep -E -e '^ (compile|target) ' -e '^sbs : (errors|warnings)' -e '[Ee]rror:' -e '^ERROR' -e '^built ' || true
    grep -q '^sbs : errors: 0' "$WORK/build.log" || die "сборка не удалась, лог: $WORK/build.log"
    if [ "$arg" = bump ]; then
        cp "$WORK/KTKLegacy.pro" "$PROJECT/KTKLegacy.pro"
    fi
    local ver; ver=$(version "$WORK")
    [ -f "$WORK/KTKLegacy_$ver.sis" ] || die "нет KTKLegacy_$ver.sis, лог: $WORK/build.log"
    cp "$WORK"/KTKLegacy*_"$ver".sis "$PROJECT"/
    say "Готово:"
    ls -l "$PROJECT"/KTKLegacy*_"$ver".sis | awk '{ print "    " $5 "  " $NF }'
}

check() {
    check_setup
    local ver sis tmp
    ver=$(version "$PROJECT")
    sis=$PROJECT/KTKLegacy_$ver.sis
    [ -f "$sis" ] || die "нет $sis, сначала соберите"
    tmp=$WINEPREFIX/drive_c/work/check
    rm -rf "$tmp" && mkdir -p "$tmp" && cp "$sis" "$tmp/app.sis"
    wcmd "$tmp" "C:\\QtSDK\\Symbian\\SDKs\\SymbianSR1Qt474\\epoc32\\tools\\dumpsis.exe -x app.sis" >/dev/null
    local pkg; pkg=$(find "$tmp/app" -name '*.pkg' | head -1)
    [ -n "$pkg" ] || die "dumpsis не распаковал пакет"
    say "$sis"
    iconv -f utf-16 -t utf-8 "$pkg" 2>/dev/null | tr -d '\r' | grep -E '^#|^\(|^"' || true
    wcmd "$tmp/app" "C:\\QtSDK\\Symbian\\SDKs\\SymbianSR1Qt474\\epoc32\\tools\\elf2e32.exe --dump=hs --e32input=file0" \
        | grep -E 'Secure ID|Capabilities|^ +[A-Z][A-Za-z]+$|Module Version|Heap Size|Stack Size'
    rm -rf "$tmp"
}

clean() {
    check_setup
    if [ -d "$WORK" ]; then
        wcmd "$WORK" "set QTSDK=C:\\QtSDK&& build-symbian.cmd clean" >/dev/null
        rm -rf "$WORK"
    fi
    say "Копия проекта в C:\\work удалена (готовые .sis в $PROJECT не тронуты)"
}

case "${1:-}" in
    setup) shift; setup "$@" ;;
    check) check ;;
    clean) clean ;;
    ''|bump) build "${1:-}" ;;
    *) die "неизвестная команда $1 (setup, bump, check, clean или без аргументов)" ;;
esac
