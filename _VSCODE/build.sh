#!/bin/bash

set -e

echo "========================================"
echo "Building PankLang VSCode Extension..."
echo "========================================"

cd "$(dirname "$0")"

if ! command -v node &> /dev/null; then
    echo "[ERROR] Node.js is not installed"
    echo "Please install Node.js from https://nodejs.org/"
    exit 1
fi

VERSION=$(grep -o '"version": *"[^"]*"' package.json | cut -d'"' -f4)
echo ""
echo "[INFO] Version: $VERSION"

echo ""
echo "[1/3] Cleaning old builds..."
rm -f *.vsix

echo ""
echo "[2/3] Validating extension..."
npx --yes vsce validate 2>/dev/null || echo "[WARNING] Validation found issues, continuing..."

echo ""
echo "[3/3] Packaging extension..."
npx --yes vsce package

echo ""
echo "========================================"
echo "SUCCESS: Extension built successfully!"
echo "Output: ji-lang-${VERSION}.vsix"
echo "========================================"
echo ""
echo "To install: code --install-extension ji-lang-${VERSION}.vsix"