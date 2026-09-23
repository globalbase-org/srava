#!/bin/sh
# ★★ #3535: **上流ライブラリのコピーが 2 つになっていないか数える**。$1 = build ディレクトリ。
#
# srava のモジュール (.so) は幾何クラスの共有ライブラリ (libsrava_XX) と同じ上流を
# *自分にも静的リンクしている*ことがある。静的アーカイブは **参照されたオブジェクトだけ**を
# 引くので、両方が「自分が参照した分」を別々に抱え込む。
# ⚠⚠ 上流が **プロセス大域の可変状態**を持つと、そこで壊れる: geogram は CmdLine の変数表と
#   Process のスレッド数を file-scope に持っており、2026-09-14 に *初期化したコピーと使う
#   コピーが別*で SIGSEGV した (#3535 4 節・Linux)。
#
# ★ 理想形は「**幾何 lib が上流シンボルを export し、モジュールは借りる**」。
#   ⇒ この検査は **2 つ**を見る:
#     ① モジュール側が上流シンボルを *定義* していないこと (= コピーが 2 つない)
#     ② ★★ モジュールが **借りている上流シンボルが、その幾何 lib から実際に解決すること**
# ⚠ 「借用が何本あるか」は見ない — 使う op が増減すれば動く数で、約束ではない。
#   見たいのは *コピーが 2 つあるか* だけなので、**定義の有無**が正しい指標。
#
# ⚠⚠ ② が要る理由 (2026-09-15・bench が Linux の対照ビルドで見つけた **この検査自身の穴**):
#   geogram を default 可視性で建てる手当て (#3535①③) だけを外したビルドは、
#     ・**リンクは通る** (モジュール .so は未定義シンボルを既定で許すため)
#     ・上流シンボルの **自前定義は 0 本**  ⇒ ①だけだと **満点で通る**
#     ・しかし実行時に @dlopen@ が落ちる:
#         [pig] cannot load module 'geogram.so': undefined symbol: GEO::Mesh::copy
#   ⇒ ① は必要条件であって十分条件ではない。**借り先が export していなければ動かない**。
#   ★ この穴は **mac では露出しない** — mac は元から export があり、B 相当の状態を作れない
#     ⇒ *Linux でしか検定できない穴が、検査自身にもあった*。
#
# ⚠⚠ 数え方の落とし穴 4 つ (①② は 2026-09-15 ・ ③④ は 2026-09-16 に実際に踏んだ):
#   ① nm の型欄は **行によって位置が動く**。定義行は "<addr> <型> <名前>" (3 欄) だが、
#      未定義行はアドレスが空白で "<型> <名前>" (2 欄) になる。$2 を型とみなすと
#      **未定義行の名前が型欄に入り、全部「定義」に数えられる**。⇒ 型は $(NF-1) で取る。
#   ② デマングルは **awk で選り分けた後**にかける。先にかけると名前に空白が入って
#      $(NF-1) がもう型ではなくなり、①の対策ごと効かなくなる。
#   ⚠ `nm -g` は使わない — Linux は hidden 可視性で全部 local になり、-g だと
#     **常に 0 本に見えて検査が空振りする** (mac だけで通る検査になってしまう)。
#   ③ ★★ 除外は `/^[Uu]$/` — これは **2 つの別のものを落としている**。意図 (①の説明) は
#      「未定義行 `U` を落とす」だが、効果は「`U` (未定義) と `u` (STB_GNU_UNIQUE の *定義*) を
#      落とす」。⇒ 意図と効果が食い違っている式なので、**なぜ `u` も落としてよいか**を書く:
#        `u` はローダが **プロセスに 1 個だけ**作ると保証するシンボル。つまり `u` の定義は
#        「2 つ目のコピー」ではなく、**1 つしかないコピーへの参加**。この検査が数えたいのは
#        *コピーが 2 つあるか* なので、`u` を定義として数えない方が **中身として正しい**。
#      ⚠ モジュール .so は `-fvisibility=hidden` で建つので同じものが `d` (local) に落ちる。
#        `d` は本当にその .so 専用のコピー = 数えるべき ⇒ この式はそれを数える。正しい。
#      ⚠ `u` は GNU の拡張。mac / Windows には無いので、そちらでは `U` だけが落ちる。
#   ④ ⚠⚠ **同じ式を `.o` に使ってはいけない — 確実に間違う**。
#      `.o` では CGAL の可変大域は *まさに* `u` で出る (2026-09-16 実測・bench):
#          $ nm CMakeFiles/srava_cg.dir/.../cgMesh3D.cpp.o | c++filt | grep get_mode
#          0000000000000000 u CGAL::IO::Static::get_mode()::mode
#      `u` を落とす式で数えると **どの .o も常に 0 本**になり、検査が丸ごと空振りする。
#      ⇒ `.o` を数える検査 (#3545 段 3 = test/srava_op_cgal_free.sh) は **`U` だけ**を落とす。
#      ★ つまりこの式は *対象が .so であること* に依存している。`.so` では偶然ではなく正しく、
#        `.o` では確実に間違い — **対象を変えたら式も変える**。
D="${1:?build dir not given}"

