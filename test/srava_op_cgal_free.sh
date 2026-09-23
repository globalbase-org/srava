#!/bin/sh
# ★★ #3545 段 3: **op の TU が上流 (CGAL) を引いていないことを数える**。$1 = build ディレクトリ。
#
# ---- 何を見ているか ----
# #3545 は「幾何ライブラリの境界を **型で**引く」。op は幾何クラス (nfMesh / cgMesh) の
# *不透明な* 顔だけを見て、CGAL のヘッダは幾何 lib の TU だけが読む — という形にする。
# ★★ この形が成立しているかは **op の TU の .o に CGAL が emit されているか**でしか測れない。
#   CGAL は header-only (Debian にコンパイル済み libCGAL は無い) なので、**カーネルのヘッダを
#   1 枚 include しただけ**で関数内 static や テンプレートの実体が その TU の .o に出る
#   (2026-09-16 実測・実ビルド行で。何も呼ばない TU で):
#
#     CGAL/Exact_predicates_exact_constructions_kernel.h    可変大域 2 ・ CGAL:: 42
#     CGAL/Exact_predicates_inexact_constructions_kernel.h  可変大域 2 ・ CGAL:: 42
#     CGAL/Nef_polyhedron_3.h  (カーネルを引く)             可変大域 2 ・ CGAL:: 52
#     CGAL/Surface_mesh.h / Polygon_2.h / Polygon_with_holes_2.h / IO/Color.h /
#     assertions.h / number_utils.h                         いずれも **0**
#
#   ⇒ 「呼んでいるか」でも「@CGAL::@ と書いてあるか」でもなく、**.o に出たか**が実体。
#
# ⚠⚠ **字面で数えると外す** (2026-09-16 に踏んだ): nef の op TU で CGAL 型を触るのは 3 本だと
#   数えたが、実際は @nfTriSink.h@ を **9 本の op TU** が include し、そこが CGAL 型を
#   持っていた。9 本はどれも @CGAL::@ とも @Mesh@ とも書いていない (**推移的**な取り込み)。
#   ⇒ この検査は .o だけを見る。ソースは一切読まない。
#
# ---- ⚠ 何を **見ないか** (陰性対照で分かったこと・2026-09-16) ----
# 検定のために nef の op TU に CGAL の include を 1 行植えて、赤くなるかを見た。
#   @#include <CGAL/Surface_mesh.h>@                             … **0 本のまま・緑**
#   @#include <CGAL/Exact_predicates_exact_constructions_kernel.h>@ … 42 本・**赤** (犯人も出た)
# ⇒ この検査が見ているのは「include したか」ではなく **「.o に実体が出たか」**。
#   実体が出ないなら 2 コピーも起きないので、見逃しではなく *定義どおり* — ただし
#   **「op に CGAL の include が 1 行も無いこと」は保証しない**。そこは人が見る。
# ⚠⚠ #3545 段 1 の作業メモに「@Surface_mesh.h@ 単独で 2 本」とあるのは **誤り**
#   (他の CGAL ヘッダと一緒に読んだときの値だった)。⇒ 上の表に置き換えた。
#   ★ 「*使うと* 実体化する」(#3535② の旧説) も「*include すると* 実体化する」も一段荒く、
#     正しくは **どのヘッダ (カーネルか否か) を読んだか**で決まる。
#
# ---- ⚠⚠ 対象の .o を **glob で拾ってはいけない** ----
# @CMakeFiles/<target>.dir/**/*.o@ には **もう리リンクされていない古い .o が残る**。
#   実例 (2026-09-16・bench): @nef_snc.dir@ に 09-07 の @nfMesh.cpp.o@ (154MB) が残っており、
#   glob で数えると「op TU 44 本中 4 本が CGAL を引く」と出た。**正しくは 40 本中 0 本**。
#   @cgal.dir@ にも @cgaVoxelize.cpp.o@ (openvdb_cg へ移した) が残っていて 63/64 に見えた。
#   **正しくは 62/63**。
# ⇒ 対象は **リンカに実際に渡した並び**から取る。*ローダが見るもの*と同じ材料。
#
# ---- ★★ 生成器ごとに「渡した並び」の在り処が違う (2026-09-16・simu01 報告で追加) ----
#   Makefile 生成器  @CMakeFiles/<t>.dir/link.txt@   … リンカのコマンドライン そのもの
#   Ninja 生成器     @build.ninja@ の **link edge**   … 同じ並びが build 文の *入力* に並ぶ
#   ⇒ 両方を見る。⚠ **黙って glob へ落ちない** (落ちると上の古い .o を数え直すことになる)。
# ★★ 2026-09-16 夕: **実物の build.ninja で検証済み** (simu01 に ninja が入ったため)。
#   `cmake -G Ninja` で建てた木と Makefile 生成器の木で、8 ターゲット
#   (cgal / nef_snc / nef_hybrid / nef_cg / nef_mf / openvdb_cg / libsrava_cg / libsrava_nf_snc)
#   の **.o の並びが完全に一致**した。⇒ 「合成した治具で確かめただけ」の但し書きは外せる。
#   ⚠ 最初の突き合わせは **両側とも 0 本**で「一致」と出た (関数の切り出しに失敗していた)。
#     ⇒ 一致を見るときは **片側が 0 でないこと**を先に確かめる。0 == 0 は一致ではない。
# ⚠⚠ この検査は 2026-09-16 に Windows (box・Ninja 生成器) で
#   @OPFREE-SKIP link.txt が無い@ → **Passed** として 1 度も検定せずに緑だった。
#   ⇒ 段 2/4 の成果が Windows で未検証のまま「緑」に見えていた。**SKIP は Passed にしない**
#     (CMakeLists 側で SKIP_RETURN_CODE / SKIP_REGULAR_EXPRESSION を付けた)。
# ⚠ 拡張子も OS で違う: 共有ライブラリ @.so / .dylib / .dll@ ・ オブジェクト @.o / .obj@。
#   名前を 1 つに決め打つと、**建っていない扱いで静かに全部 skip される** (= 上と同じ事故)。
#
# ---- ⚠ nm の型欄: ここでは @u@ を **除かない** ----
# @test/srava_upstream_copies.sh@ は @$(NF-1) !~ /^[Uu]$/@ で数えるが、あれは *.so* を見る
# 検査なので正しい (@u@ = STB_GNU_UNIQUE はローダがプロセスに 1 個だけ作る = コピーではない)。
# ⚠⚠ **.o に同じ式を使うと確実に 0 になる**: CGAL の可変大域は .o では *まさに* @u@ で出る
#   (実測 @cgMesh3D.cpp.o@: @u CGAL::IO::Static::get_mode()::mode@)。
#   ⇒ ここで除くのは **@U@ (未定義) だけ**。詳細は srava_upstream_copies.sh の ④。
#
# ---- ★ 2 つの列を出す (両 OS で同じ量が動くように) ----
#   ① CGAL の**可変大域**を持つ TU の本数 … #3535② の危険そのもの。⚠ mac では元から 0 なので
#      この列だけ見ていると **mac ではこの変更の効果が丸ごと見えない** (macMINI 指摘 2026-09-16)。
#   ② **@CGAL::@ の定義**を持つ TU の本数 … 「その TU が CGAL を読んだか」。OS に依らない。
#   ⇒ 柵は両方に掛ける。①が 0 の OS でも ②が退行を止める。
D="${1:?build dir not given}"

