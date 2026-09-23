#!/bin/sh
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
# install ツリーの回帰テスト (#3431 P0-a)。
# $1 = ビルドディレクトリ (cmake --install の対象)。$2 = staging prefix (作り直される)。
#
# ★ このテストの存在理由 (2026-08-18):
#   他の全テストは **ビルドツリーの .so と srava_agent を env で名指し**して走る
#   (CMakeLists 末尾が全テストへ SRAVA_MODULE_PATH を注入し、各テストが SRAVA_AGENT を渡す)。
#   つまり「install したツリーがそれ自身で動くか」を **1 本も検証していなかった**。
#   実際 2026-08-18 時点の install ツリーは素の env では動かず、
#     - モジュール探索路は configure 時の $PREFIX を焼き込んだ絶対パスと「実行体と同じ dir」だけ
#     - install レイアウトは bin/ と lib/srava/modules/ に分かれている
#   ため、**自分の兄弟の .so を一切見ず**にその機械の /usr/local にある別世代の install を読み、
#   「planner と agent の版が違います」で落ちていた (#3431 で実行体相対の解決を入れて修正)。
#
# 見ているもの:
#   ① install した prefix を **別の場所へ置いても** (= configure 時の prefix と違っても)
#      srava が自分の兄弟のモジュールと srava_agent を見つけて動く
#   ② 外部依存を持つモジュールが in-proc (thread) と別プロセス (process) の **両方**で動く
#      → これが #3431 の完了条件そのもの
#   ③ install ツリーの答えがビルドツリーと一致する (RPATH で別の共有ライブラリを掴んでいない)
#   ④ install した .so に解決できない共有ライブラリ依存が無い (ldd がある環境のみ)
#
# ⚠ **env を必ず落とすこと**。ctest は全テストに SRAVA_MODULE_PATH を注入するので、
#   そのままだと install ツリーではなくビルドツリーの .so を読んでしまい、何も検証しなくなる。
BUILD="${1:?build dir not given}"
STAGE="${2:?stage prefix not given}"

MODDIR="$STAGE/lib/srava/modules"
BINDIR="$STAGE/bin"

# ---- ビルドツリー側の期待値を先に取る (env 明示 = 従来どおりの走らせ方) ----
BSRAVA="$BUILD/srava"
if [ ! -x "$BSRAVA" ]; then echo "FAIL: no srava in build dir $BUILD"; exit 1; fi

# 2 つの箱の union の体積。in-proc/process とカーネルを跨いで同じ値になることだけが要件。
PROG='var m = box(2,2,2) ||| box(1,1,3); print("VOL", volume(m));'

# ⚠ キャッシュ dir は **毎回消す**。残っていると全 HIT で agent が 1 度も起動せず、
#   「install した .so と srava_agent が動く」という肝心の部分を何も検証しないテストになる。
run_vol() {   # $1=srava $2=so名 $3=exec $4=cachedir   (env は呼び手が組む)
	rm -rf "$4"
	SRAVA_CACHE_DIR="$4" SRAVA_SOURCE="module(\"$2\",{priority:99,exec_default:\"$3\"}); $PROG" \
	  "$1" 2>&1 | sed -n 's/^VOL //p'
}

# ---- ★ install_manifest.txt を退避する (このテストの副作用を消す) ----
#   CMake の cmake_install.cmake は manifest の書き先を **build dir に直書き**していて、
#   --prefix を変えても常に同じ $BUILD/install_manifest.txt に書く。よって何もしないと
#   **ctest を回すだけで manifest が staging (installtest/) の内容に化け、実 install 先を
#   指さなくなる**。「install_manifest の新旧差分で入れ替え時の残骸を検出する」運用
#   (tinyState からの申し送り) が静かに壊れるので、前後で退避・復元する。
#   ★ 途中で exit する経路が多いので trap で必ず戻す。
MANIFEST="$BUILD/install_manifest.txt"
MANIFEST_BAK="$BUILD/install_manifest.txt.installtest-bak"
restore_manifest() {
	if [ -f "$MANIFEST_BAK" ]; then
		mv -f "$MANIFEST_BAK" "$MANIFEST" 2>/dev/null \
		  || echo "WARN: install_manifest.txt を復元できませんでした ($MANIFEST_BAK に残っています)"
	fi
}
trap restore_manifest EXIT
if [ -f "$MANIFEST" ]; then
	# ★ 書けない (別ユーザ所有) なら cmake --install 自体が最後に失敗する。先に見て分かる形で落とす。
	if [ ! -w "$MANIFEST" ]; then
		echo "FAIL: $MANIFEST に書けません (別ユーザ所有?)。"
		echo "      build dir から sudo cmake --install すると manifest が root 所有になり、"
		echo "      以後このテストが最後の manifest 書き込みだけで失敗します。chown で解消します。"
		exit 1
	fi
	cp -p "$MANIFEST" "$MANIFEST_BAK" || { echo "FAIL: manifest を退避できません"; exit 1; }
