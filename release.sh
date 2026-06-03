#!/usr/bin/env bash
# Usage: ./release.sh v0.0.8
set -euo pipefail

VERSION=${1:?Usage: $0 <version>  e.g. v0.0.8}
VERSION_BARE=${VERSION#v}

REPO="$(cd "$(dirname "$0")" && pwd)"
PATCH_DIR="$REPO/patch-init"
RACK_DIR="$REPO/vcv-rack"
PLUGIN_JSON="$RACK_DIR/plugin.json"

# ── Sanity checks ─────────────────────────────────────────────────────────────
for cmd in gh jq zip; do
    command -v "$cmd" &>/dev/null || {
        echo "error: $cmd not found — install with: brew install $cmd" >&2; exit 1
    }
done

if git tag --list | grep -qx "$VERSION"; then
    echo "error: tag $VERSION already exists" >&2; exit 1
fi

if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "error: uncommitted changes — commit or stash before releasing" >&2; exit 1
fi

# ── Bump plugin.json version if needed ───────────────────────────────────────
CURRENT=$(jq -r '.version' "$PLUGIN_JSON")
if [[ "$CURRENT" != "$VERSION_BARE" ]]; then
    echo "Bumping plugin.json: $CURRENT → $VERSION_BARE"
    jq --arg v "$VERSION_BARE" '.version = $v' "$PLUGIN_JSON" > "$PLUGIN_JSON.tmp"
    mv "$PLUGIN_JSON.tmp" "$PLUGIN_JSON"
    git add "$PLUGIN_JSON"
    git commit -m "Bump VCV plugin version to $VERSION_BARE"
fi

# ── Build Daisy firmware ──────────────────────────────────────────────────────
echo "Building Daisy firmware..."
make -C "$PATCH_DIR" clean > /dev/null 2>&1
make -C "$PATCH_DIR" 2>&1 | grep -E "error:|Memory region|%age Used" || true
BIN="$PATCH_DIR/build/Tymbal.bin"
[[ -f "$BIN" ]] || { echo "error: Tymbal.bin not found — build failed" >&2; exit 1; }
echo "  → Tymbal.bin ($(du -h "$BIN" | cut -f1))"

# ── Build VCV Rack plugin ─────────────────────────────────────────────────────
echo "Building VCV Rack plugin..."
make -C "$RACK_DIR" clean > /dev/null 2>&1
make -C "$RACK_DIR" 2>&1 | grep -v "^In file\|DaisySP\|^\.\./\|^$" | grep -E "error:|warning:|\.dylib" || true
DYLIB="$RACK_DIR/plugin.dylib"
[[ -f "$DYLIB" ]] || { echo "error: plugin.dylib not found — build failed" >&2; exit 1; }
echo "  → plugin.dylib"

# ── Package as .vcvplugin zip ─────────────────────────────────────────────────
echo "Packaging VCV Rack plugin..."
PLUGIN_PKG="CicadaSound-Tymbal-${VERSION_BARE}-mac-arm64.vcvplugin"
STAGING=$(mktemp -d)
mkdir -p "$STAGING/res"
cp "$PLUGIN_JSON"    "$STAGING/"
cp "$DYLIB"          "$STAGING/"
cp "$RACK_DIR/res/"* "$STAGING/res/"
(cd "$STAGING" && zip -r "$REPO/$PLUGIN_PKG" . --exclude "*/.DS_Store") > /dev/null
rm -rf "$STAGING"
echo "  → $PLUGIN_PKG"

# ── Draft release notes ───────────────────────────────────────────────────────
NOTES=$(mktemp)
LAST_TAG=$(git tag --sort=-version:refname | head -1 2>/dev/null || echo "")

{
    if [[ -n "$LAST_TAG" ]]; then
        echo "## Changes since $LAST_TAG"
        echo ""
        git log "${LAST_TAG}..HEAD" --pretty="- %s"
    else
        echo "## Changes"
        echo ""
        git log --pretty="- %s" | head -20
    fi
    echo ""
    echo "## Firmware"
    echo "- \`Tymbal.bin\` — flash to Daisy Patch SM via DFU"
    echo ""
    echo "## VCV Rack"
    echo "- \`$PLUGIN_PKG\` — drag into Rack to install"
} > "$NOTES"

echo ""
echo "Opening release notes for editing (save and close to continue)..."
${EDITOR:-vi} "$NOTES"

# ── Tag and push ──────────────────────────────────────────────────────────────
git tag "$VERSION"
git push origin HEAD
git push origin "$VERSION"

# ── Create GitHub release ─────────────────────────────────────────────────────
echo "Creating GitHub release $VERSION..."
gh release create "$VERSION" \
    --title "Tymbal $VERSION" \
    --notes-file "$NOTES" \
    "$BIN#Tymbal.bin (Daisy firmware)" \
    "$REPO/$PLUGIN_PKG#VCV Rack plugin (mac arm64)"

rm -f "$REPO/$PLUGIN_PKG" "$NOTES"
echo ""
gh release view "$VERSION" --json url -q '"Release: " + .url'