# ★★ SKIP は **Passed にしない** (2026-09-16・box で 1 度も検定せず緑だった)。
#   ctest 側は SKIP_RETURN_CODE 77 / SKIP_REGULAR_EXPRESSION で「Skipped」と表示する。
SKIPRC=77
skip() { echo "OPFREE-SKIP $1"; exit "$SKIPRC"; }

# ★★ #3522: ハングの番犬 (共通)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

command -v nm      >/dev/null 2>&1 || skip "nm が無い"
command -v c++filt >/dev/null 2>&1 || skip "c++filt が無い"
ls "$D"/CMakeFiles/*/link.txt >/dev/null 2>&1 || [ -f "$D/build.ninja" ] || \
	skip "リンクの並びが読めない (link.txt も build.ninja も無い)"

fails=0

# CGAL の可変大域 (名前は srava_upstream_globals.sh / srava_upstream_copies.sh と同じ手書きリスト)。
GLOBALS='get_default_random\(\)::default_random|get_static_error_handler\(\)::_error_handler|get_static_error_behaviour\(\)::_error_behaviour|IO::Static::get_mode\(\)::mode|relative_precision_of_to_double_internal\(\)::'

# ★★ #3545 段 5 (2026-09-18): **上流ごとに一般化する**。
#   ⚠ それまでこの検査は CGAL 専用で、他の上流 (openvdb / occt / geogram / manifold / cherchi) は
#     **一度も数えられていなかった**。⇒ 2026-09-18 に測ったら、害の基準では全部 0 だった:
#
#       上流の **可変大域** を持つ op TU   6 モジュールすべて **0 本**
#       上流の **コード実体** を持つ op TU  openvdb 30/30 ・ occt 30/61 ・ manifold 1/46 ・ 他 0
#
#   ★ この 2 つは **別の量**で、守るべきは前者:
#       可変大域   = *大域状態が二重になる* ⇒ 正しさの問題 (CGAL の乱数種・エラーハンドラ等)
#       コード実体 = 同じ関数の複製 ⇒ サイズと起動コストの問題。**正しさは壊れない**
#     ⇒ #3539 の原則「サイズのためだけに触らない」に従い、**柵は可変大域に掛ける**。
#   ⚠ openvdb / occt のコード実体は *残したまま* にしてある。触るなら先に起動コストを測ること。
#
#   上流の名前 (デマングル後に当たる綴り)。⚠ 新しいカーネルを足したらここにも足す
#     — 足し忘れは「検査 0 本で緑」になる形なので、下の check_up が **名前の無いモジュールを
#       名指しで飛ばす** ようにしてある。
upstream_of() {
	case "$1" in
	cgal|nef_snc|nef_hybrid|nef_cg|nef_mf|openvdb_cg) echo 'CGAL::' ;;
	openvdb)   echo 'openvdb::' ;;
	geogram)   echo 'GEO::' ;;
	manifold)  echo 'manifold::' ;;
	cherchi)   echo 'cinolib::' ;;
	occt)      echo '^(BRep|TopoDS|Geom|gp_|Standard_|TCollection|Poly_|TopAbs|Message_|NCollection|TopTools|BOPAlgo|HLR|opencascade)' ;;
	*)         echo '' ;;
	esac
}

# count_up <名前> <上流の綴り> → "<上流の可変大域を持つ TU 数> <全 TU 数>"
#   ⚠⚠ 数えるのは **定義された可変大域** = nm の型 @D B d b@ に加えて **@u@ (unique global)**。
#     ★ CGAL の可変大域は *inline 関数の局所 static* なので @u@ で出る
#       (@u CGAL::get_default_random()::default_random@)。⇒ @u@ を落とすと **陽性対照が 0 になり、
#       検出器が死んでいるのに緑になる**。2026-09-18 に実際にそうなって気づいた。
#       ⇒ ファイル冒頭の「nm の型欄: ここでは u を除かない」はこの数え方にも効く。
#     ⚠ 読み取り専用 (R/r) は数えない — 複製されても状態を持たないので正しさは壊れない。
#   ⚠⚠ さらに **vtable / typeinfo / guard variable を除く**。これらは型 d で出るが *状態ではない*
#     (リンカが COMDAT で 1 つに畳む・複製されても壊れない)。
#     ★ 除かないと嘘の赤が出る: openvdb の op TU は @tbb::task_arena_function<...>@ の vtable を
#       31 本中 28 本が持っており、除く前の数え方だと「上流の可変大域 28 本」と読めてしまった
#       (2026-09-18 に実際にそう出た)。⇒ 守りたいのは **大域状態**であって複製ではない。
#   ⚠ nm は複数ファイルを渡すと「空行 + 『パス:』の見出し」を挟む (上の count_tus と同じ作法)。
count_up() {
	_ex=""; _n=0; _single=""
	for _o in $(objs_of "$1"); do
		[ -f "$D/$_o" ] || continue
		_ex="$_ex $_o"; _n=$((_n+1)); _single="$_o"
	done
	[ "$_n" = "0" ] && { echo "0 0"; return 0; }
	[ "$_n" = "1" ] || _single=""
	set -- $( ( cd "$D" && nm $_ex 2>/dev/null ) |
		awk -v single="$_single" '
			BEGIN { f = single }
			NF==1 && /:$/ { f = substr($0, 1, length($0)-1); next }
			NF>=2 && $(NF-1) ~ /^[DBdbuVv]$/ && f != "" { print f "\t" $NF }' |
		c++filt |
		awk -F'\t' -v u="$2" '
			$2 ~ /^(vtable|VTT|typeinfo|typeinfo name|construction vtable|guard variable) for /  { next }
			$2 ~ u { A[$1]=1 }
			END { na=0; for (k in A) na++; print na }' )
	echo "${1:-0} $_n"
}

# defs <.o> → その .o が **定義している** シンボルのデマングル名 (⚠ 除くのは U だけ・上の注記)
#   ★ 1 本だけ見たいとき用。**数えるときは使わない** (下の count_tus を見ること)。
defs() {
	nm "$1" 2>/dev/null | awk 'NF>=2 && $(NF-1) !~ /^U$/ {print $NF}' | c++filt
}

# ---- リンクされた .o をどこから取るか ----
# ⚠ target 名は共有ライブラリ名と一致しない (cgal / nef_snc はそのまま・他は <name>_module)
#   ので、**「その名前を吐くリンク行」** を探す。配線を変えても追随する。
LIBEXTS=".so .dylib .dll"

# ninja_cache — build.ninja を **1 回だけ**読み、「出力の basename <TAB> .o」の表を作る。
# ★ ninja の build 文は `build <出力…>: <ルール> <入力…> | <暗黙> || <順序>`。
#   ⚠ 行は ` $` で折り返される ⇒ **畳んでから**読む。
#   ⚠ Windows では 1 つの edge が **2 つ出力する** (foo.dll と 実装ライブラリ foo.dll.a)
#     ⇒ 出力は 1 つ目だけでなく **全部**見る。
#   ⚠ 出力にはディレクトリが付く (modules/cgal/cgal.so) ⇒ basename で照合する。
# ⚠⚠ **1 回だけ**にする理由 (2026-09-16 夜・box 実測): 以前は objs_of が
#   「拡張子 3 つ × ターゲット 9 本」で **最大 27 回** 2.3MB の build.ninja を読み直していた。
#   Linux では 1.6 秒だったが **Windows では 176 秒** (100 倍)。MSYS はプロセス起動と I/O が
#   高いので、Linux で無視できる無駄がそのまま出る。⇒ 表を 1 回作って引く形にした。
_nj_cache=""
ninja_cache() {
	[ -n "$_nj_cache" ] && return 0
	[ -f "$D/build.ninja" ] || return 1
	_nj_cache="${TMPDIR:-/tmp}/srava-opfree-nj-$$"
	awk '
	function flush(   i, sep, outs, rest, n, parts, tok, j, m, outn, outs_a, objs, nobj) {
		if ( buf == "" ) return
		if ( buf !~ /^build / ) { buf = ""; return }
		sep = 0
		for ( i = 7 ; i < length(buf) ; i++ )
			if ( substr(buf,i,2) == ": " && substr(buf,i-1,1) != "$" ) { sep = i; break }
		if ( sep == 0 ) { buf = ""; return }
		outs = substr(buf, 7, sep - 7)
		rest = substr(buf, sep + 2)
		nobj = 0
		n = split(rest, parts, /[ \t]+/)
		for ( i = 2 ; i <= n ; i++ ) {          # 1 番目はルール名
			tok = parts[i]
			if ( tok == "|" || tok == "||" ) break
			if ( tok ~ /\.(o|obj)$/ ) objs[++nobj] = tok
		}
		if ( nobj > 0 ) {
			outn = split(outs, outs_a, /[ \t]+/)
			for ( j = 1 ; j <= outn ; j++ ) {
				tok = outs_a[j]; sub(/^.*\//, "", tok)
				if ( tok == "" ) continue
				for ( m = 1 ; m <= nobj ; m++ ) print tok "\t" objs[m]
			}
		}
		buf = ""
	}
	{
		line = $0
		if ( cont ) { sub(/^[ \t]+/, "", line); buf = buf " " line }
		else        { buf = line }
		if ( line ~ /\$$/ ) { sub(/\$$/, "", buf); cont = 1; next }
		cont = 0
		flush()
	}
	END { flush() }
	' "$D/build.ninja" > "$_nj_cache" 2>/dev/null
	trap 'rm -f "$_nj_cache"' EXIT
	[ -s "$_nj_cache" ]
}

# objs_of <base> → リンクされた .o/.obj の並び (build dir からの相対)。見つからなければ 1。
# ★ <base> は拡張子なし (cgal / nef_snc / libsrava_cg)。OS ごとの拡張子はここで吸う。
# いまの構成に <base> (または <base>_module) の target が在るか。
#   在り処は CMakeFiles/TargetDirectories.txt (configure ごとに再生成 = 置き土産に載らない)。
#   ⚠ 出力名と target 名は一致しない (OUTPUT nef_cg の target は nef_cg_module)。両方見る。
ts_target_exists() {
	_td="$D/CMakeFiles/TargetDirectories.txt"
	if [ -f "$_td" ]; then
		# ⚠ 渡ってくるのは **ファイルの基底名** (libsrava_cg) で、target 名は srava_cg。
		#   先頭の lib を落とした綴りも見る。module は <名前>_module のことがある。
		_b1="$1"; _b2="${1#lib}"
		grep -qE "/($_b1|${_b1}_module|$_b2|${_b2}_module)\.dir/?$" "$_td" && return 0
		return 1
	fi
	# TargetDirectories.txt が無い生成器 — 成果物の実在で代替する (置き土産は拾いうる)
	for _e2 in $LIBEXTS; do
		[ -f "$D/$1$_e2" ] && return 0
	done
	return 1
}

objs_of() {
	for _e in $LIBEXTS; do
		_n="$1$_e"
		# ★★ 2026-09-19: **いまの構成に在る target か**を先に確かめる。
		#   CMakeFiles/<t>.dir/ は option を OFF にしても**消えない**ので、link.txt も .o も
		#   .so も残る。⇒ 「link.txt が在る = 建っている」と読むと、**無効化した構成の
		#   置き土産を監査**してしまう。実例 (2026-09-19・simu01):
		#     SRAVA_MODULE_NEF_SNC=OFF の木に nef_snc.dir/link.txt が 08-30 のまま残っており、
		#     **段 1 で分割する前**の並び (nfMesh.cpp.o 等 4 本がまだモジュール側に居た頃) を
		#     数えて「op TU 24/25 が CGAL を引く」と赤になった。ON で建て直したら **0/40**。
		#     製品ではなく死骸を見ていた。
		#   ⚠ nef_cg / nef_mf が正しく飛んでいたのは、**この木で一度も建てたことが無く**
		#     .dir ごと存在しなかったからで、扱いに差が在ってはいけない。
		#   ★ 権威は @CMakeFiles/TargetDirectories.txt@ — **configure のたびに作り直される**ので
		#     置き土産に引きずられない。Makefile 生成器と Ninja 生成器の**どちらでも出る**
		#     (2026-09-19 に simu01 と box の両方で確認)。無い生成器に備えて成果物の実在で代替する。
		if ! ts_target_exists "$1"; then continue; fi
		# (a) Makefile 生成器
		_lt=$(grep -l -- " -o $_n " "$D"/CMakeFiles/*/link.txt 2>/dev/null | head -1)
		if [ -n "$_lt" ]; then
			sed 's/ /\n/g' "$_lt" | grep -E '\.(o|obj)$'
			return 0
		fi
		# (b) Ninja 生成器 — 表は 1 回だけ作る (上の ⚠⚠)
		if ninja_cache; then
			_no=$(awk -F'\t' -v w="$_n" '$1==w{print $2}' "$_nj_cache")
			if [ -n "$_no" ]; then printf '%s\n' "$_no"; return 0; fi
		fi
	done
	return 1
}