# ★★ #3522: ハングの番犬 (共通)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

command -v nm      >/dev/null 2>&1 || { echo "UPSTREAM-SKIP nm が無い";      exit 0; }
command -v c++filt >/dev/null 2>&1 || { echo "UPSTREAM-SKIP c++filt が無い"; exit 0; }

fails=0
TMP="${TMPDIR:-/tmp}/upstream-$$"; mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT
# デマングル名の飾りを落とす (vtable / typeinfo / thunk は本体と同じ持ち主とみなす)。
STRIP='s/^vtable for //; s/^typeinfo for //; s/^typeinfo name for //; s/^VTT for //;
       s/^construction vtable for //; s/^non-virtual thunk to //; s/^virtual thunk to //;
       s/^guard variable for //'

# ★ 動的シンボル表を取る。Linux は @nm -D@ ・mac は -D が無いので @nm -g@ へ落とす。
#   ⚠ 幾何 lib 側は **動的に export しているか**が問題なので、通常の表 (local 込み) では
#     答えにならない (Linux は hidden 可視性で local になるため、まさにそこが争点)。
# ★ モジュールが実際にリンクしている共有ライブラリの basename を並べる。
#   mac は @c otool -L ・Linux は @c objdump -p の NEEDED。⚠ どちらも無ければ空 (検査は skip 扱い)。
linked_libs() {
	if command -v otool >/dev/null 2>&1; then
		otool -L "$1" 2>/dev/null | tail -n +2 | awk '{print $1}' | sed 's|.*/||'
	elif command -v objdump >/dev/null 2>&1; then
		objdump -p "$1" 2>/dev/null | awk '$1=="NEEDED"{print $2}'
	fi
}

dynsyms() {
	_o=$(nm -D "$1" 2>/dev/null)
	[ -n "$_o" ] || _o=$(nm -g "$1" 2>/dev/null)
	printf '%s\n' "$_o"
}

ndef() {   # ndef <.so> <名前空間>  → その名前空間を **先頭に持つ**定義の数
	nm "$1" 2>/dev/null |
	  awk 'NF>=2 && $(NF-1) !~ /^[Uu]$/ {print $NF}' |
	  c++filt | sed "$STRIP" | grep -c "^$2"
}
nund() {   # nund <.so> <名前空間>  → 未定義参照 (= 借りている) の数
	nm "$1" 2>/dev/null |
	  awk 'NF>=2 && $(NF-1) ~ /^[Uu]$/ {print $NF}' |
	  c++filt | sed "$STRIP" | grep -c "^$2"
}