fi

# ---- install (staging prefix は毎回作り直す = 前回の残骸で通ってしまうのを防ぐ) ----
rm -rf "$STAGE"
if ! cmake --install "$BUILD" --prefix "$STAGE" >"$BUILD/install-test.log" 2>&1 ; then
	echo "FAIL: cmake --install failed"; tail -20 "$BUILD/install-test.log"; exit 1
fi
if [ ! -x "$BINDIR/srava" ] || [ ! -x "$BINDIR/srava_agent" ]; then
	echo "FAIL: install produced no bin/srava(+_agent)"; exit 1
fi

# ★ ここから先は install ツリーが**自力で**解決できることを見る。ctest が注入する
#   SRAVA_MODULE_PATH と、各テストが渡す SRAVA_AGENT を **落とす**。
unset SRAVA_MODULE_PATH
unset SRAVA_AGENT

# ---- ① 探索路: モジュールが staging prefix の下から読まれていること ----
REPORT=$("$BINDIR/srava" --modules 2>&1)
# ⚠ Windows(MSYS/MinGW): --modules の path は **区切りが混ざる**。exe 由来の前半が `\` で、
#   srava が組み立てた後半が `/` になるため
#   (例: C:\Users\joshu\...\installtest/lib/srava/modules/cgal.dll)。
#   MODDIR は全部 `/` なのでそのままでは一致せず、**モジュールは正しく読めているのに FAIL** になる。
#   → 突き合わせる前に両側を `/` へ正規化する (Linux/macOS では無変化)。
REPORT_N=$(echo "$REPORT" | tr '\\' '/')
MODDIR_N=$(echo "$MODDIR"  | tr '\\' '/')
LOADED=$(echo "$REPORT_N" | sed -n "s|^  \([a-z_0-9]*\) *[0-9-]* .*$MODDIR_N/.*|\1|p")
if [ -z "$LOADED" ]; then
	echo "FAIL: installed srava loaded no module from $MODDIR"
	echo "$REPORT"
	exit 1
fi
echo "INSTALL: loaded from prefix: $(echo $LOADED | tr '\n' ' ')"

# ---- ② + ③ 外部依存を持つモジュールを in-proc / process の両方で走らせる ----
#   manifold … 外部依存 (FetchContent の Manifold) を **静的 bundle**。thread/process 両対応
#   cgal     … 外部依存 (system の GMP/MPFR) を **共有リンク**。process 実行
#   nef_snc  … 同上 (CGAL Nef)。どちらも RPATH で libpig.so を辿る必要がある
NTEST=0
# ⚠ Windows(MinGW) のモジュールは **.dll**。ここを .so 決め打ちにすると
#   `[ -f "$MODDIR/$SO" ]` が全部外れ、**1 つも検証しないまま「install されていない」**で
#   落ちる (製品は正常なのにテストだけが赤くなる)。install 先を見て決める。
# ★★ 2026-09-18: 綴りは **ビルドツリー**で見る (⑤ の SHEXT と同じ規則に揃えた)。
#   install 先で決めていたので、**install が丸ごと失敗した機では既定 (.so) に落ちて**
#   ⑤ の期待が全部 .so で組まれ、「N 個足りない」に化けていた = 診断が真因を指さない。
#   ⚠⚠ **OS から決めてはいけない**: CMake の MODULE は macOS でも既定で .so
#     (.dylib になるのは SHARED だけ)。⇒ **実在するモジュールの綴り**を見る。
#     ⚠ 「ビルドツリーの任意の *.dylib」で決めると mac が libsrava_*.dylib を拾って外す。
MODEXT=
for _c in so dll dylib ; do
	for _n in manifold cgal nef_snc geogram occt points ; do
		if [ -f "$BUILD/$_n.$_c" ]; then MODEXT="$_c" ; break 2 ; fi
	done
