#!/bin/sh
# crosscheck.sh -- build the RTG DDX with a cross compiler and inspect what
#                  came out, before anything is copied to the machine.
#
# THIS RUNS ON THE BUILD HOST, not on AMIX.  Everything else in this repository
# is written for AMIX's Bourne shell; this one may assume a modern sh, python3
# and a m68k cross toolchain.
#
#   sh tools/crosscheck.sh -s /path/to/amix/rootfs
#
# The sysroot is a mounted vanilla AMIX filesystem: it must contain
# usr/include, usr/amiga/include and usr/x11r5.  Pass it with -s or set
# AMIX_SYSROOT.  Nothing about it is recorded in this repository.
#
# Options:
#   -s DIR    AMIX filesystem root                  (or $AMIX_SYSROOT)
#   -c PREFIX cross tool prefix                     (or $AMIX_CROSS; default
#                                                    m68k-cbm-sysv4- if that
#                                                    is on PATH, else
#                                                    m68k-linux-gnu-)
#   -p PROF   flag profile: native | modern         (default: from the
#                                                    compiler's version)
#   -a ARCH   override the architecture flag
#   -e        treat 68060-unsafe instructions as a failure even in the
#             native profile, where they are expected
#   -x        skip the mi/dix sample scan (faster)
#   -k        keep the scratch build tree
#
# Exit status: 0 pass, 1 a check failed, 2 a usage or environment problem.

set -e

# ---------------------------------------------------------------------------
# TWO COMPILERS, TWO FLAG SETS.  Conflating them is the mistake this comment
# exists to prevent.
#
#   native   GNU C 2.7.2.3, target m68k-cbm-sysv4 -- the same compiler that is
#            installed on the machine, so this profile checks what the machine
#            will actually produce.  It has -O2; the architecture flag is
#            -m68030, because -m68040 and -m68020-40 make it emit one
#            MIT-syntax mnemonic the AMIX assembler refuses (see the syntax
#            check near the end).  It does NOT
#            have -m68060 or -m68020-60, and no flag it does have avoids the
#            64-bit MULS.L/DIVS.L forms the 68060 lacks.  That is a property
#            of the compiler, not a mistake in the flags, so the scan reports
#            those instructions in this profile without failing.  Matches
#            ServerCDebugFlags in config/amix.cf with AmixGccMajor 2.
#
#            (A stock AMIX install has GNU C 1.40.5 instead, which has neither
#            -O2 nor any of this.  That is AmixGccMajor 1 in amix.cf, and it
#            cannot be checked here -- there is no 1.40 cross compiler.)
#
#   modern   a current GCC, e.g. m68k-linux-gnu-gcc 14.  Generates better code
#            and -m68020-60 avoids the trapping instructions entirely, but it
#            needs an ABI flag (-malign-int) and a build flow that links on
#            the machine.  Not what config/amix.cf drives.
# ---------------------------------------------------------------------------

profile_native() {
    ARCH_DEF=-m68030
    OPT="-O2 -fomit-frame-pointer"
    ABI=""
    LANG_FLAGS="-ansi -finline-functions"
    CODEGEN=""
    # gcc 2.7.2.3 has no flag that avoids the 64-bit MULS.L/DIVS.L forms, so
    # finding them here is expected and is reported rather than failed.
    SCAN_FATAL=0
    WARN="-Wall -Wno-implicit -Wno-return-type -Wno-parentheses -Wno-unused"
}

