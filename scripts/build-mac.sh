#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
CONFIG="${1:-Release}"

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake が見つかりません。Xcode Command Line Tools を入れてください。" >&2
    exit 1
fi

echo "Building LiteHost ($CONFIG) for macOS..."
cmake -B "$BUILD" -S "$ROOT" -G Xcode
cmake --build "$BUILD" --config "$CONFIG"

APP="$BUILD/LiteHost_artefacts/$CONFIG/LiteHost.app"
if [[ ! -d "$APP" ]]; then
    APP="$BUILD/LiteHost_artefacts/LiteHost.app"
fi

if [[ ! -d "$APP" ]]; then
    echo "ビルド成果物が見つかりません: LiteHost.app" >&2
    exit 1
fi

echo "OK: $APP"
