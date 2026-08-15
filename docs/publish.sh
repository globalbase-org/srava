#!/bin/bash
# srava ドキュメントをビルドして公開先へ展開する(ワンコマンド)。
#   使い方:  docs/publish.sh
#     1) build_html.sh で docs/_site に HTML 生成
#     2) 公開先へ転送(ssh エイリアス globalbaseProject 経由・鍵は ~/.ssh/config 任せ)
#     3) 公開 URL を curl で疎通確認
#   公開先は環境変数 SRAVA_DOCS_REMOTE で上書き可(既定: 下記)。
#   公開 URL: https://project.globalbase.org/releases/srava/
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/_site"
REMOTE="${SRAVA_DOCS_REMOTE:-globalbaseProject:/home/gbs/public/releases/srava}"
URL="${SRAVA_DOCS_URL:-https://project.globalbase.org/releases/srava/}"
host="${REMOTE%%:*}"; path="${REMOTE#*:}"

# 1) ビルド
bash "$HERE/build_html.sh" "$OUT"

# 2) 転送
echo "deploying → $REMOTE"
ssh "$host" "mkdir -p '$path'"
scp -q -r "$OUT"/* "$host:$path/"

# 3) 疎通確認
echo "verifying → $URL"
ok=1
for f in "" srava_function_reference.html srava_language_reference.html srava_module_reference.html srava_roll_reference.html style.css; do
  code=$(curl -s -o /dev/null -w "%{http_code}" --max-time 15 "$URL$f" || echo "ERR")
  echo "  [$code] $URL$f"
  [ "$code" = "200" ] || ok=0
done
[ "$ok" = "1" ] && echo "published OK → $URL" || { echo "WARNING: 一部 200 以外。反映待ち or 設定要確認" >&2; exit 1; }
