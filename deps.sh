#!/bin/bash
# deps.sh - resolve Maven AARs without Gradle
# Usage: add entries to DEPS array as "group:artifact:version"
# Artifacts are cached in .deps/

set -e

DEPS=(
  "androidx.webgpu:webgpu:1.0.0-alpha05"
)

GOOGLE_MAVEN="https://dl.google.com/android/maven2"
MAVEN_CENTRAL="https://repo1.maven.org/maven2"

DEPS_DIR=".deps"
mkdir -p "$DEPS_DIR"

resolve_aar() {
  local spec="$1"
  local group=$(echo "$spec" | cut -d: -f1)
  local artifact=$(echo "$spec" | cut -d: -f2)
  local version=$(echo "$spec" | cut -d: -f3)

  local group_path=$(echo "$group" | tr '.' '/')
  local aar_name="${artifact}-${version}.aar"
  local out_dir="$DEPS_DIR/${artifact}-${version}"

  if [ -d "$out_dir" ]; then
    echo "[deps] cached: $spec"
    return 0
  fi

  mkdir -p "$out_dir"

  local url
  for base in "$GOOGLE_MAVEN" "$MAVEN_CENTRAL"; do
    url="${base}/${group_path}/${artifact}/${version}/${aar_name}"
    if curl -sf -o "$out_dir/${aar_name}" "$url"; then
      echo "[deps] fetched: $spec"
      break
    fi
  done

  if [ ! -f "$out_dir/${aar_name}" ]; then
    echo "[deps] ERROR: could not resolve $spec"
    return 1
  fi

  unzip -q "$out_dir/${aar_name}" -d "$out_dir"
  rm "$out_dir/${aar_name}"

  echo "[deps] extracted: $out_dir"
}

for dep in "${DEPS[@]}"; do
  resolve_aar "$dep"
done