profile_modern() {
    ARCH_DEF=-m68020-60
    OPT="-O2 -fomit-frame-pointer"
    # -malign-int: AMIX aligns struct members to 4 bytes, modern GCC to 2 on
    # m68k.  Without it every structure shared with libc or libX11 has the
    # wrong layout -- and the program otherwise runs, which is the danger.
    # -fcommon: X11R5 relies on tentative definitions.
    # -fno-strict-aliasing: the server type-puns constantly.
    ABI="-malign-int -fcommon -fno-strict-aliasing -fno-asynchronous-unwind-tables"
    LANG_FLAGS="-std=gnu89 -fpermissive"
    # Stops GCC turning a hand-written VRAM fill loop into a memset call.
    CODEGEN="-fno-tree-loop-distribute-patterns"
    SCAN_FATAL=1
    WARN="-Wall -Wno-implicit-int -Wno-implicit-function-declaration \
-Wno-return-type -Wno-parentheses -Wno-unused-variable \
-Wno-unused-but-set-variable -Wno-missing-braces -Wno-unknown-pragmas \
-Wno-builtin-declaration-mismatch"
}

DEFS="-DAMIX -Dm68k -DSVR4 -DXDMCP -DSHAPE -DMULTIBUFFER -DMITMISC -DMITSHM"
RTGDEFS="-DRTG -DVA2000"

# ---------------------------------------------------------------------------

SYSROOT="${AMIX_SYSROOT:-}"
CROSS="${AMIX_CROSS:-}"
PROFILE=""
ARCH=""
SCAN_CORE=1
FORCE_FATAL=0
KEEP=0

while [ $# -gt 0 ]; do
    case "$1" in
    -s) SYSROOT="$2"; shift 2 ;;
    -c) CROSS="$2";   shift 2 ;;
    -p) PROFILE="$2"; shift 2 ;;
    -a) ARCH="$2";    shift 2 ;;
    -e) FORCE_FATAL=1; shift ;;
    -x) SCAN_CORE=0;  shift ;;
    -k) KEEP=1;       shift ;;
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
    *)  echo "crosscheck: unknown option $1" >&2; exit 2 ;;
    esac
done

repo=$(cd "$(dirname "$0")/.." && pwd)

if [ -z "$CROSS" ]; then
    if command -v m68k-cbm-sysv4-gcc >/dev/null 2>&1; then
        CROSS=m68k-cbm-sysv4-
    else
        CROSS=m68k-linux-gnu-
    fi
fi

if [ -z "$SYSROOT" ]; then
    echo "crosscheck: no AMIX sysroot." >&2
    echo "  Pass -s /path/to/amix/rootfs, or set AMIX_SYSROOT." >&2
    echo "  It is a mounted vanilla AMIX filesystem and must contain" >&2
    echo "  usr/include, usr/amiga/include and usr/x11r5." >&2
    exit 2
fi

for d in usr/include usr/amiga/include usr/x11r5/include \
         usr/x11r5/server/include usr/x11r5/server/ddx/mi \
         usr/x11r5/server/ddx/mfb usr/x11r5/server/ddx/amix \
         usr/x11r5/fonts/include; do
    if [ ! -d "$SYSROOT/$d" ]; then
        echo "crosscheck: $SYSROOT/$d is missing -- is that an AMIX root?" >&2
        exit 2
    fi
done

CC="${CROSS}gcc"
OBJDUMP="${CROSS}objdump"
AS="${CROSS}as"
command -v "$CC"      >/dev/null 2>&1 || { echo "crosscheck: $CC not found" >&2; exit 2; }
command -v "$OBJDUMP" >/dev/null 2>&1 || { echo "crosscheck: $OBJDUMP not found" >&2; exit 2; }

# Version detection has to survive a wrapper.  The m68k-cbm-sysv4 driver is a
# shell wrapper that fixes up assembly on the way past, and it answers
# -dumpversion by trying to link -- so ask the real driver first, then the
# front end, then fall back to the target name.
ccver=""
if [ -x "$(command -v "$CC").real" ]; then
    ccver=$("$(command -v "$CC").real" -dumpversion 2>/dev/null || true)
fi
case "$ccver" in
[0-9]*) ;;
*)  ccver=$("$CC" -dumpversion 2>/dev/null || true) ;;
esac
case "$ccver" in
[0-9]*) ;;
*)  case "$CROSS" in
    *cbm-sysv4*) ccver="2.7.2.3 (assumed from target name)" ;;
    *)           ccver="unknown" ;;
    esac ;;