# check <モジュール.so> <名前空間> <許容する定義数> <なぜその数か>
check() {
	mod="$D/$1"
	if [ ! -f "$mod" ]; then echo "      skip $1 (建っていない)"; return 0; fi
	d=$(ndef "$mod" "$2"); u=$(nund "$mod" "$2")
	echo "      $1: $2 定義 $d (許容 $3) ・借用 $u"
	if [ "$d" -gt "$3" ]; then
		echo "FAIL: $1 が $2 を **自分で $d 本定義している** (許容 $3) = 上流のコピーが増えた"
		echo "  ⇒ 大域状態を持つ上流だと、初期化したコピーと使うコピーが食い違う (#3535 4 節)"
		echo "  ⇒ 直し方: op から上流を直に触らず幾何クラス側の .so へ寄せ、"
		echo "     CMake の LINK 行から上流を外す (モジュールは借りるだけにする)"
		echo "  ⇒ 増えた分: 下の一覧と #3535 の表を突き合わせること"
		nm "$mod" 2>/dev/null | awk 'NF>=2 && $(NF-1) !~ /^[Uu]$/ {print $NF}' |
		  c++filt | sed "$STRIP" | grep "^$2" | sed 's/^/       /' | head -20
		fails=$((fails+1))
	fi
	# ---- ② 借りている上流シンボルが **実際にリンク先から解決するか** ----
	# ⚠ マングル名のまま突き合わせる (デマングルすると空白で欄がずれる・表記ゆれも出る)。
	# ★★ 2026-09-15: 借り先を **決め打ちしない**。geogram を共有ライブラリへ移したとき、
	#   借り先が libsrava_gg → libgeogram へ変わって検査が空振りした (実際に赤くなって気づいた)。
	#   ⇒ **モジュールが実際にリンクしている .so を辿って**、その和集合で解決するかを見る。
	#   これは *ローダがやること* と同じ形なので、配線を変えても追随する。
	[ -n "$5" ] || return 0
	deps=$(linked_libs "$mod")
	: > "$TMP/have"
	for _d in $deps; do
		_f=""
		for _c in "$D/$_d" "$D/lib/$_d" "$(dirname "$mod")/$_d"; do
			[ -f "$_c" ] && { _f="$_c"; break; }
		done
		[ -n "$_f" ] || continue
		dynsyms "$_f" | awk 'NF>=2 && $(NF-1) !~ /^[Uu]$/ {print $NF}' | sed 's/^_//' >> "$TMP/have"
	done
	sort -u -o "$TMP/have" "$TMP/have"
	# 上流の名前空間だけを対象にする (libc / libstdc++ 等は別の .so が解決する)
	nm "$mod" 2>/dev/null | awk 'NF>=2 && $(NF-1) ~ /^[Uu]$/ {print $NF}' |
	  grep -- "$5" | sed 's/^_//' | sort -u > "$TMP/need"
	miss=$(comm -23 "$TMP/need" "$TMP/have" | wc -l | tr -d ' ')
	echo "      $1: 借りている $2 $(wc -l < "$TMP/need" | tr -d ' ') 本 / リンク先が export していないもの $miss 本"
	if [ "$miss" != "0" ]; then
		echo "FAIL: $1 が借りている $2 のうち **$miss 本がリンク先のどれからも解決しない**"
		echo "  ⇒ リンクは通るが **dlopen で落ちる** (モジュール .so は未定義を既定で許すため)"
		echo "  ⇒ 辿ったリンク先: $deps"
		echo "  ⇒ 直し方: 上流を **default 可視性**で建てて幾何クラス側の .so から export させる"
		echo "     (#3535① の ③。Linux の -fvisibility=hidden で local になっているのが典型)"
		comm -23 "$TMP/need" "$TMP/have" | head -10 | c++filt | sed 's/^/       /'
		fails=$((fails+1))
	fi
}

# ★★ #3535②: **上流の可変大域状態がモジュール側にも実体化していないこと**。
#   ⚠ 「上流のコピーが 2 つ」より狭く、より直接的な検査。CGAL はヘッダオンリーなので
#     アーカイブの重複は起きないが、*テンプレートの実体化* で **可変な大域変数の実体**が
#     モジュール .so にもできる。2 つあると片方で設定してももう片方に効かない。
#   ★ とくに @c IO::Static::get_mode は ASCII/binary なので **書き出しの形式が黙って変わる**。
#   ⇒ 2026-09-15 に cgal.so から 4 つとも追い出した (op の CGAL 呼び出しを libsrava_cg へ移した)。
#     ここが 0 でなくなったら **その移動が崩れた**ということ。
# ★ nef も 2026-09-15 に 0 本にした。⚠ nef は **構造が cgal と違う** ので手当ても違った:
#   nfMesh は @c Nef_polyhedron_3 を値で持ち、**ctor / dtor がヘッダに inline で在った**ため、
#   *nfMesh を作るだけの op* (nfaEmpty3D 等 11 本) にも実体ができていた。
#   ⇒ ctor / dtor を .cpp へ移すだけで 11 本が消え、残る nfaBox / nfaTranslate だけ
#     cgal と同じ「呼び出しを幾何 lib 側へ移す」で片付いた。
# ⚠⚠ 名前は **手で並べたリスト**。2026-09-15 に 5 本目 (relative_precision_of_to_double) が
#   後から見つかった ⇒ リストは漏れうる。
#   ★ 漏れを探す一般的な方法: デマングルすると関数内 static は Foo::bar()::baz の形になるので
#       nm <so> | awk '... {print $NF}' | c++filt | grep -E '\)::[A-Za-z_][A-Za-z_0-9]*$'
#     を全 .so で取り、**複数の .so に同名で定義**されているものを見る。
#   ⚠ 可変か不変かは nm の型欄では分からない (const な Lazy<>::zero()::z も可変な
#     relative_precision も mac では型 S) ⇒ 分類は人がやる。だからリストのままにしてある。
check_globals() {   # check_globals <モジュール.so> [許容数]
	mod="$D/$1"; allow="${2:-0}"
	[ -f "$mod" ] || { echo "      skip $1 (建っていない)"; return 0; }
	n=0; names=""
	for g in 'get_default_random()::default_random' \
	         'get_static_error_handler()::_error_handler' \
	         'get_static_error_behaviour()::_error_behaviour' \
	         'IO::Static::get_mode()::mode' \
	         'relative_precision_of_to_double_internal()::'
	do
		c=$(nm "$mod" 2>/dev/null | awk 'NF>=2 && $(NF-1) !~ /^[Uu]$/ {print $NF}' |
		    c++filt | grep -cF "$g")
		[ "$c" != "0" ] && { n=$((n+c)); names="$names $g"; }
	done
	echo "      $1: CGAL の可変大域 $n 本 (許容 $allow)"
	if [ "$n" -gt "$allow" ]; then
		echo "FAIL: $1 が CGAL の可変大域状態を **$n 本 自前で持っている** (許容 $allow)"
		echo "  ⇒ libsrava_cg 側のものと別物になり、片方で設定してももう片方に効かない"
		echo "  ⇒ とくに IO::set_mode は ASCII/binary なので **書き出しの形式が黙って変わる**"
		echo "  ⇒ 直し方: その op の CGAL 呼び出しを cgMesh3D / cgMesh2D 側へ移す"
		echo "     (#3535② の形。cgaBox / cgaPrism / cgaTube / cgaExtrude / cgaRevolve /"
		echo "      cgaImport が先例)"
		for g in $names; do echo "       $g"; done
		fails=$((fails+1))
	fi
}

