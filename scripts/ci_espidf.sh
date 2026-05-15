#!/usr/bin/env bash
set -euo pipefail

APP=$1
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="$REPO_DIR/apps/$APP"

echo "REPO_DIR: $REPO_DIR"

if [ -d /opt/docker30 ]; then
    for f in /opt/docker30/*.yml; do
        echo "=== $f ==="
        cat "$f"
    done
fi

cd "$APP_DIR"
echo "Running ESP-IDF build for $APP_DIR..."
source "$IDF_PATH/export.sh"
idf.py set-target esp32
idf.py reconfigure
idf.py build > /tmp/idf_build.log 2>&1 || { cat /tmp/idf_build.log; exit 1; }

echo "--- QEMU output ---"
coproc QEMU { idf.py qemu 2>&1; }
SAVED_PID=${QEMU_PID:-}
echo "DEBUG: coproc started, QEMU_PID=${SAVED_PID}"
while IFS= read -r line <&"${QEMU[0]}"; do
    echo "$line"
    if [[ "$line" == *"END OF UNITTESTS"* ]]; then
        echo "DEBUG: saw END OF UNITTESTS, breaking"
        break
    fi
done
echo "DEBUG: loop done, SAVED_PID=${SAVED_PID}, QEMU_PID=${QEMU_PID:-<unset>}"
pkill -P "${SAVED_PID}" 2>/dev/null || true
kill "${SAVED_PID}" 2>/dev/null || true
wait "${SAVED_PID}" 2>/dev/null || true
echo "DEBUG: cleanup done"