esac

if [ -z "$PROFILE" ]; then
    case "$ccver" in
    1.*|2.*) PROFILE=native ;;
    *)       PROFILE=modern ;;
    esac
fi
case "$PROFILE" in
native) profile_native ;;
modern) profile_modern ;;
*) echo "crosscheck: unknown profile '$PROFILE' (native|modern)" >&2; exit 2 ;;
esac
[ -n "$ARCH" ] || ARCH="$ARCH_DEF"
[ "$FORCE_FATAL" = 1 ] && SCAN_FATAL=1

X="$SYSROOT/usr/x11r5"
scratch=$(mktemp -d "${TMPDIR:-/tmp}/xrtg-crosscheck.XXXXXX")
if [ "$KEEP" = 0 ]; then
    trap 'rm -rf "$scratch"' EXIT INT TERM
fi

echo "crosscheck: ${CROSS}gcc $ccver, profile $PROFILE, arch $ARCH"
echo "            sysroot $SYSROOT"
echo "            scratch $scratch"
echo

# ---------------------------------------------------------------------------
# Overlay tree and header isolation.
#
# The DDX sources include "../../amix.h" and friends by relative path, and
# those files come from the vanilla tree, not from this repository.  So build
# the same overlay install-sources.sh builds on the machine: vanilla first,
# this repository's files on top.
#
# The X11/ symlink is the same fix install-sources.sh applies on the machine:
# AMIX keeps the X headers in /usr/x11r5/include with no X11/ subdirectory,
# but the sources include <X11/Xmd.h>.  On a Linux build host that resolves --
# to the HOST's X11 headers, silently, which is worse than failing.  An
# earlier version of this script did exactly that and reported a pass built
# partly against Linux headers, so: -nostdinc, and every include path named.
# ---------------------------------------------------------------------------

mkdir -p "$scratch/build" "$scratch/inc"
ln -s "$X/include" "$scratch/inc/X11"

cp -r "$X/server/ddx/amix" "$scratch/build/amix"
chmod -R u+w "$scratch/build/amix"

R="$repo/usr/x11r5/server/ddx/amix"
cp "$R"/*.c "$R"/Imakefile "$scratch/build/amix/" 2>/dev/null || true
mkdir -p "$scratch/build/amix/rtg/va2000"
cp "$R"/rtg/*.c "$R"/rtg/*.h "$R"/rtg/Imakefile "$scratch/build/amix/rtg/"
cp "$R"/rtg/va2000/* "$scratch/build/amix/rtg/va2000/"

B="$scratch/build"

# The compiler's own headers (stddef.h, stdarg.h).  Not every wrapper answers
# -print-file-name, so only add it when it names a real directory.
GCCINC=$("$CC" -print-file-name=include 2>/dev/null || true)
if [ -n "$GCCINC" ] && [ -d "$GCCINC" ]; then
    SELFINC="-isystem $GCCINC"
else
    SELFINC=""
fi

BASEINC="-nostdinc -I$scratch/inc \
-I$X/server/include -I$X/include -I$X/server/ddx/mi -I$X/server/ddx/mfb \
-I$X/fonts/include -I$SYSROOT/usr/amiga/include -I$SYSROOT/usr/include \
$SELFINC"
INC="-I$B/amix/rtg/va2000 -I$B/amix/rtg -I$B/amix $BASEINC"

CFLAGS="-c $OPT $ARCH $ABI $LANG_FLAGS $CODEGEN"

# Prove the isolation rather than assume it: no header may come from outside
# the sysroot, the scratch tree or the toolchain.
echo "Checking header isolation"
leak=$("$CC" $CFLAGS -w $DEFS $RTGDEFS $INC -H \
       "$B/amix/rtg/va2000/va2000win.c" -o /dev/null 2>&1 |
       grep '^\.* */' | sed 's/^\.* *//' |
       grep -v "^$SYSROOT" | grep -v "^$scratch" |
       grep -v "$(dirname "$(command -v "$CC")")" || true)