done
if [ -z "$MODEXT" ]; then
	MODEXT=so
	echo "INSTALL: ⚠ ビルドツリー $BUILD にモジュールが 1 つも無い ⇒ 綴りは既定の .so とする"
fi
#   geogram  … 外部依存 (FetchContent の geogram) を **静的 bundle** + OpenMP (libgomp) を共有
#   occt     … 外部依存 (system の OCCT + TBB) を **共有リンク** (規約 C)。process 実行
for spec in "manifold.$MODEXT thread process" "cgal.$MODEXT process" "nef_snc.$MODEXT process" "geogram.$MODEXT process" "occt.$MODEXT process"; do
	set -- $spec
	SO="$1" ; shift
	# その .so が install されていなければ (option OFF ビルド) 黙って飛ばす。
	[ -f "$MODDIR/$SO" ] || continue
	for EXEC in "$@" ; do
		NTEST=$((NTEST+1))
		B=$(run_vol "$BSRAVA" "$SO" "$EXEC" "$BUILD/installtest-cache-b-$SO-$EXEC")
		I=$(run_vol "$BINDIR/srava" "$SO" "$EXEC" "$BUILD/installtest-cache-i-$SO-$EXEC")
		if [ -z "$B" ]; then echo "FAIL: build tree produced no volume ($SO/$EXEC)"; exit 1; fi
		if [ -z "$I" ]; then
			echo "FAIL: install tree produced no volume ($SO/$EXEC)"
			SRAVA_CACHE_DIR="$BUILD/installtest-cache-e" \
			  SRAVA_SOURCE="module(\"$SO\",{priority:99,exec_default:\"$EXEC\"}); $PROG" \
			  "$BINDIR/srava" 2>&1 | head -10
			exit 1
		fi
		ok=$(awk -v a="$B" -v b="$I" 'BEGIN{
			d=a-b; if(d<0)d=-d; s=(a<0?-a:a); if(s<1)s=1;
			print (d <= 1e-9*s) ? 1 : 0 }')
		if [ "$ok" != "1" ]; then
			echo "FAIL: volume mismatch ($SO/$EXEC) build=$B install=$I"; exit 1
		fi
		echo "INSTALL: $SO/$EXEC = $I (build tree $B)"
	done
done
if [ "$NTEST" = "0" ]; then
	echo "FAIL: no module with external dependencies was installed (nothing verified)"; exit 1
fi

# ---- ②' openvdb は **私物の共有ライブラリ (libtbb.so) を持ち込む唯一のモジュール**なので別枠 ----
#   上のループは使えない: openvdb は leaf 生成 op (box/sphere) を持たないので、priority を上げても
#   PROG の box はメッシュ系へ行き、openvdb は 1 度も起動しない (= 何も検証しないテストになる)。
#   voxelize を明示的に通して初めて openvdb.so が動く。
#   ★ここが本命: install ツリーの openvdb.so が $PREFIX/lib/libtbb.so を **自力で**解決できること。
#     解決できないと "agent closed before handshake" で死ぬ (規約 §7.3 の警告そのもの)。
if [ -f "$MODDIR/openvdb.$MODEXT" ]; then
	# ★ #3452 (module() 明示ロード) 追従: voxelize は **橋渡しモジュール** が担う。
	#   module/all.sra には入らない設計 (個別 opt-in) なので、ここで明示ロードする。
	VPROG='module("openvdb.'"$MODEXT"'",{}); module("openvdb_mf.'"$MODEXT"'",{}); var v = voxelize(box(2,2,2), 0.1); print("VOL", volume(v));'
	run_vox() {   # $1=srava $2=cachedir
		rm -rf "$2"
		SRAVA_CACHE_DIR="$2" SRAVA_SOURCE="module(\"manifold.$MODEXT\",{priority:99}); $VPROG" \
		  "$1" 2>&1 | sed -n 's/^VOL //p'
	}
	B=$(run_vox "$BSRAVA"       "$BUILD/installtest-cache-b-vdb")
	I=$(run_vox "$BINDIR/srava" "$BUILD/installtest-cache-i-vdb")
	if [ -z "$B" ]; then echo "FAIL: build tree produced no voxelized volume"; exit 1; fi
	if [ -z "$I" ]; then
		echo "FAIL: install tree produced no voxelized volume (libtbb.so を解決できていない疑い)"
		SRAVA_CACHE_DIR="$BUILD/installtest-cache-e-vdb" \
		  SRAVA_SOURCE="module(\"manifold.$MODEXT\",{priority:99}); $VPROG" \
		  "$BINDIR/srava" 2>&1 | head -10
		exit 1
	fi
	ok=$(awk -v a="$B" -v b="$I" 'BEGIN{
		d=a-b; if(d<0)d=-d; s=(a<0?-a:a); if(s<1)s=1;
		print (d <= 1e-9*s) ? 1 : 0 }')
	if [ "$ok" != "1" ]; then
		echo "FAIL: voxelized volume mismatch build=$B install=$I"; exit 1
	fi
	echo "INSTALL: openvdb.$MODEXT/voxelize = $I (build tree $B)"
