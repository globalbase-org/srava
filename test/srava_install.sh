#!/bin/sh
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
MODEXT=so
for _f in "$MODDIR"/*.dll ; do [ -f "$_f" ] && MODEXT=dll ; break ; done
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

echo "INSTALL-OK ($NTEST run(s))"