if [ -n "$leak" ]; then
    echo "  headers pulled from outside the sysroot:"
    printf '%s\n' "$leak" | sed 's/^/    /' | head -10
    echo "  refusing to report a result built against the wrong headers." >&2
    exit 1
fi
echo "  ok, every header came from the sysroot"

# ---------------------------------------------------------------------------
# Compile
# ---------------------------------------------------------------------------

# The DDX sources, taken from the Imakefile so this list cannot drift out of
# step with what the machine builds -- it already did once, and a file that
# is not compiled here passes every check in this script by not existing.
IMAKE_SRCS=$(sed -n 's/^SRCS = //p;s/^ *\(va2000[a-z]*\.c\)/\1/p' \
             "$B/amix/rtg/va2000/Imakefile" 2>/dev/null |
             tr -d '\\' | tr ' ' '\n' | grep '\.c$' || true)
for f in $IMAKE_SRCS; do
    if [ ! -f "$B/amix/rtg/va2000/$f" ]; then
        echo "crosscheck: Imakefile lists $f but it is not there" >&2
        exit 1
    fi
done

SRCS="amix/rtg/rtgInit.c \
amix/rtg/va2000/va2000hw.c amix/rtg/va2000/va2000screen.c \
amix/rtg/va2000/va2000draw.c amix/rtg/va2000/va2000text.c \
amix/rtg/va2000/va2000tile.c amix/rtg/va2000/va2000win.c \
amix/rtg/va2000/va2000cmap.c amix/rtg/va2000/va2000pix.c \
amix/rtg/va2000/va2000cursor.c \
amix/amixIo.c amix/amixInit.c amix/amixCursor.c"

echo
echo "Compiling the RTG DDX"
status=0
warned=0
cd "$B"
for f in $SRCS; do
    o="$scratch/obj-$(basename "$f" .c).o"
    if out=$("$CC" $CFLAGS $WARN $DEFS $RTGDEFS $INC "$f" -o "$o" 2>&1); then
        n=$(printf '%s\n' "$out" | grep -c "warning:" || true)
        if [ "$n" -gt 0 ]; then
            printf "  %-20s ok, %s warning(s)\n" "$(basename "$f")" "$n"
            printf '%s\n' "$out" | grep "warning:" | sed 's/^/      /'
            warned=$((warned + n))
        else
            printf "  %-20s ok\n" "$(basename "$f")"
        fi
    else
        printf "  %-20s FAILED\n" "$(basename "$f")"
        printf '%s\n' "$out" | sed 's/^/      /' | head -20
        status=1
    fi
done

# A sample of the core server.  The DDX barely divides by a constant, so it
# says little on its own; mi and dix are where the trapping forms appear, and
# they are the code the DDX spends its time inside.
if [ "$SCAN_CORE" = 1 ]; then
    echo
    echo "Compiling a core-server sample (mi, dix)"
    CORE="$X/server/ddx/mi/miarc.c $X/server/ddx/mi/miregion.c \
$X/server/ddx/mi/mifillarc.c $X/server/ddx/mi/miwideline.c \
$X/server/ddx/mi/mizerarc.c $X/server/ddx/mi/mibitblt.c \
$X/server/ddx/mi/miglblt.c $X/server/ddx/mi/mipushpxl.c \
$X/server/dix/colormap.c $X/server/dix/dispatch.c $X/server/dix/window.c"
    n=0
    for f in $CORE; do
        o="$scratch/core-$(basename "$f" .c).o"
        if "$CC" $CFLAGS -w $DEFS $BASEINC "$f" -o "$o" 2>/dev/null; then
            n=$((n + 1))
        else
            echo "  note: $(basename "$f") did not compile; skipped"
        fi
    done
    echo "  $n file(s) compiled for the scan"
fi