fi

# ---- ④ 解決できない共有ライブラリ依存が無いこと (ldd のある環境のみ) ----
if command -v ldd >/dev/null 2>&1 ; then
	for f in "$MODDIR"/*.so "$MODDIR"/*.dll "$BINDIR"/srava "$BINDIR"/srava_agent ; do
		[ -e "$f" ] || continue
		MISSING=$(ldd "$f" 2>/dev/null | sed -n 's/^\t\(.*\) => not found$/\1/p')
		if [ -n "$MISSING" ]; then
			echo "FAIL: $f has unresolved shared library dependencies:"
			echo "$MISSING"
			exit 1
		fi
	done
	echo "INSTALL: ldd clean"
fi

# ==================================================================
# ---- ⑤ ★★ install ツリーの **在庫** を数える (OS ごとの期待一覧・2026-09-18) ----
#   ①〜④ はどれも「**動いたか**」しか見ていない。⇒ *余計に入った* / *足りない* は、
#   動きさえすれば通ってしまう。実際 ② のループは
#       [ -f "$MODDIR/$SO" ] || continue      ← option OFF ビルドのつもりの読み飛ばし
#   なので、**install に失敗したモジュールも黙って飛ばす**。NTEST=0 でしか気づけない。
#
# ★ 期待値は **この機械のビルドツリーから導く** (人が書いた一覧を突き合わせない)。
#   手書きの一覧は、モジュールが増減するたびに古くなり、しかも古くなったことが
#   *緑のまま* 進行する。⇒ 導けるものは導き、導けないものだけを表に置く。
#
# ⚠⚠ **OS から拡張子を決めないこと**。CMake の MODULE ライブラリは macOS でも既定で
#   @.so@ (@.dylib@ になるのは SHARED だけ)。「mac なら dylib」と書くと mac で
#   **1 つも照合せず素通り**する。⇒ 綴りは *実物を見て* 決める (②の MODEXT と同じ考え方)。
# ==================================================================
INV_FAIL=0

# ---- 綴りを実物から決める (MODULE と SHARED で違いうる) ----
case "$(uname -s 2>/dev/null)" in
	Darwin)               OSKEY=mac   ; SHEXT_EXP=dylib ;;
	MINGW*|MSYS*|CYGWIN*) OSKEY=win   ; SHEXT_EXP=dll   ;;
	*)                    OSKEY=linux ; SHEXT_EXP=so    ;;
esac
#   ⚠⚠ SHARED の綴りは **ビルドツリー**で見る。install 先を見てはいけない —
#     Windows は SHARED が RUNTIME 扱いで **bin/ に入り lib/ には来ない**ので、
#     $STAGE/lib を覗くと検出に失敗し、下の陽性対照が *製品は正常なのに* 発火する。
SHEXT=
for _c in so dylib dll ; do
	if [ -f "$BUILD/libpig.$_c" ]; then SHEXT="$_c" ; break ; fi
done
if [ -z "$SHEXT" ]; then SHEXT="$SHEXT_EXP" ; fi
#   ★ OS から予想した綴りと食い違ったら、それ自体が所見 (レイアウトが変わった合図)。
if [ "$SHEXT" != "$SHEXT_EXP" ]; then
	echo "INSTALL: ⚠ 共有ライブラリの綴りが OS の予想と違う ($OSKEY: 実物 .$SHEXT / 予想 .$SHEXT_EXP)"
fi
EXE=""
[ "$OSKEY" = win ] && EXE=".exe"

# ---- 期待の一覧を組む ----
EXPECT="$BUILD/installtest-expect.txt"
ACTUAL="$BUILD/installtest-actual.txt"
: >"$EXPECT"

#   (a) 実行体
echo "bin/srava$EXE"       >>"$EXPECT"
echo "bin/srava_agent$EXE" >>"$EXPECT"

#   (b) モジュール = ビルドツリーの module から **install しないもの**を引く
#   ⚠ 引く側は @srava_add_module(... INSTALL)@ を **書いていない** もの。増えたらここに足す。
#     ★ 足し忘れると *赤* になる (緑のまま古くなる向きではない) ので、失敗の向きが安全。
# ★★ 2026-09-19: **置き土産を数えない**。ビルドツリーの成果物を glob で拾うと、option を
#   OFF にした後も残る古い .so を「install されるはず」と数えて「N 個足りない」に化ける
#   (実例: build/ に 08-27 の nef_snc.so が残っており、SRAVA_MODULE_NEF_SNC=OFF なのに
#    期待へ入っていた)。⇒ **いまの構成に target が在るか**を権威にする。
#   在り処 = CMakeFiles/TargetDirectories.txt。**configure のたびに作り直される**ので
#   置き土産に載らない。Makefile 生成器 / Ninja 生成器の**どちらでも出る** (simu01 と box で確認)。
#   ⚠ 出力名と target 名は一致しない: OUTPUT nef_cg の target は nef_cg_module ・
#     libsrava_cg.so の target は srava_cg。先頭 lib を落とした綴りと _module 付きも見る。
#   ⚠ 無い生成器に当たったら **従来どおり** (glob をそのまま採る)。黙って 0 本にしない。
TDIRS="$BUILD/CMakeFiles/TargetDirectories.txt"
target_exists() {
	[ -f "$TDIRS" ] || return 0            # 権威が無い => 従来どおり採る
	_b1="$1" ; _b2="${1#lib}"
	grep -qE "/($_b1|${_b1}_module|$_b2|${_b2}_module)\.dir/?$" "$TDIRS"
}

NOINSTALL=" d2 d3 d4 d5 demo "
NMOD=0
for _f in "$BUILD"/*."$MODEXT" ; do
	[ -f "$_f" ] || continue
	_b=$(basename "$_f" ".$MODEXT")
	case "$_b" in libpig|libsrava_*|lib*) continue ;; esac
	target_exists "$_b" || continue          # 置き土産 (OFF にした option の残り) は数えない
	case "$NOINSTALL" in *" $_b "*) continue ;; esac
	echo "lib/srava/modules/$_b.$MODEXT" >>"$EXPECT"
	NMOD=$((NMOD+1))
done

#   (c) host 側の共有ライブラリ (libpig + libsrava_*)。⚠ Windows は RUNTIME ⇒ bin/ に入る
NLIB=0
for _f in "$BUILD"/libpig."$SHEXT" "$BUILD"/libsrava_*."$SHEXT" ; do
	[ -f "$_f" ] || continue
	_b=$(basename "$_f")
	_bn=$(basename "$_f" ".$SHEXT")
	target_exists "$_bn" || continue         # 同上 (libsrava_nf_snc.so 等の置き土産)
	if [ "$OSKEY" = win ]; then echo "bin/$_b" >>"$EXPECT" ; else echo "lib/$_b" >>"$EXPECT" ; fi
	NLIB=$((NLIB+1))
done

#   (d) share/ = ソースツリーの lib/ と srava2kwave/ の写し (install(DIRECTORY ...))
SRC="$(cd "$(dirname "$0")/.." && pwd)"
NSHARE=0
for _d in lib srava2kwave ; do
	[ -d "$SRC/$_d" ] || continue
	for _f in $(cd "$SRC/$_d" && find . -type f | sed 's|^\./||' | sort) ; do
		echo "share/srava/$_d/$_f" >>"$EXPECT"
		NSHARE=$((NSHARE+1))
	done
done

sort -o "$EXPECT" "$EXPECT"
#   ★ symlink も在庫のうち (libgeogram.so → libgeogram.so.1.10.0 等)
( cd "$STAGE" && find . \( -type f -o -type l \) | sed 's|^\./||' | sort ) >"$ACTUAL"

# ---- ★★ 陽性対照 — 検査に歯が在るか (空回りしていないか) ----
#   ⚠ 期待が空 / モジュール 0 / host lib 0 なら、以下の突き合わせは **何も言っていない**。
#     「差 0 件」は、比べる物が無くても出る。⇒ 先に中身が在ることを見る。
NEXP=$(wc -l <"$EXPECT" | tr -d ' ')
NACT=$(wc -l <"$ACTUAL" | tr -d ' ')
echo "INSTALL: 在庫 期待 $NEXP 本 (module $NMOD / lib $NLIB / share $NSHARE) ・ 実物 $NACT 本 [$OSKEY]"
if [ "$NMOD" -lt 1 ] || [ "$NLIB" -lt 1 ] || [ "$NSHARE" -lt 1 ] || [ "$NEXP" -lt 10 ]; then
	echo "FAIL: 在庫の期待値が組めていない (module=$NMOD lib=$NLIB share=$NSHARE 計=$NEXP)"
	echo "      ⇒ この状態の『差 0 件』は検査ではない。綴り (.$MODEXT / .$SHEXT) と"
	echo "         ビルドツリー $BUILD の中身を見ること。"
	exit 1
fi

# ---- 足りないもの (期待に在って install に無い) = 必ず FAIL ----
MISSING=$(comm -23 "$EXPECT" "$ACTUAL")
if [ -n "$MISSING" ]; then
	echo "FAIL: install ツリーに **足りない** ものがあります:"
	echo "$MISSING" | sed 's|^|      - |'
	INV_FAIL=1
fi

# ---- 余っているもの (install に在って期待に無い) ----
#   ⚠ ここが **OS ごとに形が違う**唯一の場所。許すものを *理由つきで* 並べる。
#     許可に漏れたものは FAIL にする (黙って許すと、置き場所の事故が通ってしまう)。
EXTRA=$(comm -13 "$EXPECT" "$ACTUAL")
UNEXPECTED=""
for _e in $EXTRA ; do
	case "$_e" in
		#   バンドルした上流の共有ライブラリ (版番号つき / その symlink)。版は上がるのでパターン。
		lib/libgeogram.*|lib/libopenvdb.*|bin/libgeogram.*|bin/libopenvdb.*) ;;
		#   mac: dylib の版つき別名
		lib/*.[0-9]*.dylib|lib/*.dylib.[0-9]*) ;;
		#   Windows: PE は依存 DLL を **実行体と同じ dir** に置く (3e8432f)。
		#            import library (.a/.lib) も install される構成がある。
		#   ⚠⚠ 2026-09-18: ただし **何でも許してはいけない**。以前は bin/ のあらゆる .dll を
		#     通していたので、**モジュールが bin/ に紛れ込む事故が素通り**した
		#     (⑤ の狙いは置き場所の事故を捕まえることなので、そこだけ歯が無かった)。
		#     ⇒ ビルドツリーに **同名のモジュール**が在るものは、置き場所の誤りとして FAIL。
		#     (libpig / libsrava_* は期待側 (c) に入るのでここには来ない = 上流の DLL だけが残る)
		bin/*.dll|bin/*.lib|lib/*.dll.a|lib/*.a)
			if [ "$OSKEY" != win ]; then
				UNEXPECTED="$UNEXPECTED $_e"
			else
				_bn=$(basename "$_e")
				case "$_bn" in
					*.dll.a) _bn=${_bn%.dll.a} ;;
					*.dll)   _bn=${_bn%.dll}   ;;
					*.lib)   _bn=${_bn%.lib}   ;;
					*.a)     _bn=${_bn%.a}     ;;
				esac
				if [ -f "$BUILD/$_bn.$MODEXT" ]; then UNEXPECTED="$UNEXPECTED $_e" ; fi
			fi
			;;
		#   それ以外は申告漏れとして出す
		*) UNEXPECTED="$UNEXPECTED $_e" ;;
	esac
done
if [ -n "$UNEXPECTED" ]; then
	echo "FAIL: install ツリーに **申告されていない** ものがあります ($OSKEY):"
	for _u in $UNEXPECTED ; do echo "      + $_u" ; done
	echo "      ⇒ 意図した追加なら test/srava_install.sh の ⑤ の許容表に **理由つきで** 足すこと。"
	INV_FAIL=1
fi

if [ "$INV_FAIL" = "1" ]; then
	echo "      (期待 = $EXPECT ・ 実物 = $ACTUAL に残してあります)"
	exit 1
fi
echo "INSTALL: 在庫 一致 (足りない 0 ・ 申告漏れ 0)"

echo "INSTALL-OK ($NTEST run(s))"
