#!/usr/bin/env bash
# Turns a Ship of Harkinian checkout into Zombies of Time: applies the engine
# patches, then copies the mode's sources into it (SoH's CMake globs every
# source file under soh/soh/). Run it again after editing anything under mod/;
# patches that are already applied are skipped.
#
#   scripts/apply.sh                       # the Shipwright submodule (Linux, Windows)
#   scripts/apply.sh --android soh-android # the SoH Android port tree (CI's Android build)
#
# Engine patches:
#   mod/patches/*.patch            applied to the tree's root with `git -C <tree> apply`
#   mod/patches/<dir>/*.patch      applied inside <tree>/<dir>, for a nested submodule
#                                  such as libultraship (git apply can't cross into one)
#   mod/patches-android/...        same layout, Android tree only. A file here with the
#                                  same relative path as one in mod/patches/ replaces it
#                                  on Android (for a patch whose context differs there);
#                                  the rest are Android-only additions.
# Patches are applied in file-name order, so number new ones (0003-..., 0004-...).
# A patch that neither applies nor is already applied stops the script.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD

ANDROID=0
if [ "${1:-}" = "--android" ]; then
    ANDROID=1
    shift
fi
TREE=${1:-$([ $ANDROID = 1 ] && echo soh-android || echo Shipwright)}

if [ "$TREE" = Shipwright ] && [ ! -f Shipwright/CMakeLists.txt ]; then
    git submodule update --init --recursive
fi
if [ ! -f "$TREE/CMakeLists.txt" ] || [ ! -d "$TREE/soh" ]; then
    echo "apply.sh: $TREE is not a Ship of Harkinian checkout" >&2
    exit 1
fi

# Relative paths (from mod/patches/ or mod/patches-android/) of the patches to apply, in order.
list_patches() {
    local shared=mod/patches android=mod/patches-android
    {
        if [ -d $shared ]; then
            (cd $shared && find . -name '*.patch' | sed 's|^\./||')
        fi
        if [ $ANDROID = 1 ] && [ -d $android ]; then
            (cd $android && find . -name '*.patch' | sed 's|^\./||')
        fi
    } | LC_ALL=C sort -u
}

# The file to use for a relative patch path: the Android override when there is one.
patch_file() {
    if [ $ANDROID = 1 ] && [ -f "mod/patches-android/$1" ]; then
        echo "$ROOT/mod/patches-android/$1"
    else
        echo "$ROOT/mod/patches/$1"
    fi
}

# Windows checkouts can have CRLF line endings where the patches have LF:
# --ignore-whitespace lets the context lines match either way.
APPLY_FLAGS="--ignore-whitespace"

applied=0
skipped=0
while IFS= read -r rel; do
    [ -n "$rel" ] || continue
    file=$(patch_file "$rel")
    dir=$(dirname "$rel")
    target=$TREE
    [ "$dir" = . ] || target="$TREE/$dir"
    if git -C "$target" apply $APPLY_FLAGS --check "$file" 2>/dev/null; then
        git -C "$target" apply $APPLY_FLAGS "$file"
        echo "  applied  ${file#"$ROOT"/}"
        applied=$((applied + 1))
    elif git -C "$target" apply $APPLY_FLAGS --reverse --check "$file" 2>/dev/null; then
        echo "  present  ${file#"$ROOT"/}"
        skipped=$((skipped + 1))
    else
        echo "apply.sh: ${file#"$ROOT"/} does not apply to $target:" >&2
        git -C "$target" apply $APPLY_FLAGS --check -v "$file" >&2 || true
        exit 1
    fi
done < <(list_patches)
echo "Engine patches: $applied applied, $skipped already present in $TREE"

cp -R mod/soh/. "$TREE/soh/"
echo "Zombies of Time copied into $TREE/soh/soh/Enhancements/Zombies"