# ---------------------------------------------------------------------------
# 68060 instruction scan.
#
# objdump prints the 32-bit and the 64-bit forms of DIVxL/MULxL with the same
# mnemonic, so decode the extension word instead: bit 10 set means the 64-bit
# result the 68060 does not implement.  Technique from check-68060.sh in the
# saku26-amix-demo repository.
# ---------------------------------------------------------------------------

echo
echo "Scanning for instructions the 68060 lacks"
"$OBJDUMP" -d "$scratch"/*.o > "$scratch/dis.txt" 2>/dev/null || true

set +e
python3 - "$scratch/dis.txt" "$SCAN_FATAL" "$PROFILE" <<'PY_EOF'
import re, sys

path, fatal, profile = sys.argv[1], sys.argv[2] == "1", sys.argv[3]
pat = re.compile(r'^\s*([0-9a-f]+):\s*((?:[0-9a-f]{4}\s+)+)\s*(div|mul)(s|u)l',
                 re.IGNORECASE)
total = 0
bad = []
for line in open(path):
    m = pat.match(line)
    if not m:
        continue
    total += 1
    words = m.group(2).split()
    if len(words) >= 2 and int(words[1], 16) & 0x0400:   # bit 10 = 64-bit
        bad.append(line.strip())

print("  %d long multiply/divide instructions examined" % total)
if not bad:
    print("  none use the 64-bit-result form -- safe on 68040 and 68060")
    sys.exit(0)

print("  %d use the 64-bit-result form the 68060 LACKS:" % len(bad))
for b in bad[:8]:
    print("    %s" % b)
if len(bad) > 8:
    print("    ... and %d more" % (len(bad) - 8))
print()
print("  On a 68060 these trap into kernel emulation.  The cause is a %")
print("  operator or a division by a literal constant.")
if fatal:
    sys.exit(1)
print("  EXPECTED in the '%s' profile: GNU C 2.7.2.3 has no flag that" % profile)
print("  avoids them, so this is a property of the compiler on the machine,")
print("  not of these flags.  Avoiding them needs the 'modern' profile and a")
print("  build flow that links on the machine.  Reported, not failed.")
sys.exit(0)
PY_EOF
[ $? -ne 0 ] && status=1
set -e

# ---------------------------------------------------------------------------
# Other instructions the two processors disagree about -- ADVISORY ONLY.
#
# The 68060 also lacks MOVEP, CAS2, CHK2 and CMP2; the 68030 lacks MOVE16,
# CINV/CPUSH and the 040 single/double-rounded FPU forms.  A compiler does not
# normally generate any of them, so this is a net cast for surprises.
#
# It cannot be a hard check, and the reason is worth writing down.  objdump -d
# on a .o disassembles everything in .text, and m68k switch tables live in
# .text right after the `jmp %pc@(...)` that uses them.  Table data decodes as
# plausible instructions: a first run of this scan reported four MOVEPs in
# window.o, and all four were jump-table entries.  MOVEP's opcode word is the
# shape small integers take, so that will keep happening.
#
# The MULS.L/DIVS.L scan above does not have this problem in practice -- its
# hits were checked by hand and sit in recognisable reciprocal-multiply and
# divide sequences -- but this one does.  So it prints context and asks a
# human, rather than deciding.
# ---------------------------------------------------------------------------

echo
echo "Looking for other 68030/68060 instruction differences (advisory)"
set +e
python3 - "$scratch/dis.txt" <<'PY_EOF'
import re, sys

names = (r'move16|cinva|cinvl|cinvp|cpusha|cpushl|cpushp|plpa|lpstop|'
         r'f[sd](add|sub|mul|div|abs|neg|sqrt|move)|'
         r'movep|cas2|chk2|cmp2|callm|rtm')
line_re = re.compile(r'^\s*[0-9a-f]+:\s*(?:[0-9a-f]{2,4}\s+)+\s*([a-z][a-z0-9]*)',
                     re.IGNORECASE)
name_re = re.compile(r'^(%s)$' % names, re.IGNORECASE)

lines = open(sys.argv[1]).read().splitlines()
hits = []
for i, line in enumerate(lines):
    m = line_re.match(line)
    if m and name_re.match(m.group(1)):
        hits.append(i)

if not hits:
    print("  none found")
    sys.exit(0)

print("  %d suspect instruction(s) -- CHECK THESE BY HAND." % len(hits))
print("  A `jmp %pc@(...)` a few lines above means it is switch-table data")
print("  being disassembled, not code, and can be ignored.")
for i in hits[:5]:
    print()
    for j in range(max(0, i - 3), min(len(lines), i + 2)):
        mark = ">>>" if j == i else "   "
        print("    %s %s" % (mark, lines[j].strip()))
if len(hits) > 5:
    print()
    print("    ... and %d more" % (len(hits) - 5))
sys.exit(0)
PY_EOF
set -e

# ---------------------------------------------------------------------------
# Assembly the AMIX assembler will actually accept (native profile only).
#
# The m68k-cbm-sysv4 target emits AT&T syntax: the move mnemonic is `mov`,
# 855 times in one file.  With -m68040 or -m68020-40, GNU C 2.7.2.3 replaces
# the frame setup `link.w %fp,&0` with `pea (%fp)` + `move.l %sp,%fp` --
# spelling that one instruction the MIT way.  The AMIX assembler rejects it
# ("invalid instruction name") and the build dies; GNU as accepts it and
# assembles it as movea.l, so a cross build never notices.
#
# That is exactly the kind of thing this script exists to catch and did not,
# so: generate assembly and look for the mnemonic that cannot be right.
# ---------------------------------------------------------------------------

if [ "$PROFILE" = native ]; then
    echo
    echo "Checking the assembly against AMIX assembler syntax"
    # The DDX alone is not a representative sample -- the frame-setup path
    # only appears in functions with a certain shape, and it first showed up
    # in multibuf.c.  Scan a spread of the server, including the file that
    # actually broke the build.
    SYNSRCS="$SRCS"
    for extra in "$X/extensions/server/multibuf.c" "$X/extensions/server/shm.c" \
                 "$X/server/ddx/mi/miarc.c" "$X/server/ddx/mi/miregion.c" \
                 "$X/server/dix/dispatch.c" "$X/server/dix/window.c"; do
        [ -f "$extra" ] && SYNSRCS="$SYNSRCS $extra"
    done
    bad_syntax=0
    for f in $SYNSRCS; do
        s_out="$scratch/syn-$(basename "$f" .c).s"
        "$CC" -S $OPT $ARCH $ABI $LANG_FLAGS $CODEGEN -w $DEFS $RTGDEFS \
            $INC -I"$X/extensions/include" \
            "$f" -o "$s_out" 2>/dev/null || continue
        n=$(grep -c '	move\.' "$s_out" || true)
        if [ "$n" -gt 0 ]; then
            printf "  %-20s %s use 'move.' where this target spells it 'mov.'\n" \
                   "$(basename "$f")" "$n"
            grep -n '	move\.' "$s_out" | head -3 | sed 's/^/      /'
            bad_syntax=$((bad_syntax + n))
        fi
    done
    if [ "$bad_syntax" -gt 0 ]; then
        echo "  the AMIX assembler will reject these -- change the -m flag"
        status=1
    else
        echo "  ok, no mnemonic the AMIX assembler would refuse"
    fi
fi

# ---------------------------------------------------------------------------

echo
if [ "$status" = 0 ]; then
    echo "crosscheck: PASS  ($warned warning(s) in the DDX)"
    echo
    if [ "$PROFILE" = native ]; then
        echo "Flags checked -- this is ServerCDebugFlags in config/amix.cf"
        echo "for AmixGccMajor 2:"
        echo "  $OPT $ARCH"
    else
        echo "Flags checked (cross path only -- the build on the machine uses"
        echo "ServerCDebugFlags from config/amix.cf, a different set):"
        echo "  $OPT $ARCH $ABI $LANG_FLAGS $CODEGEN"
    fi
else
    echo "crosscheck: FAIL"
fi
exit $status