echo "--- 上流ライブラリのコピー数 (#3535)"
# ★★ geogram — #3535① で「借りる」形へ寄せた。**0 が約束**。
#   ここが 0 でなくなったら 2026-09-14 の SIGSEGV が戻る道が開いている。
check geogram.so  "GEO::"       0 srava_gg "3GEO"
# ★ cherchi — 元から理想形 (cinolib はヘッダのみで、アーカイブ自体が無い)。
check cherchi.so  "cinolib::"   0
# ★ manifold — 元から理想形。**変えていない**が、崩れたら気づけるように固定する (#3535 2 節)。
#   ⚠ 0 ではない: `MeshGLP<...>::~MeshGLP()` のような **ヘッダのテンプレート実体化**が残る。
#     これは上流アーカイブのコピーではない (weak シンボルでローダが畳む) ので害が無く、
#     0 を要求すると *直しようのない赤*になる。⇒ 現状 4 本 + 余裕を見て 8 を上限にする。
#   ⚠ 「返り値が manifold:: の std:: テンプレート」も先頭一致で拾ってしまう
#     (例: std::vector<manifold::Manifold>::__push_back_slow_path)。数え方の粗さとして許容する
#     — 上限を見る検査なので、粗い分は上振れ側に出るだけで見落としにはならない。
check manifold.so "manifold::"  8 srava_mf "8manifold"

# ★★ #3535②: CGAL の可変大域状態が cgal.so 側に実体化していないこと (上の check_globals 参照)。
echo "--- CGAL の可変大域状態 (#3535②)"
# ★ CGAL を使うモジュールすべて。建っていないものは skip される。
#   ⚠ nef は 2 変種を同じソースから作るので、片方が緑ならもう片方も同じ形になる
#     (構成で片方しか建てないことがあるため、両方書いて skip に任せる)。
for _m in cgal.so nef_snc.so nef_hybrid.so nef_cg.so nef_mf.so; do
	check_globals "$_m"
done
# ⚠ openvdb_cg.so だけ **1 本を許容**する。vcaExportVox.cpp が SoS 述語つきの厳密計算
#   (EFT の四則 + CGAL::sign + to_double) を持っており、これは **橋モジュール固有のロジック**。
#   libsrava_cg へ移すと「橋の都合を cgal の幾何ライブラリに入れる」ことになり置き場所が悪い。
#   ★ 残るのは relative_precision_of_to_double の 2 コピー目だけで、これは *読む* 側の利用。
#     実害は setter を呼んだときだけで、そこは test/srava_upstream_globals.sh が止める。
#   (ひさ判断 2026-09-15: 「openvdb とも共有しているわけですしね。やめておきましょう」)
check_globals openvdb_cg.so 1

