#!/bin/sh
# ★★ #3535: **上流の可変大域状態を「設定する」呼び出しが持ち込まれたら赤にする**。
#   $1 = ソースの根 (CMAKE_CURRENT_SOURCE_DIR)。$2 = build ディレクトリ (省略可)。
#
# ⚠⚠ なぜ要るか — #3535 を閉じるときに **地雷を 1 つ残した**ため。
#   cgal.so と libsrava_cg は CGAL の可変な大域状態を **2 コピー持っている** (2026-09-15 実測。
#   mac / Linux 両方):
#       get_default_random()::default_random
#       get_static_error_handler()::_error_handler
#       get_static_error_behaviour()::_error_behaviour
#       IO::Static::get_mode()::mode
#       Lazy_exact_nt<>::relative_precision_of_to_double   ← ★ 2026-09-15 に **後から見つけた**
#
# ⚠⚠ 最後の 1 つは、名前を手で並べたリストが **漏れていた**ことの実例 (CGAL::to_double が引き込む)。
#   ⇒ 見つけ方を一般化した: デマングルすると関数内 static は Foo::bar()::baz の形になるので、
#     ')::[A-Za-z_][A-Za-z_0-9]*$' で全 .so を横断して拾い、**複数の .so に同名で定義**されて
#     いるものを見る。それでこの 1 本が出た。
#   ⚠ ただし **可変か不変かは nm の型欄では分からない** (const な Lazy<>::zero()::z も
#     可変な relative_precision_of_to_double も mac ではどちらも型 S) ⇒ 分類は人がやるしかない。
#     だからこの柵は *名前のリスト* のままにしてある。
#   CGAL 6.x は**ヘッダオンリー**で上流の実体が存在しないため、geogram (#3535①) のように
#   「借りる形に寄せる」ことができない。⇒ 実体化そのものを止める (op から CGAL を排す) 以外に
#   直す道が無く、そこは未着手のまま閉じた。
#
# ★ いま害が出ていないのは **誰もこの 4 つを設定していないから**であって、構造が安全だからではない。
#   ⇒ 「設定した瞬間に踏む」ので、**設定を持ち込んだ瞬間に赤くする**。
#   ⚠ とくに @CGAL::IO::set_mode@ は ASCII/binary の切り替えなので、片方の .so で設定すると
#     **書き出しの形式が黙って変わる** (落ちないので、テストが無ければ気づけない)。
#
# ⚠ これは *柵* であって直しではない。直すなら #3535 の ②(op から CGAL を排す) を再開すること。
#   ⇒ 直ったら (= cgal.so が上流の可変大域を 1 本も定義しなくなったら) この検査は要らなくなる。
S="${1:?source dir not given}"

# ★★ #3522: ハングの番犬 (共通)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

fails=0

# ⚠ 探すのは **設定する側**だけ。読む側 (get_mode / get_default_random の呼び出し) は
#   2 コピーでも「それぞれの既定値」を読むだけなので、まだ害にならない。
#   ★ 害になるのは *片方に書いて、もう片方で読む* 形 ⇒ **書く呼び出しの有無**が正しい指標。
for pat in \
	'CGAL::set_error_handler' \
	'CGAL::set_warning_handler' \
	'CGAL::set_error_behaviour' \
	'CGAL::set_warning_behaviour' \
	'CGAL::IO::set_mode' \
	'CGAL::set_ascii_mode' \
	'CGAL::set_binary_mode' \
	'CGAL::set_pretty_mode' \
	'set_relative_precision_of_to_double'
do
	# ⚠ コメント行は除く (この検査自身の説明や、注意書きに名前が出るのは正当)。
	hit=$(grep -rn -- "$pat" "$S/modules" "$S/src" 2>/dev/null |
	      grep -v '^\([^:]*\):[0-9]*:[[:space:]]*[*/#]' || true)
	if [ -n "$hit" ]; then
		echo "FAIL: **$pat** が持ち込まれた"
		printf '%s\n' "$hit" | sed 's/^/     /'
		fails=$((fails+1))
	fi
done

if [ "$fails" != "0" ]; then
	echo ""
	echo "  ⚠⚠ CGAL の可変大域状態は **cgal.so と libsrava_cg に 2 コピーある** (#3535)。"
	echo "     片方で設定してももう片方には効かない。⇒ 設定は黙って無視されたように見える。"
	echo "     ★ とくに IO::set_mode は ASCII/binary なので **書き出しの形式が黙って変わる**。"
	echo "  ⇒ どうしても要るなら、先に #3535 の ② (op から CGAL を排す) を済ませること。"
	echo "     応急でやるなら **両方の .so で同じ設定をする** 経路を作る (片方だけでは効かない)。"
	exit 1
fi
echo "UPSTREAM-GLOBALS-OK 設定の持ち込みなし"