# count_tus <name.so> → "<可変大域を持つ TU 数> <CGAL:: を持つ TU 数> <TU 総数>"
# ⚠⚠ **1 本ずつ nm を起こさない** (2026-09-16 夜・box 実測)。以前は .o ごとに
#   nm + awk + c++filt + grep x2 = **5 プロセス**を起こしており、177 本で **約 900 プロセス**。
#   Linux では 1.6 秒だが **Windows では 176 秒** (100 倍)。MSYS はプロセス起動が高いので、
#   Linux で無視できる無駄がそのまま出る。⇒ **nm と c++filt を 1 ターゲットに 1 回ずつ**にした。
# ⚠ nm は **複数ファイルを渡すと「空行 + 『パス:』の見出し」**を挟む。⚠ ただし
#   **1 ファイルだけだと見出しを出さない** ので、そのときは名前を外から与える。
#   (`nm -A` なら常に接頭辞が付くが、**パスに : が入る環境**で壊れるので採らない)
count_tus() {
	_ex=""; _n=0; _single=""
	for _o in $(objs_of "$1"); do
		[ -f "$D/$_o" ] || continue
		_ex="$_ex $_o"; _n=$((_n+1)); _single="$_o"
	done
	[ "$_n" = "0" ] && { echo "0 0 0"; return 0; }
	[ "$_n" = "1" ] || _single=""
	set -- $( ( cd "$D" && nm $_ex 2>/dev/null ) |
		awk -v single="$_single" '
			BEGIN { f = single }
			NF==1 && /:$/ { f = substr($0, 1, length($0)-1); next }
			NF>=2 && $(NF-1) !~ /^U$/ && f != "" { print f "\t" $NF }' |
		c++filt |
		awk -F'\t' -v g="$GLOBALS" '
			$2 ~ g        { A[$1]=1 }
			$2 ~ /CGAL::/ { B[$1]=1 }
			END { na=0; for (k in A) na++; nb=0; for (k in B) nb++; print na, nb }' )
	echo "${1:-0} ${2:-0} $_n"
}

# ★★ 何本を **実際に検べたか** を数える (2026-09-16・macMINI 指摘)。
#   ⚠ モジュール単位の「建っていない」は テスト全体の終了コードに出ない。⇒ 既定構成
#     (@SRAVA_MODULE_NEF_SNC=OFF@) では nef_snc / nef_cg / nef_mf の **3 本が黙って飛ぶ**のに
#     OPFREE-OK と出ていた。**0 本検べても緑**になりうる = SKIP を Passed にするのと同じ穴。
#   ⇒ 検べた本数と飛ばした本数を必ず出し、**0 本なら Skipped** にする。
#   ★ 「飛ばした」自体は正しいことがある (その構成では建たない) ので赤にはしない。
#     読む人が *何本を根拠に緑と言っているか* を見分けられれば足りる。
checked=0
skipped=""

# check <name> <可変大域の許容 TU 数> <CGAL:: の許容 TU 数> <なぜその数か>
check() {
	if ! objs_of "$1" >/dev/null 2>&1; then
		echo "      skip $1 (いまの構成に target が無い = 対象外)"
		skipped="$skipped $1"; return 0
	fi
	checked=$((checked+1))
	set -- "$1" "$2" "$3" "$4" $(count_tus "$1")
	_g=$5; _c=$6; _n=$7
	echo "      $1: 可変大域を持つ op TU $_g (許容 $2) ・ CGAL:: を持つ op TU $_c (許容 $3) / 全 $_n 本   # $4"
	_bad=0
	[ "$_g" -gt "$2" ] && _bad=1
	[ "$_c" -gt "$3" ] && _bad=1
	if [ "$_bad" != "0" ]; then
		echo "FAIL: $1 の op TU が上流 (CGAL) を引いている本数が許容を超えた"
		echo "  ⇒ CGAL は header-only なので **カーネルのヘッダを 1 枚 include しただけ**で .o に実体が出る"
		echo "     (呼んでいなくても出る。字面で探しても見つからない — 推移的な取り込みが典型)"
		echo "  ⇒ 直し方 (#3545 の型): 公開ヘッダから CGAL の include と typedef を落とし、"
		echo "     CGAL 型は不透明な箱にして、上流を触る自由関数は幾何 lib 側の唯一のヘッダへ移す"
		echo "     (nef の先例: modules/nef/h/nf/c++/nfMeshCgal.h)"
		echo "  ⇒ 犯人の TU:"
		for _o in $(objs_of "$1"); do
			[ -f "$D/$_o" ] || continue
			defs "$D/$_o" | grep -q 'CGAL::' && echo "       $(basename "$_o")"
		done | head -20
		fails=$((fails+1))
	fi
}

# ---- ★★ 陽性対照: 検出器が **当たることを見てから** 0 を報告する ----
# ⚠ 「0 件でした」は *何も見せない* — フィルタが壊れていても 0 になる (2026-09-15 に mac で
#   @grep -P@ が無く「検出 0 件」で緑になった実例がある)。
# ⇒ 幾何 lib の TU は **CGAL を引いていて正しい**。そこが 0 に見えたら検出器の方が壊れている。
echo "--- 陽性対照 (幾何 lib の TU は CGAL を引いていて正しい)"
ctl_hit=0; ctl_found=0
# ★ #3559: 幾何 lib は **libsrava_cg 1 本**になった (旧 libsrava_nf_snc / _hybrid / _nfcg を畳んだ)。
for _l in libsrava_cg; do
	objs_of "$_l" >/dev/null 2>&1 || continue
	ctl_found=1
	# ⚠⚠ **値の分割だけ glob を止める** (2026-09-22)。
	#   `set -- $(…)` の $( ) は **クォートできない** (単語分割が要る) のでパス名展開に掛かる。
	#   比べる値は `[1,1,0]` 形 = glob の **文字クラス**なので、カレント (ビルド dir) に `1` という
	#   名前のファイルが 1 つあるだけで `1` に化け、**「値が違う」という、それらしい文言で赤くなる**
	#   (2026-09-21 に実際に踏んだ。⚠ PIG_TIMING は *ファイルパス* を取る env なので
	#   `PIG_TIMING=1` と打つと `1` というファイルができる)。
	#   ★★ **実行と分割を分ける**のが肝。`set -f; set -- $(cmd)` と囲うと -f が $( ) の中まで効き、
	#     cmd がシェル関数だと *その中の glob* まで殺す (実測: count_tus → objs_of の
	#     `CMakeFiles/*/link.txt` が読めず「全 0 本」になった)。⇒ 先に変数へ取ってから分割する。
	_SPLIT_=$(count_tus "$_l")
	set -f
	set -- $_SPLIT_
	set +f
	echo "      $_l: 可変大域 $1 ・ CGAL:: $2 / 全 $3 本"
	[ "$2" -gt 0 ] && ctl_hit=1
done
if [ "$ctl_found" = "0" ]; then
	skip "幾何 lib が建っていない (陽性対照が取れない)"
fi
if [ "$ctl_hit" = "0" ]; then
	echo "FAIL: 陽性対照が当たらない — **検出器の方が壊れている**"
	echo "  ⇒ 幾何 lib の TU は CGAL を引いているはずで、0 はありえない"
	echo "  ★ ただし **全 0 本** と出ているなら、まず疑うのは検出器ではなく **木**:"
	echo "     その build dir を **まだ建てていない** (configure しただけ) と、リンクの並びは"
	echo "     読めるのに .o が 1 つも無いのでこうなる (2026-09-16 に -G Ninja の木で実測)。"
	echo "  ⇒ 全 0 本でないなら nm / c++filt / awk の型欄 (\$(NF-1)) を疑う。.o では定義は u で出る"
	exit 1
fi

echo "--- op TU が上流 (CGAL) を引いている本数 (#3545 段 3)"
# ★★ nef — 段 1 (2026-09-16) で **0 本**にした。ここが 0 でなくなったら段 1 が崩れている。
check nef_snc         0 0 "段 1 済み"
check nef_hybrid   0 0 "段 1 済み (snc と同一ソース)"
# ★★ cgal — 段 2 (2026-09-16) で **62 → 0**。cgMesh.h から CGAL を落とし、op は
#   素の型で受ける入口 (build_from_triangles / build_geodesic / add_region_ring /
#   add_regions_from_rings / add_guide / copy_contents_from / cg_estimate_normals) を
#   通すようになった。⇒ ここが 0 でなくなったら段 2 が崩れている。
check cgal               0 0 "段 2 済み"
# ★★ 橋 (nef_cg / nef_mf / openvdb_cg) — 段 4 (2026-09-16) で **0**。
#   ⚠⚠ 段 3 の初版はここを「0 にはできない」と書いて *いまの値で止める柵* にしていた。
#     **それは誤りだった** (ひさ指摘)。橋の op も、変換の実体を .so へ出せば 0 にできる:
#       nef_cg      → nfcBridge.cpp … 当時は両側の幾何 lib を同時に要るのでどちらにも置けず、
#                     橋が自分の .so (libsrava_nfcg) を持っていた。★ #3559 で 2 型とも
#                     libsrava_cg に入ったので、そこへ同居させて .so は畳んだ
#       nef_mf      → **新しい .so は要らなかった**。nef 自身が既に同じフレーミングを書いて
#                     いるので、@c nfMesh::to_exact_boundary_bytes@ を足すだけで済んだ
#       openvdb_cg  → vcaIsosurface は @c build_from_triangles (段 2 で作った口) で足り、
#                     vcaExportVox の厳密計算は libsrava_vdcg (vdcgExact.cpp) へ
#   ★ 「0 にできない」と思ったのは *柵で足りていた* からで、構造の限界ではなかった。
#     ⚠ 柵 (橋は HIDDEN で建てないので Linux では @u@ のまま) は **OS ごとに確かめ直しが要る**
#       — @u@ は GNU の拡張で mac / Windows には無い。段 4 はその依存を外した。
check nef_cg           0 0 "段 4 済み (変換は libsrava_cg・#3559 で旧 libsrava_nfcg から移動)"
check nef_mf           0 0 "段 4 済み (バイト列は nef 側が作る)"
check openvdb_cg   0 0 "段 4 済み (厳密幾何は libsrava_vdcg)"

# ==================================================================
# ★★ #3545 段 5: **上流ごとの柵** — どのモジュールの op TU にも *上流の可変大域* を持たせない
#   ⚠ ここが 0 である限り、コード実体が複製されていても **正しさは壊れない**
#     (壊れるのは大域状態が二重になったとき — geogram で実際に起きた・#3535①)。
# ==================================================================
echo "--- 上流ごとの柵 (op TU に上流の可変大域を置かない)"

# ★★ 専用の陽性対照 — この数え方が **当たることを見てから** 0 を報告する。
#   ⚠ 上の CGAL の陽性対照とは *数えている量が違う* (あちらは CGAL:: を含む全定義) ので、
#     こちらはこちらで取る。幾何 lib 側は上流の可変大域を **持っていて正しい**。
up_ctl=0
if objs_of libsrava_cg >/dev/null 2>&1; then
	_SPLIT_=$(count_up libsrava_cg 'CGAL::')
	set -f
	set -- $_SPLIT_
	set +f
	echo "      陽性対照 libsrava_cg: 上流の可変大域を持つ TU $1 / 全 $2 本"
	[ "$1" -gt 0 ] && up_ctl=1
	if [ "$up_ctl" = "0" ]; then
		echo "FAIL: 陽性対照が当たらない — **数え方の方が壊れている**"
		echo "  ⇒ libsrava_cg の TU は CGAL の可変大域 (乱数種・エラーハンドラ) を持つはずで、0 はありえない"
		fails=$((fails+1))
	fi
else
	echo "      ⚠ libsrava_cg が建っていないので陽性対照が取れない ⇒ 下の 0 は根拠が弱い"
fi

# ★★ 許容は **いまの実測値で固定する** (ラチェット)。0 でない 2 つは性質が違う:
#
#   openvdb  0   ★ 2026-09-18 に **30 → 0**。中身は @openvdb::math::Mat3/Mat4<double>::identity()::sIdentity@
#                = 単位行列の定数で *状態ではない* (複製されても壊れない) が、害の有無で柵を緩めると
#                次に *状態を持つ* 大域が混ざったとき区別が付かないので 0 にした。
#                手当ては 2 つ: @vdGrid.h@ から OpenVDB を外して不透明な箱へ /
#                @vdTriSink.h@ を **素の配列で受ける**形に (9 本の op TU が推移的に引いていた)。
#   occt    14   (2026-09-18 に 21 から) @opencascade::type_instance<Standard_Transient>::get()::anInstance@ 等 = **RTTI の
#                型インスタンス**。⚠ OCCT の @DownCast@ / @IsKind@ は型記述子を **アドレスで**比べるので、
#                像を跨いで複製されると壊れうる ([[cross-image-rtti-three-os]] / mac の hidden
#                visibility 事故と同じ家系)。★ Linux では @u@ (STB_GNU_UNIQUE) をローダが 1 つに
#                畳むので現に動いているが、**mac / Windows にその保証は無い**。
#                ⇒ #3545 の残件はここ。0 にできたら許容も 0 へ下げる。
#
# ⚠ 許容を「いまの値」にするのは *現状追認* に見えるが、そうではない — **増えたら赤**になる。
#   0 にできないものを赤のまま放置すると、赤が日常になって誰も見なくなる (それが本当の失敗)。
allow_of() {
	case "$1" in
	openvdb) echo 0 ;;   # ★ 2026-09-18 に 30 → 0 (段 5: vdGrid.h を不透明に + vdTriSink を素の配列へ)
	occt)    echo 14 ;;   # ★ 2026-09-18: 21 → 14 (生成系 7 本を幾何 lib へ移した)
	*)       echo 0  ;;
	esac
}

