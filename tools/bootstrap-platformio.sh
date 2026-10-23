#!/usr/bin/env sh
set -eu
PYTHON="${PYTHON:-python3}"
"$PYTHON" tools/dev.py setup
"$PYTHON" tools/dev.py doctor
echo "Next: $PYTHON tools/dev.py test"
echo "Next: $PYTHON tools/dev.py build"
