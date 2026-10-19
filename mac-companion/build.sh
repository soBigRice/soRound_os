#!/bin/zsh
set -eu
ROOT="${0:A:h}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/soround-media.XXXXXX")"
trap 'rm -rf -- "$WORK"' EXIT
APP="$ROOT/dist/soRound Media.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
# 固定上游源码、离线构建；不安装、不自启、不执行媒体测试。
# Build pinned source offline; never install, auto-start, or run media tests.
cmake -S "$ROOT/vendor/mediaremote-adapter" -B "$WORK/adapter" -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK/adapter" --target MediaRemoteAdapter -j 2
ditto "$WORK/adapter/MediaRemoteAdapter.framework" "$APP/Contents/Resources/MediaRemoteAdapter.framework"
cp "$ROOT/vendor/mediaremote-adapter/bin/mediaremote-adapter.pl" "$APP/Contents/Resources/"
cp "$ROOT/vendor/mediaremote-adapter/LICENSE" "$APP/Contents/Resources/MediaRemoteAdapter.LICENSE"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>local.sobigrice.soround-media</string>
<key>CFBundleName</key><string>soRound Media</string>
<key>CFBundleExecutable</key><string>soRoundMedia</string>
<key>CFBundleVersion</key><string>1</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>LSUIElement</key><true/>
<key>NSBluetoothAlwaysUsageDescription</key><string>仅向已配对的 soRound 发送本机曲目信息与封面。Send local Now Playing artwork and text to your paired soRound only.</string>
</dict></plist>
PLIST
xcrun swiftc -swift-version 5 -O -framework AppKit -framework CoreBluetooth -framework CoreText -framework ImageIO \
    "$ROOT/Sources/main.swift" -o "$APP/Contents/MacOS/soRoundMedia"
codesign --force --deep --sign - "$APP"
print -r -- "Built: $APP"