up_checked=0
for _m in cgal manifold geogram openvdb occt cherchi nef_snc nef_hybrid; do
	objs_of "$_m" >/dev/null 2>&1 || continue
	_u=$(upstream_of "$_m")
	if [ -z "$_u" ]; then
		echo "      ⚠ $_m: 上流の綴りが upstream_of に無い ⇒ **数えていない**"
		fails=$((fails+1)); continue
	fi
	_al=$(allow_of "$_m")
	_SPLIT_=$(count_up "$_m" "$_u")
	set -f
	set -- $_SPLIT_ "$_al"
	set +f
	up_checked=$((up_checked+1))
	echo "      $_m: 上流の可変大域を持つ op TU $1 (許容 $3) / 全 $2 本"
	if [ "$1" -lt "$3" ]; then
		echo "      ★ 許容 $3 より減っている ($1) ⇒ allow_of を下げること (ラチェットを締める)"
	fi
	if [ "$1" -gt "$3" ]; then
		echo "FAIL: $_m の op TU が **上流の可変大域** を持っている"
		echo "  ⇒ 大域状態がモジュールごとに二重になる (geogram で実際に起きた・#3535①)"
		echo "  ⇒ 直し方: その上流ヘッダを op TU から外し、幾何 lib 側の唯一のヘッダへ移す (#3545 の型)"
		fails=$((fails+1))
	fi
done
[ "$up_checked" -gt 0 ] || echo "      ⚠ 上流の柵は 0 本しか検べていない"

echo "--- 検べた本数"
echo "      検査 $checked 本 ・ 飛ばし${skipped:- なし} ・ 上流の柵 $up_checked 本"
if [ "$fails" != "0" ]; then
	echo "FAIL: 許容を超えたモジュールが $fails 件"
	exit 1
fi
# ⚠⚠ **0 本なら緑と言わない**。陽性対照は通っているので検出器は生きているが、
#   それは「op TU が境界の内側にいる」の証拠には **1 つもなっていない**。
[ "$checked" -gt 0 ] || skip "対象のモジュールが 1 本も建っていない (検査 0 本)"
echo "OPFREE-OK op TU は境界の内側にいる (CGAL の検査 $checked 本 ・ 上流の柵 $up_checked 本)"
