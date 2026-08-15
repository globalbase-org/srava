#!/bin/bash
# srava ドキュメント(Markdown)を pandoc で HTML 化する。
#   使い方:  docs/build_html.sh [出力ディレクトリ]   (既定: docs/_site)
#   生成: index.html / srava_language_reference.html / srava_function_reference.html + style.css
# 公開先 https://project.globalbase.org/releases/srava/ にこの出力をそのまま展開する。
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$HERE/_site}"

command -v pandoc >/dev/null || { echo "pandoc が必要です" >&2; exit 1; }

mkdir -p "$OUT"
cp "$HERE/web/style.css" "$OUT/style.css"

build() {   # 入力md  HTMLタイトル  出力ファイル名
  pandoc "$1" -f markdown -t html5 --standalone --toc --toc-depth=3 \
    --metadata title="$2" --metadata lang=ja -c style.css -o "$OUT/$3"
  echo "  $3"
}

echo "building srava docs → $OUT"
build "$HERE/web/index.md"                "srava ドキュメント"          index.html
build "$HERE/srava_install_guide.md"      "srava インストールガイド"    srava_install_guide.html
build "$HERE/cygwin_build.md"             "srava Cygwin ビルド手順"     cygwin_build.html
build "$HERE/srava_language_reference.md" "srava 言語リファレンス"      srava_language_reference.html
build "$HERE/srava_function_reference.md" "srava 関数リファレンス"      srava_function_reference.html
build "$HERE/srava_module_reference.md"   "srava モジュールリファレンス" srava_module_reference.html
build "$HERE/srava_roll_reference.md"     "srava 螺旋巻きつけライブラリ" srava_roll_reference.html
build "$HERE/srava_module_design.md"      "srava モジュール設計"        srava_module_design.html
build "$HERE/srava_kwave.md"              "srava → k-Wave 音響シミュレーション" srava_kwave.html
echo "done."
