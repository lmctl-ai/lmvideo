#!/usr/bin/env bash
# Prints the Skia/release platform of this machine: linux-x64, linux-arm64, macos-x64 or macos-arm64.
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) echo linux-x64;;
  Linux-aarch64|Linux-arm64) echo linux-arm64;;
  Darwin-x86_64) echo macos-x64;;
  Darwin-arm64) echo macos-arm64;;
  *) echo "unsupported platform: $(uname -s)-$(uname -m)" >&2; exit 1;;
esac