# ⚠ cgal / nef / openvdb は **ここでは見ない**。CGAL はヘッダ主体でテンプレート実体化が本体
#   (cgal.so に 2233 本。リンクでは直らない ⇒ #3535 ②)・openvdb は由来の切り分けが先 (③)。
#   ⇒ 直せる形になってから足す。いまの値を検査にしても「直せない赤」を常時出すだけになる。

# ==================================================================
# ★★ #3559: **CGAL の可変大域を定義している「像」を数える**。
#
# 上の check_globals は *モジュール .so* が自前で持っていないかを見る (許容 0)。だが
# **幾何ライブラリが何本あるか**は見ていない — 変種ごとに幾何 lib を建てていた頃は
# libsrava_cg / libsrava_nf_snc / libsrava_nf_hybrid / libsrava_nfcg の **4 本**が
# それぞれ CGAL の可変大域を持っていた (2026-09-19 実測)。
# ⚠⚠ ELF では @u@ (STB_GNU_UNIQUE) をローダがプロセスに 1 個だけ作るので **実行時には
#   1 個に見える**。だから「ELF で動いている」は何の証拠にもならない。**PE には畳む機構が
#   無い** (box 実測: 局所シンボル @d@ のまま像ごとに 1 個) ので、そちらでは本当に複数になる。
#   ⇒ ここで数えるのは *実行時のコピー数* ではなく **定義を持つ像の数**。これは
#     どの OS でも同じ静的事実で、PE で壊れるかどうかを Linux でも先に言える。
# ★ 許容は **名前で挙げる** (数ではなく集合)。数だけだと「1 本減って 1 本増えた」を見逃す。
ALLOW_IMAGES="libsrava_cg libsrava_vdcg"
#   libsrava_cg   … 幾何クラス (cgMesh3D / cgMesh2D / nfMesh / nfMeshSnc) の唯一の置き場所。
#   libsrava_vdcg … openvdb_cg 橋の厳密計算 (vdcgExact.cpp)。**橋固有のロジック**なので
#                   libsrava_cg には入れない (ひさ判断 2026-09-15: 「openvdb とも共有して
#                   いるわけですしね。やめておきましょう」)。⇒ #3559 の対象外。

echo "--- CGAL の可変大域を定義している像 (#3559)"
img_bad=0; img_ctl=0
for _f in "$D"/*.so "$D"/*.dylib "$D"/*.dll; do
	[ -f "$_f" ] || continue
	_b=$(basename "$_f"); _b=${_b%.*}
	_n=$(nm "$_f" 2>/dev/null | awk 'NF>=2 && $(NF-1) !~ /^U$/ {print $NF}' | c++filt |
	     grep -cE 'get_default_random\(\)::default_random|get_static_error_handler\(\)::_error_handler|get_static_error_behaviour\(\)::_error_behaviour|IO::Static::get_mode\(\)::mode|relative_precision_of_to_double_internal\(\)::')
	[ "$_n" = "0" ] && continue
	_ok=0
	for _a in $ALLOW_IMAGES; do [ "$_b" = "$_a" ] && _ok=1; done
	[ "$_b" = "libsrava_cg" ] && img_ctl=1
	if [ "$_ok" = "1" ]; then
		echo "      $_b: $_n 本 (許容されている像)"
	else
		echo "      $_b: $_n 本  ← **許容されていない**"
		img_bad=$((img_bad+1))
	fi
done
# ★★ 陽性対照: libsrava_cg は持っていて **正しい**。そこが 0 なら数え方の方が壊れている。
if [ "$img_ctl" = "0" ]; then
	echo "      ⚠ 陽性対照 (libsrava_cg) が当たらない ⇒ **数え方か木の方を疑う**"
	echo "        (まだ建てていない木では像が 1 つも無いのでこうなる)"
else
	if [ "$img_bad" != "0" ]; then
		echo "FAIL: CGAL の可変大域を持つ像が **許容の外に $img_bad 本**ある"
		echo "  ⇒ ELF は @u@ で畳むので実行時には見えないが、**PE では像ごとに別の実体**になる"
		echo "  ⇒ 直し方 (#3559 の型): その .so の CGAL を引く TU を libsrava_cg へ寄せ、"
		echo "     変種はライブラリではなく **型** で分ける"
		fails=$((fails+1))
	fi
fi

if [ "$fails" != "0" ]; then
	echo "FAIL: 上流のコピーが増えた箇所が $fails 件"
	exit 1
fi
echo "UPSTREAM-OK コピーは 1 つ"
