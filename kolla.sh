#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Allt som ska vara grönt före en push: varje moduls egen kolla.sh, sedan webbemulatorn (byggs, och provas i
# Chrome om Playwright finns). blonda-motor och fm1-dock bredvid blonda-fm1, eller BLONDA_MOTOR / FM1_DOCK.
set -e
cd "$(dirname "$0")"
ROOT=$(pwd)
: "${BLONDA_MOTOR:=$ROOT/../blonda-motor}"
: "${FM1_DOCK:=$ROOT/../fm1-dock}"
export BLONDA_MOTOR FM1_DOCK
for m in moduler/*/; do
    echo "######## $m"
    sh "$m/kolla.sh"
done
echo "######## web"
sh web/bygg.sh
if python3 -c "import playwright" 2>/dev/null; then   # (felen visas och stoppar kollen: tail svalde förut svaret)
    if ! python3 web/test_sida.py > build/test_sida.log 2>&1; then grep "^FEL" build/test_sida.log; tail -1 build/test_sida.log; exit 1; fi
    tail -1 build/test_sida.log
fi
echo "allt grönt"
