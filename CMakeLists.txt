cmake_minimum_required(VERSION 3.16)
project(srava LANGUAGES CXX C)   # C は lemon_cpp(パーサ生成器)のビルド用

# include "..." ライブラリの **コンパイル時既定探索先**($PREFIX/share/srava/lib)。
# レキサ(cgptsLemonParser)が $SRAVA_PATH の後にここを探す → インストール済み stdlib を環境変数なしで引ける。
# add_compile_definitions は以降に定義する全ターゲットに効く(冒頭に置く)。
add_compile_definitions(SRAVA_LIBDIR="${CMAKE_INSTALL_PREFIX}/share/srava/lib")
# モジュールの install 既定 (探索路の最弱・pigModuleLoader.cpp の SRAVA_MODULE_SYSDIR 既定を上書き)。
# ★ ここを定義していなかったため、Windows では既定の "/usr/local/lib/srava/modules" が
#   そのまま使われていた。MSYS2 の /usr/local は Win32 から見ると C:/msys64/usr/local なので、
#   install 済みモジュールを一切見つけられない (`srava --modules` で sysdir が「(無し)」になる)。
#   prefix から作れば Linux/macOS は従来と同じ文字列になり、Windows だけが正しくなる。
add_compile_definitions(SRAVA_MODULE_SYSDIR="${CMAKE_INSTALL_PREFIX}/lib/srava/modules")
# agent バイナリの install 既定 (env SRAVA_AGENT 未指定時の既定)。これも "/usr/local/bin/srava_agent" の
# ハードコードだったため、Windows では **agent を起動できず** その失敗が「fork 上限超過」という
# 無関係なエラーになっていた。prefix + 実行ファイル拡張子から作る。
add_compile_definitions(SRAVA_AGENT_DEFAULT="${CMAKE_INSTALL_PREFIX}/bin/srava_agent${CMAKE_EXECUTABLE_SUFFIX}")

# planner と agent が同じビルドかを突き合わせる識別子。**configure 時に 1 回決める**ので、この
# ビルドツリーから出る全バイナリ (srava / srava_agent / libpig) が同じ値を持つ。別ツリー・別 install は
# 別の値になるため、混ぜて実行すると起動時に検出できる (pigBuildStamp.cpp のコメント参照)。
if(NOT DEFINED SRAVA_BUILD_STAMP)
  string(TIMESTAMP SRAVA_BUILD_STAMP "%Y%m%dT%H%M%S" UTC)
endif()
add_compile_definitions(SRAVA_BUILD_STAMP="${SRAVA_BUILD_STAMP}")

find_package(Threads REQUIRED)

# CGAL(エージェント側のみリンク。プランナーは CGAL 非依存を維持)。
# 重い幾何演算(corefinement union 等)に EPECK kernel を使う → GMP/MPFR 依存は CGAL::CGAL が運ぶ。
#
# ★ SRAVA_MODULE_CGAL (2026-08-10): cgal カーネルを **ビルドに含めるか**。既定 ON = 従来どおり
#   (find_package CGAL REQUIRED・cgal.so + cgal 専用テストをビルド)。OFF にすると:
#     - find_package(CGAL) を呼ばない → **CGAL/GMP/MPFR 非依存でビルドできる** (CGAL 未導入環境向け)。
#     - cgal.so・cgatsAgent・test_cgatsagent・srava_module_cgal_so/srava_type_cgal を作らない。
#     - planner/srava_agent は cgal.so への依存を持たず、既定カーネルは次点 (manifold 等) になる。
#   ★ ただし ctest の大多数 (既定カーネル=cgal 前提の一般幾何テスト) は CGAL ON を前提とする
#     (OFF での ctest 全緑は保証しない=「ビルドできること」の保証)。実行時に個別カーネルを切るだけ
#     なら再ビルド不要の `module("cgal.so","off")` を使う (docs agent_so_design.md §2.4)。
option(SRAVA_MODULE_CGAL "build CGAL geometry module (find_package CGAL REQUIRED; OFF = CGAL 非依存ビルド)" ON)
# 後方互換: 旧名 SRAVA_KERNEL_CGAL を尊重 (kernel→module リネーム・2026-08-10)。
if(DEFINED SRAVA_KERNEL_CGAL)
  set(SRAVA_MODULE_CGAL ${SRAVA_KERNEL_CGAL})
endif()

# ★ モジュール option は **全てここで宣言する** (2026-08-12)。
#   実体(FetchContent / target 定義)はファイル後半の各ブロックにあるが、option() をそこに置くと
#   **クリーン configure の 1 回目**だけ if(SRAVA_MODULE_MANIFOLD) が偽になり、それより前(855/901/1989 行)
#   にある add_test 群 (manifold 系 19 本) が静かに登録されない。2 回目以降はキャッシュに載るので 226 本に
#   なり、「初回 207 / 再 configure 226」という再現しにくい差になっていた。宣言を使用より前に集約して回避。
option(SRAVA_MODULE_PIPEPROX "build pipe_proximity module (vendored source)" ON)
# 後方互換で旧名を尊重 (plugin→kernel→module と 2 度改名・2026-08-10)。新しい方が後勝ち順で効くよう
#   SRAVA_PLUGIN_PIPEPROX → SRAVA_KERNEL_PIPEPROX → (現行) SRAVA_MODULE_PIPEPROX の順に上書き。
if(DEFINED SRAVA_PLUGIN_PIPEPROX)
  set(SRAVA_MODULE_PIPEPROX ${SRAVA_PLUGIN_PIPEPROX})
endif()
if(DEFINED SRAVA_KERNEL_PIPEPROX)
  set(SRAVA_MODULE_PIPEPROX ${SRAVA_KERNEL_PIPEPROX})
endif()
option(SRAVA_MODULE_MANIFOLD "build Manifold geometry module (FetchContent)" ON)
# 後方互換: 旧名 SRAVA_KERNEL_MANIFOLD を尊重 (kernel→module リネーム・2026-08-10)。
if(DEFINED SRAVA_KERNEL_MANIFOLD)
  set(SRAVA_MODULE_MANIFOLD ${SRAVA_KERNEL_MANIFOLD})
endif()
# ★ #3419: Manifold の op 内並列 (TBB)。**既定 OFF = 従来どおりシリアル**。
#   ON にすると Manifold を MANIFOLD_PAR=ON でビルドし、op 内を TBB で並列化する。
#   ★ コンパイル時定義なので切り替えには **Manifold の全再コンパイル**が要る。
#   ★ TBB は **規約 C (system の共有ライブラリ)** を使う。FetchContent で別に建てると
#     1 プロセスに TBB が 2 つになる (OCCT も素の tbb を要求するため)。
#   設計: docs/srava_load_control_design.md §6.3 / T0-b。
option(SRAVA_MANIFOLD_PAR "build Manifold with TBB intra-op parallelism (MANIFOLD_PAR=ON)" ON)
# ★ #3435 P3: geogram (Bruno Lévy・BSD-3) モジュール。FetchContent で取得して**静的リンク**する
#   (外部依存の取り込み規約 B・docs/srava_module_design.md §7.3)。distro パッケージが無いため。
#   ★ 既定 ON (2026-08-31 ひさ指示)。⚠ 取得 + ビルドに時間がかかる外部依存なので、
#     軽く済ませたいときは -DSRAVA_MODULE_GEOGRAM=OFF。
#     (かつて「測定/比較用だから既定 OFF」と書いていたが、これは理由になっていない —
#      カーネルの性質は他のカーネルと突き合わせて初めて分かるもので、cgal も含め全部が
#      互いの比較対象。OFF だった本当の理由は取得とビルドのコストだけ。)
option(SRAVA_MODULE_GEOGRAM "build geogram geometry module (FetchContent・BSD-3)" ON)
# ★ #3433 P1: CGAL Nef (SNC) モジュール。CGAL に同梱なので新規依存はゼロ (SRAVA_MODULE_CGAL に従属)。
option(SRAVA_MODULE_NEF "build CGAL Nef geometry modules (requires SRAVA_MODULE_CGAL)" ON)
#   ★ 2 変種は個別に選べる。
#     nef_hybrid.so : 有界立体は厳密境界 (型 nfb-mesh3d) — 既定 ON
#     nef_snc.so    : cache は常に SNC   (型 nf-mesh3d)  — 既定 ON
#   ⚠ 上の SRAVA_MODULE_NEF=OFF は両方まとめて落とす傘。
#   ★★ 2026-09-16 ひさ指示で **既定を ON に戻した** (2026-08-31 に hybrid のみへ絞ったものを
#     再度開く)。⇒ 素で建てた木と、各自が検証に使う木が **同じものになる**。
#     理由: ① 3 機とも結局 -DSRAVA_MODULE_NEF_SNC=ON で建てていた (既定が誰の実態でもない)
#           ② 橋渡しの nef_cg.so / nef_mf.so が **ON でしか建たない** ⇒ 既定 OFF のままだと
#              #3545 段 4 の成果が「既定構成では 1 度も検証されない」状態が固定化する
#           ③ ctest の本数が構成で割れ、機体間の突き合わせに毎回 option の但し書きが要った
#     ⚠ 既定が変わるので **ctest の本数が増える** (この木で 487 → 531)。過去の測定と
#       並べるときは、その値がどちらの構成のものかを必ず確かめること。
option(SRAVA_MODULE_NEF_HYBRID "build nef_hybrid.so (Nef・有界立体は厳密境界)" ON)
option(SRAVA_MODULE_NEF_SNC    "build nef_snc.so (Nef・常に SNC 表現)" ON)
# ★ #3434 P2: OpenVDB (Apache-2.0) ボリュームモジュール。geogram と同じく FetchContent で
#   取ってくるので既定 OFF (ビルドが重い)。★TBB を **共有ライブラリ**として連れてくる唯一の
#   モジュールでもある (理由は下の取り込みブロックのコメント)。
option(SRAVA_MODULE_OPENVDB "build OpenVDB volume module (FetchContent・Apache-2.0・TBB を共有で連れてくる)" ON)
# ★ #3437 P5: Open CASCADE (OCCT) B-rep モジュール。**規約 C** (distro が配っているので
#   find_package でシステムから検出する)。既定 OFF で、OFF なら find_package も走らない。
option(SRAVA_MODULE_OCCT "build Open CASCADE B-rep module (find_package・LGPL-2.1+例外)" ON)
# ★★ 2026-09-07: occt の **任意機能**。OCCT の全モジュールが揃っている前提を捨て、
#   要る toolkit を機能ごとに宣言する。
#     TEXT … text op。StdPrs_BRepFont が居る **Visualization (TKV3d → TKService)** が要る。
#            TKService は freetype / fontconfig / X11 を引くので、ヘッドレスの幾何モジュールに
#            可視化層が丸ごと乗る。これが「最小 OCCT では建たない」唯一の理由 (#3471)。
#     STEP … import/export の step/stp。**DataExchange (TKDESTEP/TKXSBase)** が要る (#3437)。
#   ★★ **既定は ON で、無ければ configure を止める** (自動 OFF にしない)。
#     自動 OFF にすると OCCT を差し替えた日に op が黙って歯抜けになり、
#     その op を含む式全体のカーネルが裏返る (テストは落ちずに意味だけ変わる)。
#     落とすときは -DSRAVA_OCCT_TEXT=OFF のように **人が明示的に書く**。
option(SRAVA_OCCT_TEXT "occt: TrueType text op (OCCT Visualization / TKV3d が要る)" ON)
option(SRAVA_OCCT_STEP "occt: STEP 入出力 (OCCT DataExchange / TKDESTEP が要る)" ON)
option(SRAVA_OCCT_IGES "occt: IGES 入出力 (OCCT DataExchange / TKDEIGES が要る)" ON)   # ★ #3513
# ★ #3438 P6: Cherchi (indirect predicates) ブールモジュール。MIT・**ヘッダだけ**の実装なので
#   ビルドは軽いが、submodule を芋づるで引く (cinolib / Indirect_Predicates / abseil / oneTBB /
#   Eigen で ~150MB)。取り込みは **規約 B の変種** — FetchContent で取得するが
#   上流の CMakeLists は **使わない** (実行体 5 本と oneTBB を建てようとするため)。
#   ★ TBB は **規約 C (system)** を使う (OpenVDB / OCCT と同じ 1 プロセス 1 インスタンス原則)。
option(SRAVA_MODULE_CHERCHI "build Cherchi (indirect predicates) boolean module (FetchContent・MIT)" ON)
# ★ 2026-09-06: openvdb_cg (openvdb <-> cgal の橋渡し) の option も **ここで宣言する**。
#   実体は後半 (§openvdb_cg のブロック) だが、宣言をそこに置いていたため
#   **クリーン configure の 1 回目だけ srava_export_vox が静かに登録されなかった**
#   (2159 行の if がこの変数を見るのに、宣言が 4510 行にあった = 初回は未定義で偽)。
#   冒頭コメントが警告しているのと同じ罠で、#3468 で後から足したぶんが集約から漏れていた。
#   ⚠ 実害: クリーンビルドの CI で export_vox の回帰 (#3491 の中空の殻を含む) が走らない。
option(SRAVA_MODULE_OPENVDB_CG_GPL "build openvdb <-> cgal cross module (★ links CGAL = GPL)" ON)
# ⚠ Cygwin では建たない: IRMB が引く abseil-cpp が policy_checks.h で
#   `#if defined(__CYGWIN__) / #error "Cygwin is not supported."` と**明示的に拒否**している。
#   これは除外リストの漏れ (geogram の 2 件) ではなく上流のサポート方針なので、回避しない。
#   ON を指定されても黙って無視せず、理由を出して落とす。
# ⚠ Cygwin では **TBB を要するモジュールは建たない** (2026-08-28 に実機で確認)。
#   Cygwin は TBB をパッケージせず (`cygcheck -p libtbb` が 0 件)、自前で入れても
#   次の 2 つが構造的に残る:
#     ① 共有で建てると TBB のヘッダが `_WIN32` を見て `__declspec(dllimport)` を出さないため、
#        消費側は直接シンボルを要求するのに import lib は `__imp_` しか持たず食い違う。
#     ② 静的で建てると ld の「アーカイブは一度しか走査しない」規則に当たり、リンク行で
#        TBB より後ろに libgeogram.a が再度現れて未定義参照になる。
#   さらに静的 TBB は srava の「TBB は 1 プロセス 1 インスタンス」原則にも反する。
#   → geogram (parallel STL で TBB を無条件に引く) と openvdb (TBB 必須) は Cygwin では無効化する。
if(CYGWIN)
  foreach(_tbbmod GEOGRAM OPENVDB)
    if(SRAVA_MODULE_${_tbbmod})
      message(STATUS "${_tbbmod}: Cygwin は TBB を提供せず、自前 TBB では dllimport / アーカイブ走査順の問題が残るため無効化する")
      set(SRAVA_MODULE_${_tbbmod} OFF)
    endif()
  endforeach()
endif()
# ⚠ SRAVA_MANIFOLD_PAR も TBB を要する (下の find_package)。**既定が ON** なので、ガードが無いと
#   Cygwin は configure の段階で落ちる。上の foreach は SRAVA_MODULE_* しか見ないのでここで別に倒す。
if(CYGWIN AND SRAVA_MANIFOLD_PAR)
  message(STATUS "manifold: Cygwin は TBB を提供しないため op 内並列 (MANIFOLD_PAR) を無効化する (シリアルで建てる)")
  set(SRAVA_MANIFOLD_PAR OFF)
endif()

# ⚠ OCCT は Cygwin がパッケージしていない (find_package(OpenCASCADE REQUIRED) で落ちる)。
#   既定 ON にしたのでガードが要る。occt_mf は occt の中で建つので連動して消える。
if(CYGWIN AND SRAVA_MODULE_OCCT)
  message(STATUS "occt: Cygwin は OpenCASCADE を提供しないため無効化する (occt_mf も連動)")
  set(SRAVA_MODULE_OCCT OFF)
endif()
if(CYGWIN AND SRAVA_MODULE_CHERCHI)
  message(STATUS "cherchi: Cygwin では abseil が `#error \"Cygwin is not supported.\"` で拒否するため無効化する")
  set(SRAVA_MODULE_CHERCHI OFF)
endif()

# ==================================================================
# ★ 2026-09-06: テストへ渡す「**実際に建つモジュール**」の一覧 (SRAVA_TEST_MODULES)。
#
#   README は -DSRAVA_MODULE_<NAME>=OFF を公式にサポートすると書いているが、カーネル名を
#   べた書きで回すテストが「建っていないモジュール」を要求して **赤になっていた**
#   (実例 2026-09-06: -DSRAVA_MODULE_{GEOGRAM,CHERCHI,PIPEPROX}=OFF で
#    srava_err_module / srava_empty3d_set / srava_mf_inproc_nested_array の 3 本)。
#
#   ★ 直し方として「そのテストを登録しない」は採らない — 建っている *他の* カーネルの
#     検査まで一緒に消えてしまうため。代わりに一覧を環境変数で渡し、スクリプト側が
#     **自分が見たいカーネルと突き合わせて絞る** (test/srava_parse.sh の kernels())。
#     フルビルドでは網羅が変わらず、部分ビルドでは建っているものだけを検査する。
#
#   ⚠ ここは Cygwin の無効化 (上) より **後**でなければならない。実効値を見る必要がある。
#   ⚠ ENVIRONMENT プロパティは ";" を区切りに使うので、リストは **空白区切りの文字列**へ畳む。
# ==================================================================
set(_srava_test_modules "")
foreach(_pair "SRAVA_MODULE_CGAL:cgal" "SRAVA_MODULE_MANIFOLD:manifold"
              "SRAVA_MODULE_GEOGRAM:geogram" "SRAVA_MODULE_CHERCHI:cherchi"
              "SRAVA_MODULE_OCCT:occt" "SRAVA_MODULE_OPENVDB:openvdb"
              "SRAVA_MODULE_PIPEPROX:pipe_proximity")
  string(REPLACE ":" ";" _p "${_pair}")
  list(GET _p 0 _opt)
  list(GET _p 1 _mod)
  if(${_opt})
    list(APPEND _srava_test_modules ${_mod})
  endif()
endforeach()
# nef_* は CGAL と SRAVA_MODULE_NEF の両方に従属する (option の説明どおり)。
if(SRAVA_MODULE_CGAL AND SRAVA_MODULE_NEF AND SRAVA_MODULE_NEF_HYBRID)
  list(APPEND _srava_test_modules nef_hybrid)
endif()
if(SRAVA_MODULE_CGAL AND SRAVA_MODULE_NEF AND SRAVA_MODULE_NEF_SNC)
  list(APPEND _srava_test_modules nef_snc)
endif()
string(REPLACE ";" " " SRAVA_TEST_MODULES "${_srava_test_modules}")
message(STATUS "テストへ渡すモジュール一覧: ${SRAVA_TEST_MODULES}")
if(SRAVA_MODULE_CGAL)
  find_package(CGAL REQUIRED)
else()
  message(STATUS "SRAVA_MODULE_CGAL=OFF → CGAL 非依存ビルド (cgal.so・cgal 専用テストを除外・既定カーネルは次点へ)")
endif()

# ★ #3468 (2026-09-01): HDF5 の検出を **CGAL ブロックの外**へ出した。
#   export_vox は cgal.so から openvdb_cg.so へ移設したので、cgal 専用の依存ではなくなった。
#   無ければ export_vox だけ無効化 (srava 本体は通常どおりビルドされる)。
# ⚠ Cygwin の libhdf5-devel は cmake config(/usr/cmake/hdf5-config.cmake)が壊れており
#   find_package(HDF5) が FATAL で configure を落とす。voxelize は任意機能なので Cygwin では既定で無効化。
#   有効化したい(config を直した)場合は -DSRAVA_ENABLE_HDF5=ON。
if(CYGWIN AND NOT SRAVA_ENABLE_HDF5)
  message(STATUS "Cygwin: HDF5(export_vox)は既定で無効(壊れた hdf5-config を回避・-DSRAVA_ENABLE_HDF5=ON で有効化)")
else()
  find_package(HDF5 COMPONENTS C)
endif()
# ★ #3468: export_vox の提供元は openvdb_cg.so なので、HDF5 だけでは足りない。
#   実際に op が出るのは CGAL / OPENVDB / OPENVDB_CG_GPL がすべて ON のときだけ。
#   ここは configure の見通しのために **実際に有効になるか**を言う (HDF5 の有無だけを言わない)。
if(NOT HDF5_FOUND)
  message(STATUS "HDF5 not found → export_vox は無効（srava 本体は通常ビルド）")
elseif(SRAVA_MODULE_CGAL AND SRAVA_MODULE_OPENVDB)
  message(STATUS "HDF5 found → export_vox(voxel化→vox.h5)を有効化 (openvdb_cg.so が提供)")
else()
  message(STATUS "HDF5 found だが export_vox は無効 "
                 "(提供元の openvdb_cg.so には SRAVA_MODULE_CGAL と SRAVA_MODULE_OPENVDB の両方が要る)")
endif()

# tinyState ランタイム(find_package: <prefix>/lib/cmake/tinyState の imported target)
#   直書き /usr/local パスを廃し、tinyState v2.0.0 が出す CMake package を参照する。
#   tinyState::tinyState2 が include/コンパイルオプション/リンク依存(pthread,m,rt,dl)を
#   INTERFACE で運ぶ。バージョン検査も効く。別 prefix は -DCMAKE_PREFIX_PATH= で指定可。
# 注: バージョン指定はしない。一部の install prefix(例 MinGW 実機)は tinyStateConfigVersion.cmake を
#     同梱しておらず version=unknown となり "2.0" 要求で弾かれるため。API は v2 前提。
find_package(tinyState REQUIRED)
set(TINYSTATE_LIB tinyState::tinyState2)
get_target_property(TINYSTATE_INC tinyState::tinyState2 INTERFACE_INCLUDE_DIRECTORIES)

# ★★ #3508 / tinyState #3509: ゲートの入場順は **同点を LIFO に倒す口** を必要とする
#   (delta を主キーにすると葉が全員 +1・二項 op が全員 -1 で並ぶため)。その口が
#   stdLimitSemaphore::insNeq() で、tinyState **v2.0.0-rc16** 以降にしかない。
#   ⚠ 無いときに黙って古い挙動へ落とさない — 既定の入場順が toolchain の版で静かに
#     割れることになり、測った数字がどちらのものか分からなくなる。configure で止める。
#   ★ 判定はヘッダの中身を直接見る。check_cxx_source_compiles だと stdLimitSemaphore.h が
#     引く他ヘッダぶんの include パスと方言指定まで揃える必要があり、揃わないと
#     「無い」と「引けない」が区別できずに同じ FATAL_ERROR になる (実際に踏んだ)。
set(TINYSTATE_HAS_INSNEQ OFF)
#   ⚠ find_file は結果を **キャッシュする**ので、tinyState の prefix を差し替えても
#     古いパスを指したままになる (実際に踏んだ: 一時 prefix を消した後もそこを指していた)。
#     消えた prefix のヘッダを根拠にガードが通る = 防ぎたかった「黙って間違う」そのものなので、
#     configure のたびに引き直す。
unset(TINYSTATE_LIMITSEM_H CACHE)
find_file(TINYSTATE_LIMITSEM_H "ts2/c++/stdLimitSemaphore.h"
          PATHS ${TINYSTATE_INC} NO_DEFAULT_PATH)
mark_as_advanced(TINYSTATE_LIMITSEM_H)
if(TINYSTATE_LIMITSEM_H)
  file(READ "${TINYSTATE_LIMITSEM_H}" _ts_limitsem_src)
  if(_ts_limitsem_src MATCHES "insNeq")
    set(TINYSTATE_HAS_INSNEQ ON)
  endif()
  unset(_ts_limitsem_src)
endif()
if(NOT TINYSTATE_HAS_INSNEQ)
  message(FATAL_ERROR
    "tinyState at '${TINYSTATE_INC}' has no stdLimitSemaphore::insNeq(). "
    "srava needs it for the worker gate admission order. Install tinyState v2.0.0-rc16 or "
    "later, or point this build at one with "
    "-DtinyState_DIR=<prefix>/lib/cmake/tinyState -DTINYSTATE_TSCPP2=<prefix>/bin/tscpp2 .")
endif()

# tinyState ヘッダ(sPtr.h が concepts/typeof を使う)に合わせる
add_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++2a>
  $<$<COMPILE_LANGUAGE:CXX>:-frtti>
  $<$<COMPILE_LANGUAGE:CXX>:-g>
  $<$<COMPILE_LANGUAGE:CXX>:-Wall>
  $<$<COMPILE_LANGUAGE:CXX>:-D__EXTENSIONS__>
  $<$<COMPILE_LANGUAGE:CXX>:-D_LARGEFILE_SOURCE>
  $<$<COMPILE_LANGUAGE:CXX>:-D_FILE_OFFSET_BITS=64>
)

# Cygwin: CGAL/EPECK の大量テンプレート実体化で cgMesh2D/3D.cpp.o が COFF の .text 上限を超え
# 「file too big」で as が失敗する。big-obj 形式を有効化して回避(MinGW の gas は既定で扱えるため不要)。
if(CYGWIN)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wa,-mbig-obj>)
endif()

# src/h(手書きヘッダ)、build(tscpp2 生成 _ts2/c++/)、tinyState install を引けるように
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/h
  # .so 化 Phase 3a: 幾何実体を modules/ へ移設。include 接頭辞 (cg/c++/, mf/c++/) は温存し、
  #   各カーネルの h ルートを -I で足して従来の #include をそのまま解決する (dir 名 cgal/manifold と
  #   接頭辞 cg/mf が食い違うのは移行期の許容事項)。言語層ヘッダは src/h/cg/c++ に残る。
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/h
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/h
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/h
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/h
  # ★ #3528: 点群はカーネル中立 (cgal/geogram/occt/openvdb/manifold がどれも平坦 double 配列で
  #   受け取る) なので、本体クラス ptCloud と reader/writer は libsrava_pt に置き、消費側の
  #   モジュールは pt/c++/ptCloud.h を素で include して借りる (occt_mf が mfGeom を借りるのと同じ)。
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/h
  # ★ #3527: 素性を訊く計算 (meshprops.h / ringprops.h) はカーネル非依存なので、本体クラス
  #   guGeom と reader/writer は libsrava_gu に置き、消費側は gu/c++/guGeom.h を素で include する。
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/h
  ${CMAKE_BINARY_DIR}
  ${TINYSTATE_INC}
)

include(${CMAKE_CURRENT_SOURCE_DIR}/proj/cmake/TinyState.cmake)

enable_testing()

# ------------------------------------------------------------------
# pig コア(stdObject ベースの軽パス。状態機械ではない → tscpp2 不要)
# ------------------------------------------------------------------
# ★ 2026-08-12: libpig を共有ライブラリにしたので **RPATH** が要る。過去 2 回 (manifold / pipeprox)
#   「共有ライブラリを RPATH で辿れず agent closed before handshake」で死んでいるので既定を明示する。
#   bin/ からは ../lib、モジュール (lib/srava/modules/) からは ../.. が $PREFIX/lib を指す。
if(APPLE)
  set(CMAKE_INSTALL_RPATH "@loader_path/../lib")
  set(SRAVA_MODULE_RPATH "@loader_path/../..")
else()
  set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
  set(SRAVA_MODULE_RPATH "$ORIGIN/../..")
endif()
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)   # ビルドツリーは CMake 既定の build-rpath で解決させる

# ★ #3431 (2026-08-18): RPATH を **LD_LIBRARY_PATH より強く**する (ELF のみ)。
#   既定のリンカは新形式の DT_RUNPATH を出すが、RUNPATH は **LD_LIBRARY_PATH の後**に探される。
#   libpig.so は srava 専用の private な共有ライブラリで、planner / agent / モジュールが
#   **同一ビルドのもの**を掴んでいることが正しさの前提 (違う版を混ぜると症状は沈黙ハング)。
#   ところが LD_LIBRARY_PATH に空成分 (例 "/opt/cuda/lib64:" の末尾 ':' = **カレントディレクトリ**)
#   が入っている機械は珍しくなく、その状態で別のビルドツリーを cwd にして走らせると
#   **隣のビルドの libpig.so** を掴んで沈黙ハングする (この機械で実際に踏んだ:
#   ctest が cwd=build ツリーで走らせるので、2 つ目のビルドを検証するテストが必ず引っかかる)。
#   → 旧形式の DT_RPATH を出させる。DT_RPATH は LD_LIBRARY_PATH **より先**に探されるので、
#     自分の隣 ($ORIGIN/../lib) の libpig が必ず勝つ。第三者ライブラリ (CGAL/GMP 等) は
#     この RPATH 配下に居ないので、従来どおり通常の探索で解決される。
#   (macOS は @loader_path が同じ役割を果たし new/old dtags の区別が無い。Windows も無関係。)
if(NOT APPLE AND NOT WIN32)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_LINK_OPTIONS "-Wl,--disable-new-dtags")
  check_cxx_source_compiles("int main(){return 0;}" SRAVA_LD_HAS_DISABLE_NEW_DTAGS)
  unset(CMAKE_REQUIRED_LINK_OPTIONS)
  if(SRAVA_LD_HAS_DISABLE_NEW_DTAGS)
    add_link_options("-Wl,--disable-new-dtags")
  endif()
endif()

# ==================================================================
# srava_add_module() — モジュール (.so) 定義の定型を 1 箇所に集約する (#3431 P0-a・2026-08-18)。
#
# ★なぜ関数にしたか: 同じ 5〜6 行が 9 個のモジュールへコピーされていて、しかも設計ガイド
#   (docs/srava_module_design.md §7.1) の雛形には **INSTALL_RPATH も install ルールも
#   書かれていなかった**。雛形どおりに新しいカーネルを足すと「RPATH の無い .so」ができ、
#   共有ライブラリを持ち込んだ瞬間に「agent closed before handshake」で死ぬ (過去 2 回踏んでいる)。
#   → 書き忘れが**構造的に起きえない**ようにする。P2 (OpenVDB) / P3 (geogram) / P5 (OCCT) は
#     いずれも外部ライブラリを連れてくるので、その前にここを 1 本化しておく。
#
#   srava_add_module(<target>
#     OUTPUT   <name>          # 出力 .so の basename (必須)。→ <name>.so
#     SOURCES  <files...>      # ソース (必須)
#     DEPENDS  <targets...>    # add_dependencies (codegen 等)
#     LINK     <libs...>       # 追加リンク (pig は常に付く)
#     DEFINES  <defs...>       # 追加 define
#     INCLUDES <dirs...>       # 追加 include dir
#     HIDDEN                   # 可視性を hidden に (同一ソースから 2 変種を作る場合に必須)
#                              # ★ Apple では**無視される** — Mach-O は two-level namespace で
#                              #   衝突せず、hidden にすると dynamic_cast が壊れる (下の実装を参照)
#     INSTALL)                 # $PREFIX/lib/srava/modules へ install する (省略 = テスト専用)
#
# 関数が必ず面倒を見るもの:
#   - PREFIX "" / OUTPUT_NAME       … lib<name>.so ではなく <name>.so を出す
#   - INSTALL_RPATH                 … lib/srava/modules から $PREFIX/lib の libpig.so を辿る
#   - pig へのリンク                … pig/pts/tinyState は host 供給 (未解決のままにしない)
#   - host との実行時依存           … srava / srava_agent が dlopen するので一緒にビルドさせる
#     (実際の add_dependencies はファイル末尾の srava_wire_modules_to_hosts() が行う。
#      cgal.so のように **srava より前**で定義されるモジュールがあり、その場では
#      add_dependencies(srava ...) を書けないため。)
# ==================================================================
define_property(GLOBAL PROPERTY SRAVA_MODULE_TARGETS
  BRIEF_DOCS "srava_add_module() で定義したモジュールターゲット"
  FULL_DOCS  "末尾の srava_wire_modules_to_hosts() が srava / srava_agent へ配線する")

function(srava_add_module TARGET)
  cmake_parse_arguments(SAM "HIDDEN;INSTALL" "OUTPUT" "SOURCES;DEPENDS;LINK;DEFINES;INCLUDES" ${ARGN})
  if(NOT SAM_OUTPUT)
    message(FATAL_ERROR "srava_add_module(${TARGET}): OUTPUT (.so の basename) は必須です")
  endif()
  if(NOT SAM_SOURCES)
    message(FATAL_ERROR "srava_add_module(${TARGET}): SOURCES は必須です")
  endif()

  add_library(${TARGET} MODULE ${SAM_SOURCES})
  set_target_properties(${TARGET} PROPERTIES
    PREFIX ""                                  # lib<name>.so ではなく <name>.so
    OUTPUT_NAME "${SAM_OUTPUT}"
    INSTALL_RPATH "${SRAVA_MODULE_RPATH}")     # lib/srava/modules → $PREFIX/lib (libpig.so)
  target_link_libraries(${TARGET} PRIVATE pig ${SAM_LINK})
  if(SAM_DEPENDS)
    add_dependencies(${TARGET} ${SAM_DEPENDS})
  endif()
  if(SAM_DEFINES)
    target_compile_definitions(${TARGET} PRIVATE ${SAM_DEFINES})
  endif()
  if(SAM_INCLUDES)
    target_include_directories(${TARGET} PRIVATE ${SAM_INCLUDES})
  endif()
  if(SAM_HIDDEN AND NOT APPLE)
    # 同一ソースから 2 つの .so を作る場合、既定の可視性だと記述子等のグローバルシンボルが
    # 衝突する (ローダは RTLD_GLOBAL で dlopen するため・nef_snc / nef_hybrid で実際に踏んだ)。
    #
    # ★★ Apple では **適用してはいけない** (#3433・2026-08-20 に macOS で 38 本落ちた真因)。
    #   - 衝突は ELF の flat namespace 固有の問題で、Mach-O は **two-level namespace** が既定。
    #     各 bundle の参照はリンク時に定義側の image へ束ねられるので、同名シンボルが
    #     2 つあっても取り違えは起きない。つまり Apple では **hidden にする理由が無い**。
    #   - 一方 hidden にすると **dynamic_cast が壊れる**。tinyState の生成クラスは
    #     vtable/typeinfo を weak symbol として libpig.dylib と .so の**両方**が持つが、
    #     hidden にすると dyld が coalesce できず 2 つの typeinfo が別アドレスになる。
    #     Apple の libc++ は type_info を **アドレスで比較**する (unique RTTI 前提) ので、
    #     `dynamic_cast<Base*>(derived)` が **null を返す**。
    #   - 実際の壊れ方: sPtr::d_cast (= dynamic_cast) が null →
    #     生成 ctor の `impl(d_cast(implptr))` が null → 直後の `this->impl->ifp = thThis` で
    #     SIGSEGV (fault addr 0x21c0 = ifp のオフセット。8KB の chunkBuf の直後)。
    #     agent が落ちるので planner からは「agent closed unexpectedly」に見え、
    #     テストからは「nef produced no volume」= 幾何が出てこないように見えた。
    #     ★ 症状は幾何だが、原因は幾何カーネルと無関係な**可視性とリンク**の問題だった。
    set_target_properties(${TARGET} PROPERTIES
      CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  endif()
  if(SAM_INSTALL)
    # ★ MODULE は **DLL プラットフォームでも LIBRARY 扱い** (CMake の規則。SHARED だけが
    #   .dll = RUNTIME / import lib = ARCHIVE に分かれる)。⇒ ここに RUNTIME は要らない。
    #   ⚠ 2026-09-16 に SHARED 側 10 本へ RUNTIME を足したが、**ここは揃えないこと**。
    install(TARGETS ${TARGET} LIBRARY DESTINATION lib/srava/modules)   # ローダの SYSDIR
  endif()
  set_property(GLOBAL APPEND PROPERTY SRAVA_MODULE_TARGETS ${TARGET})
endfunction()

# 定義済みの全モジュールを host (srava / srava_agent) の実行時依存として配線する。
# ★ ファイル末尾で 1 回だけ呼ぶこと。add_dependencies は **第 1 引数のターゲットが既に
#   存在している**必要があり、cgal.so は srava より前で定義されるため、その場では書けない。
function(srava_wire_modules_to_hosts)
  get_property(_mods GLOBAL PROPERTY SRAVA_MODULE_TARGETS)
  foreach(_m IN LISTS _mods)
    add_dependencies(srava       ${_m})   # planner が起動時に dlopen (記述子/型登録・in-proc 実行)
    add_dependencies(srava_agent ${_m})   # agent host が process 実行時に dlopen (make_agent)
  endforeach()
endfunction()

# ==================================================================
# ★ srava_export_all(<target>) — **Windows で全シンボルのエクスポートを明示する** (2026-08-27)。
#
#   PE はエクスポート表に載ったものだけがリンク/解決できる。MinGW の ld は救済として
#   `--export-all-symbols` を暗黙に効かせるが、これは **DLL 内に明示的な dllexport が
#   1 つも無いとき限定**の挙動である。つまり「dllexport を撒くライブラリを 1 つ足した瞬間、
#   自分のクラスが丸ごと表から消える」という**依存の変化で遠隔から壊れる**性質を持つ。
#
#   ⚠ 実際に踏んだ (2026-08-27・MinGW 実機):
#     - libsrava_oc が **OCCT をリンクする最初の共有ライブラリ**になった途端、OCCT ヘッダの
#       Standard_EXPORT が撒く dllexport で自動エクスポートが切れ、ocShape のメンバが
#       表から漏れて occt.dll のリンクが undefined reference で落ちた
#     - 同じ機構で各モジュールの `srava_module` も漏れた (→ pigModule.h の SRAVA_MODULE_EXPORT)
#
#   ★ 「たまたま今は dllexport を持ち込む依存が無い」ものに依存しないよう、**srava が出す
#     共有ライブラリは全て明示する** (ひさ判断 2026-08-27)。自動エクスポートが効いている
#     ケースでは意味が変わらない (同じことを明示的にやるだけ) ので無害。
#   ⚠ 対象は **共有ライブラリだけ**。モジュール (add_library MODULE) が公開すべきなのは
#     `srava_module` 1 本だけなので、そちらは pigModule.h の属性で足りる。
# ==================================================================
function(srava_export_all TARGET)
  if(WIN32 OR CYGWIN)
    target_link_options(${TARGET} PRIVATE -Wl,--export-all-symbols)
  endif()
endfunction()

# ★ 2026-08-12: pig を **共有ライブラリ** にした (Windows モジュール対応の前提)。
#   従来: pig=STATIC + pts ランタイム(AGENT_HOST_SRC)を各 host が個別コンパイル → モジュール .so は
#         pig/pts/tinyState を **未解決のまま**残し host の -rdynamic + RTLD_GLOBAL で dlopen 時に解決。
#   これは ELF 固有の idiom で **PE/COFF (Windows) では成立しない** (DLL はリンク時未解決を許さない)。
#   → pig + AGENT_HOST_SRC(pts) + tinyState を 1 本の共有ライブラリに集約し、host もモジュールも
#     両方これにリンクする。モジュールの未定義シンボルが 0 になり Windows でも DLL 化できる。
#   モジュールが host に要求する未定義シンボルは pig / pts / ts2・tinyState / libc・libstdc++ に
#     またがっていた (nm -D -u で確認)。→ 3 者すべてを共有ライブラリに入れる。
#   ⚠ tinyState は **PIC ビルドが必要** (非 PIC の .a は共有ライブラリに埋め込めない)。Redmine #3421。
add_library(pig SHARED
  src/classes/pig/c++/pigData.cpp
  src/classes/pig/c++/pigModuleRegistry.cpp   # モジュール (カーネル/プラグイン) 一元レジストリ (K1〜K3 集約・Phase 1)
  src/classes/pig/c++/pigModuleLoader.cpp     # .so 動的ロード (dlopen/dlsym/abi 検査・Phase 3b)
  src/classes/pig/c++/pigExecBackend.cpp      # 起動方式 (thread/process) → Mediator 生成の抽象 (Phase 1)
  src/classes/pig/c++/pigCacheManager.cpp   # キャッシュ dir 機構(pig 層・pigwire のみ依存)。sweep を呼ぶ実行体は pig をリンク済み
  src/classes/pig/c++/pigValueCodec.cpp     # 値の往復(serialize の対の parser)。プラグイン機構の I-O
  src/classes/pig/c++/pigValueParser.cpp    # 値パーサ生成子レジストリ(言語パーサを pig 層から起動)
  src/classes/pig/c++/pigDataRef.cpp        # D_REF の pigData 表現(pigDataPair("D_REF",{...}))
)
# tinyState の埋め込み。当初 whole-archive を試したが、libtinyState2.a には **それ自体が未解決参照を
#   持つオブジェクト** (stdBalancedTree.cpp.o → `stdBalancedTree_::callback(...)` /
#   `vtable for sBalancedTreeCondition` が未定義) が含まれており、全取り込みするとリンクが落ちる
#   (通常リンクでは誰も参照しないので表面化しない)。→ 通常リンクにして、モジュール側に不足が出るなら
#   その分だけ手当てする方針。Redmine #3421 に記録。
# ★ tinyState は **PRIVATE**。共有ライブラリ化後は libpig 自身が tinyState を取り込んで
#   シンボルを export するので、consumer が **静的 libtinyState2.a を重ねてリンクする必要は無い**。
#   むしろ Windows(PE) では import lib (libpig.dll.a) と静的 .a の両方が定義を持つことになり
#   "multiple definition of stdObject::addref()" 等でリンクが落ちる (ELF は無害なので Linux では
#   露見しなかった)。Threads/m/dl は consumer も直接使うので PUBLIC のまま。
# ★ tinyState の伝播は **OS で要件が逆**になる:
#   - ELF(Linux/macOS): 共有ライブラリは未解決シンボルを許すので libpig.so は「自分が使う分」しか
#     tinyState を取り込まない。consumer 自身の inline コード (sPtr.h の addref/relref、ts2Parallel 等)
#     が要求する分は consumer 側で静的 tinyState をリンクして解決する必要がある → **PUBLIC** で伝播。
#   - PE(Windows): DLL はリンク時に全解決が要るので libpig.dll は tinyState を取り込んで export する。
#     consumer が更に静的 libtinyState2.a を重ねると import lib と定義が衝突し
#     "multiple definition of stdObject::addref()" で落ちる → **PRIVATE** にして伝播させない。
#   (本質的な解は「libpig が tinyState を丸ごと内包する」= whole-archive だが、libtinyState2.a に
#    自身が未解決参照を持つ .o (stdBalancedTree) があり全取り込みできない。Redmine #3421 参照。)
# ★ 2026-08-13: tinyState v2.0.0-rc8 で stdBalancedTree の未定義 virtual 3 つが実装されたため、
#   **whole-archive による完全内包が可能になった** (rc7 までは stdBalancedTree.cpp.o が自身で
#   未解決参照を持ち、全取り込みすると exe のリンクが落ちた)。
#   完全内包により「libpig が tinyState のすべてを持ち export する」= consumer は libpig だけを見れば
#   よい、が全 OS で成立する → PRIVATE で伝播不要・OS 分岐も不要。
#   (rc7 まではやむなく OS 分岐していた: ELF は「足りない分だけ静的補充」ができるが、PE は import lib と
#    静的 .a が衝突するのでそれができず、Windows で srava.exe が ts2Parallel 未解決で落ちていた。)
target_link_libraries(pig PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${TINYSTATE_LIB}>")
if(APPLE)
  # ★ whole-archive は tinyState の Darwin glue (cDarwin_oc.m.o = darwin_getDirectory* /
  #   NSBundle 等の Objective-C 実装) も全部取り込むため Foundation が要る。静的世界では
  #   未参照オブジェクトとしてアーカイブから引かれず露見しなかった (tinyStateConfig は
  #   APPLE のフレームワーク依存を export していない)。
  target_link_libraries(pig PRIVATE "-framework Foundation")
endif()
target_link_libraries(pig PUBLIC Threads::Threads m ${CMAKE_DL_LIBS})
# 共有ライブラリなので install が要る (host / モジュールが実行時に参照する)。
srava_export_all(pig)   # ★ Windows: 明示エクスポート (host もモジュールも全部これにリンクする)
install(TARGETS pig LIBRARY DESTINATION lib RUNTIME DESTINATION bin)

# 単体テスト
add_executable(test_pigwire test/pigwire_test.cpp)
add_test(NAME pigwire COMMAND test_pigwire)

add_executable(test_pigdata test/pigdata_test.cpp)
target_link_libraries(test_pigdata PRIVATE pig)
add_test(NAME pigdata COMMAND test_pigdata)

add_executable(test_pigvaluecodec test/pigvaluecodec_test.cpp)
target_link_libraries(test_pigvaluecodec PRIVATE pig)
add_test(NAME pigvaluecodec COMMAND test_pigvaluecodec)
set_tests_properties(pigvaluecodec PROPERTIES
  PASS_REGULAR_EXPRESSION "pigvaluecodec: ALL PASS"
  FAIL_REGULAR_EXPRESSION "FAIL")

# ------------------------------------------------------------------
# tinyState ビルド経路の検証 probe(tscpp2 codegen → compile → link → run)
#   ステップ3で pigfFunction 等を実装する前のパイプライン確認用。後で削除可。
# ------------------------------------------------------------------
tinystate_codegen(probe_codegen
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/probe/c++/tsHello.cpp
)
add_executable(smoke_tinystate
  src/main/probe_main.cpp
  src/classes/probe/c++/tsHello.cpp
)
add_dependencies(smoke_tinystate probe_codegen)
target_link_libraries(smoke_tinystate PRIVATE ${TINYSTATE_LIB} Threads::Threads m)
add_test(NAME tinystate_smoke COMMAND smoke_tinystate)

# ------------------------------------------------------------------
# pts 土台(piggybackTurtle tinyState 元祖/実態元祖: ptsObject / ptsApplication)
# ------------------------------------------------------------------
tinystate_codegen(pigts_codegen
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsObject.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsApplication.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsAgentApplication.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsDataCache.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsLoadControl.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/co_ptsLoadControl.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/co_ptsConfigWatch.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsAgent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsGenericAgent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfFunction.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfAssign.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfMain.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWireCacheStreamWriter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWireCacheStreamWriterText.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWireCacheStreamReader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWireCacheStreamReaderText.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/ptscgWireCacheStreamWriterMesh.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWireCacheStreamWriterRef.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWireCacheStreamReaderRef.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/ptscgWireCacheStreamReaderMesh.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWireStreamTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWirePipe.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsErrSink.cpp   # agent stderr をテキストへ溜める (devNull の代わり)
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsMediator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsMediatorExternal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsMediatorInternal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWirePipeTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/wpBigParent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/wpBigChild.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsAgentStub.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfAgent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfModuleAgent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigBuildStamp.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfAgentTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsCalcBody.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaBox.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaPrism.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaCylinder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaCone.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaTorus.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaTetrahedron.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaPyramid.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaSphere.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaEmpty2D.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaEmpty3D.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaIcosphere.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaUnion.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaHull.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaLoftRuled.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaRefine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaRemesh.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaSimplify.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaCombine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaIntersection.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaDifference.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaExport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaImport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaTranslate.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaRotate.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaMirror.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaScale.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaTransform.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaColor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaRect.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaNgon.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaCircle.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaPolygon.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaLine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaSection.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaExtrude.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaTube.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaRevolve.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaOffset.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaArea.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaNverts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaVert.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaVerts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaShell.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaShellAt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaPartAt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaFaceVerts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaNfaces.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaNshells.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaNparts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaGenus.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaDistanceAt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaEstimateNormals.cpp   # ★ #3528: 点群の法線推定
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaVoronoi.cpp   # ★ #3525: voronoi(p, box)
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaDelaunay.cpp  # ★ #3525: delaunay(p)
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaPart.cpp      # ★ #3525: part(v, i) の 2D 版
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaValid.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaRepair.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaProjectFlatten.cpp   # ★ #3534: z=0 への直投影
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaVolume.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaPerimeter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaCentroid.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaBbox.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaDistance.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaClosest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaFarthest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaThinSpots.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaCast.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgatsAgent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgatsAgentTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgptsPlanner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgptsLemonParser.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfSequence.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfIf.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfTryCatch.cpp   # ★ #3482 try/catch 文
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfApply.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfWhile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfArrayFold.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfMap.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfMapOp.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfGate.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsFireAndForget.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfAsync.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfSystem.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/tsSysRepro.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/tsPipeRepro.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/tsTeardownRepro.cpp
)

# ★ pig ライブラリは pigExecBackend.cpp → ptsMediator.h → 生成 _ts2/c++/ptsMediator_pb.h を
#   (推移的に) include するので pigts_codegen に依存させる。他の消費ターゲット (srava_agent/cgal/
#   test_pigf 等) は既に add_dependencies(... pigts_codegen) 済だが、pig ライブラリだけ欠けていて
#   クリーン並列ビルドで "ptsMediator_pb.h: No such file" が間欠発生していた (pigExecBackend 追加時の
#   張り忘れ)。pig は 90 行で定義済 (pigts_codegen 定義より前) なので依存はここで張る。
add_dependencies(pig pigts_codegen)

# ------------------------------------------------------------------
# lemonc++ パーサ生成(2.3): lemon_cpp ホストツールをビルド → 文法 .y から ns_sravaParser.cpp/.h 生成
#   lemon は出力を入力 .y と同じディレクトリに書くので、build/gen に .y をコピーしてそこで実行する。
# ------------------------------------------------------------------
add_executable(lemon_cpp ${CMAKE_CURRENT_SOURCE_DIR}/tools/lemon/lemon_cpp.c)
target_compile_options(lemon_cpp PRIVATE -w)   # 第三者コード。警告抑制

set(SRAVA_GEN ${CMAKE_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${SRAVA_GEN})
set(SRAVA_GRAMMAR ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/ns_sravaParser.y)
set(SRAVA_LEMPAR  ${CMAKE_CURRENT_SOURCE_DIR}/tools/lemon/lempar.c)
add_custom_command(
  OUTPUT ${SRAVA_GEN}/ns_sravaParser.cpp ${SRAVA_GEN}/ns_sravaParser.h
  COMMAND ${CMAKE_COMMAND} -E copy ${SRAVA_GRAMMAR} ${SRAVA_GEN}/ns_sravaParser.y
  COMMAND $<TARGET_FILE:lemon_cpp> -T${SRAVA_LEMPAR} ${SRAVA_GEN}/ns_sravaParser.y
  DEPENDS lemon_cpp ${SRAVA_GRAMMAR} ${SRAVA_LEMPAR}
  WORKING_DIRECTORY ${SRAVA_GEN}
  COMMENT "lemon_cpp: generating srava parser from ns_sravaParser.y")
add_custom_target(gen_srava_parser DEPENDS ${SRAVA_GEN}/ns_sravaParser.cpp ${SRAVA_GEN}/ns_sravaParser.h)

# ★ #3427 ③: ptsApplication が app 所有レジストリ (pigModuleRegistry/loader/ptsDataCache_helper) に
#   依存するようになったため、pts ランタイムの個別コンパイルをやめ libpig(共有・全部入り) をリンクする
#   (2026-08-12 の libpig 統合の残りを回収)。
add_executable(smoke_pts
  src/main/pts_main.cpp
)
add_dependencies(smoke_pts pigts_codegen)
target_link_libraries(smoke_pts PRIVATE pig Threads::Threads m)
add_test(NAME pts_smoke COMMAND smoke_pts)

# ★ #3436 P4: op の幾何型シグネチャ (sig) の文法と照合規則の単体テスト。
#   ヘッダだけに依存する (srava 本体のリンク不要)。docs/sig_grammar_design.md §7.1 の検算表を含む。
add_executable(test_pigsig test/pigsig_test.cpp)
target_include_directories(test_pigsig PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/h)
add_test(NAME pigsig COMMAND test_pigsig)
set_tests_properties(pigsig PROPERTIES PASS_REGULAR_EXPRESSION "PIGSIG-OK")

# pigf async 機構の end-to-end(pigDataFunction<pigfConst> → 非同期 yield/resume → 42)
add_executable(test_pigf
  src/main/pigf_main.cpp
  src/classes/pig/c++/pigfMain.cpp
)
add_dependencies(test_pigf pigts_codegen)
target_link_libraries(test_pigf PRIVATE pig Threads::Threads m)
add_test(NAME pigf COMMAND test_pigf)

# ------------------------------------------------------------------
# step4 pigwire I/O エンジン: ptsWireCacheStream{Writer,Reader}[Text] 往復テスト
#   WriterText が D_META/D_TEXT を書き → ReaderText がポーリング読みして連結 →
#   元の payload とバイト一致するか(逐次 + 同一プロセス並行の 2 段階)。
# ------------------------------------------------------------------
add_executable(test_cachestream
  src/main/cachestream_main.cpp
  src/classes/pig/c++/ptsWireStreamTest.cpp
)
add_dependencies(test_cachestream pigts_codegen)
target_link_libraries(test_cachestream PRIVATE pig Threads::Threads m)
add_test(NAME cachestream COMMAND test_cachestream)

# ------------------------------------------------------------------
# step5 ptsWirePipe: socketpair 越しの双方向メッセージパイプ echo 往復テスト
#   2 本の ptsWirePipe が handshake → ping/echo → 番兵で双方 self-terminate。
#   NB: AF_UNIX socketpair(2) を使うため Windows(MinGW)では除外(winsock に socketpair 無し)。
# ------------------------------------------------------------------
if(NOT WIN32)
add_executable(test_wirepipe
  src/main/wirepipe_main.cpp
  src/classes/pig/c++/ptsWirePipeTest.cpp
)
add_dependencies(test_wirepipe pigts_codegen)
target_link_libraries(test_wirepipe PRIVATE pig Threads::Threads m)
add_test(NAME wirepipe COMMAND test_wirepipe)
endif()

# ------------------------------------------------------------------
# tinyState #3393 の回帰テスト: ptsWirePipe 越しの大レコード送受信(CGAL 非依存)
#   wp_child  : ptsWirePipe 子(自 stdin/stdout)。大レコードを受け取り "COUNT=n" を返す。
#   wp_parent : ptsWirePipe 親。ts2System で wp_child を起動(WP_CHILD env)、
#               set_divisible した wfd にレコードを送る(pigfAgent.cpp の SEND 経路と同じ配線)。
#   経緯: MinGW で「WP_SEND=par + 大 arg」が write_c の EX_STAY yield 空回り(livelock)、さらに
#         shutdown で fwIO::loop が抜けない teardown hang を起こしていた。tinyState 側で根治
#         (livelock=7625336 / shutdown+refio UAF=89ee2e6)。詳細は docs/wirepipe_big_repro.md。
#   ctest wirepipe_big は WP_BYTES=4096(小・direct)で **完走 + clean exit** を確認(shutdown 経路の
#   回帰ガード)。大 arg/並行の再現は WP_SEND=par WP_BYTES=<大> で手動。診断 env: WP_SEND/WP_NODIV/
#   WP_CATCHDBG/WP_WRDBG/WP_SENDDBG/WP_TDDBG。
#   NB: 2 プロセス IPC。socketpair 不使用(ts2System pipe)なので Windows も対象。
# ------------------------------------------------------------------
add_executable(wp_child
  src/main/wp_child_main.cpp
  src/classes/pig/c++/wpBigChild.cpp
)
add_dependencies(wp_child pigts_codegen)
target_link_libraries(wp_child PRIVATE pig Threads::Threads m)

add_executable(wp_parent
  src/main/wp_parent_main.cpp
  src/classes/pig/c++/wpBigParent.cpp
)
add_dependencies(wp_parent pigts_codegen wp_child)
target_link_libraries(wp_parent PRIVATE pig Threads::Threads m)

add_test(NAME wirepipe_big COMMAND wp_parent)
set_tests_properties(wirepipe_big PROPERTIES
  ENVIRONMENT "WP_CHILD=$<TARGET_FILE:wp_child>;WP_BYTES=4096"
  TIMEOUT 30)

# ------------------------------------------------------------------
# step5 pigfAgent: 別プロセス agent との control-plane 往復(全状態通し)
#   srava-agent-stub: control-plane だけ話す最小 agent(自 stdin/stdout)
#   test_pigfagent  : pigDataFunction<pigfAgent> を compact → stub を ts2System で起動 →
#                     handshake / C_OP / C_ARG(ts2Parallel) / A_SAVE_* → 保存パス検証
# ------------------------------------------------------------------
add_executable(srava_agent_stub
  src/main/agent_stub_main.cpp
  src/classes/pig/c++/ptsAgentStub.cpp
)
add_dependencies(srava_agent_stub pigts_codegen)
target_link_libraries(srava_agent_stub PRIVATE pig Threads::Threads m)

add_executable(test_pigfagent
  src/main/pigfagent_main.cpp
  src/main/cgal_test_fixture.cpp             # Phase D-1: 旧 pigfModuleAgent placeholder の移設先
  src/classes/cg/c++/cgptsLemonParser.cpp   # pigfAgent が値結果を VALUE パース(対称化)
  src/classes/pig/c++/pigfOps.cpp            # pigDataOperatorVariable(値規則 IDENT 用)
  src/classes/pig/c++/pigfAgentTest.cpp
  ${SRAVA_GEN}/ns_sravaParser.cpp            # 値規則のみ(SRAVA_VALUE_ONLY)
)
add_dependencies(test_pigfagent pigts_codegen srava_agent_stub gen_srava_parser)
target_include_directories(test_pigfagent PRIVATE ${SRAVA_GEN})
target_compile_definitions(test_pigfagent PRIVATE SRAVA_VALUE_ONLY)
target_link_libraries(test_pigfagent PRIVATE pig Threads::Threads m)
add_test(NAME pigfagent COMMAND test_pigfagent)
# agent バイナリのパスを test 実行時に注入(pigfAgent が getenv("SRAVA_AGENT") で読む)
set_tests_properties(pigfagent PROPERTIES
  ENVIRONMENT SRAVA_AGENT=$<TARGET_FILE:srava_agent_stub>)

# ------------------------------------------------------------------
# step6 cgatsAgent: dispatch テーブル + 計算本体(ptsCalcBody)を持つ実エージェント骨格
#   srava_agent     : cgatsAgent を起動する実エージェントプロセス(CGAL 未リンク・box=ダミー)
#   test_cgatsagent : pigDataFunction<pigfModuleAgent>(op=box) を compact → srava_agent と往復
# ------------------------------------------------------------------
# ------------------------------------------------------------------
# .so 化 Phase 3c: 単一 srava_agent (カーネル非依存 host)。argv の .so を dlopen して実行体を起こす。
#   旧 srava_agent (CGAL 静的リンク) / srava_agent_mf (manifold 静的リンク) の 2 バイナリを廃止し、
#   1 本の host に集約 (docs §1.2)。カーネル幾何は cgal.so / manifold.so から供給される。
#   host は pts* 実行体基盤 (AGENT_HOST_SRC) + libpig のみ・-rdynamic で自シンボルを export し、
#   dlopen(RTLD_NOW) が .so の未解決 pig/pts/tinyState を解決する。
# ------------------------------------------------------------------

# host (pts* 実行体基盤)。srava_agent 本体 + カーネル .so の両方から使う (単一ソース)。
set(AGENT_HOST_SRC
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
  src/classes/pig/c++/ptsAgentApplication.cpp
  src/classes/pig/c++/ptsDataCache.cpp
  src/classes/pig/c++/ptsWireCacheStreamReaderText.cpp
  src/classes/pig/c++/ptsMediator.cpp
  src/classes/pig/c++/ptsAgent.cpp
  src/classes/pig/c++/osglue.cpp
  src/classes/pig/c++/ptsLoadControl.cpp
  src/classes/pig/c++/co_ptsLoadControl.cpp
  src/classes/pig/c++/co_ptsConfigWatch.cpp
  src/classes/pig/c++/pigInstallPaths.cpp   # install レイアウトを実行体相対で解く (#3431)
  src/classes/pig/c++/ptsWirePacket.cpp
  src/classes/pig/c++/ptsWirePipe.cpp
  src/classes/pig/c++/ptsErrSink.cpp
  src/classes/pig/c++/ptsWireCacheStreamWriter.cpp
  src/classes/pig/c++/ptsWireCacheStreamWriterText.cpp
  src/classes/pig/c++/ptsWireCacheStreamReader.cpp
  src/classes/pig/c++/ptsWireCacheStreamWriterRef.cpp
  src/classes/pig/c++/ptsWireCacheStreamReaderRef.cpp
  src/classes/pig/c++/pigRefCacheCodec.cpp
  src/classes/pig/c++/ptsCalcBody.cpp
  src/classes/pig/c++/ptsGenericAgent.cpp        # モジュール実行体の共通基底 (状態機械集約・派生は各 .so)
  # ★ 2026-08-12: mediator 実装も libpig へ (従来は srava/test_* が各自足していた。共有ライブラリでは
  #   ptsMediator.cpp が参照する実装が lib 内に無いと未解決のまま残り exe 側のリンクが落ちる)。
  src/classes/pig/c++/ptsMediatorExternal.cpp
  src/classes/pig/c++/ptsMediatorInternal.cpp
  # ★ 2026-08-13: pigf* 層 (言語フレームワークの状態機械) も libpig へ。
  #   従来は srava / test_pigfagent / test_cgatsagent が **各自コンパイル** していた (3 重)。
  #   Windows では更に深刻で、pigData.h の pigDataFunction<T>::_start がヘッダ内で
  #   sThreadKey<sCallSection>::operator->() を実体化するため、**exe と libpig.dll が別々の
  #   thread_local static を持ち** (PE はイメージ跨ぎでシンボルを統一しない = ELF の
  #   STB_GNU_UNIQUE 相当が無い)、caller() が null を返して SIGSEGV していた。
  #   実体化を libpig 側 1 箇所に集約して import へ一本化する。
  src/classes/pig/c++/pigfFunction.cpp
  src/classes/pig/c++/pigfAgent.cpp
  src/classes/pig/c++/pigfModuleAgent.cpp
  src/classes/pig/c++/pigBuildStamp.cpp
  src/classes/pig/c++/pigfAssign.cpp
  src/classes/pig/c++/pigfOps.cpp
  src/classes/pig/c++/pigfSequence.cpp
  src/classes/pig/c++/pigfIf.cpp
  src/classes/pig/c++/pigfTryCatch.cpp   # ★ #3482 try/catch 文
  src/classes/pig/c++/pigfApply.cpp
  src/classes/pig/c++/pigfWhile.cpp
  src/classes/pig/c++/pigfArrayFold.cpp
  src/classes/pig/c++/pigfMap.cpp
  src/classes/pig/c++/pigfMapOp.cpp
  src/classes/pig/c++/pigfGate.cpp
  src/classes/pig/c++/ptsFireAndForget.cpp
  src/classes/pig/c++/pigfAsync.cpp
  src/classes/pig/c++/pigfSystem.cpp
)

# ★ 2026-08-12: pts ランタイムを libpig(共有) に取り込む。従来は host 3 本 (srava / srava_agent /
#   srava_module_probe) が同じ AGENT_HOST_SRC を **各自コンパイル** し、モジュールは未解決のまま
#   host の -rdynamic に頼っていた。共有ライブラリに 1 部だけ置き、host もモジュールも参照する。
#   (SRAVA_VALUE_ONLY は ns_sravaParser.y にしか効かないので、1 箇所コンパイルで挙動は変わらない)
target_sources(pig PRIVATE ${AGENT_HOST_SRC})
target_include_directories(pig PRIVATE ${SRAVA_GEN})
add_dependencies(pig pigts_codegen)

# cgal.so の幾何ソース (テスト専用 cgatsAgentTest は除く)。cgal.so ターゲットで使う。
set(CGAL_KERNEL_SRC
  modules/cgal/c++/ptscgWireCacheStreamWriterMesh.cpp
  modules/cgal/c++/ptscgWireCacheStreamReaderMesh.cpp
  modules/cgal/c++/cgCacheCodec.cpp
  modules/cgal/c++/cgMesh.cpp
  modules/cgal/c++/cgMesh3D.cpp
  modules/cgal/c++/cgMesh2D.cpp
  modules/cgal/c++/cgHull.cpp
  modules/cgal/c++/cgVoronoi.cpp
  modules/cgal/c++/cgDelaunay.cpp
  modules/cgal/c++/cgLoft.cpp
  modules/cgal/c++/cgRemesh.cpp
  modules/cgal/c++/cgEstimateNormals.cpp
  modules/cgal/c++/cgaBox.cpp
  modules/cgal/c++/cgaPrism.cpp
  modules/cgal/c++/cgaCylinder.cpp
  modules/cgal/c++/cgaCone.cpp
  modules/cgal/c++/cgaTorus.cpp
  modules/cgal/c++/cgaTetrahedron.cpp
  modules/cgal/c++/cgaPyramid.cpp
  modules/cgal/c++/cgaSphere.cpp
  modules/cgal/c++/cgaEmpty2D.cpp
  modules/cgal/c++/cgaEmpty3D.cpp
  modules/cgal/c++/cgaIcosphere.cpp
  modules/cgal/c++/cgaUnion.cpp
  modules/cgal/c++/cgaHull.cpp
  modules/cgal/c++/cgaLoftRuled.cpp
  modules/cgal/c++/cgaRefine.cpp
  modules/cgal/c++/cgaRemesh.cpp
  modules/cgal/c++/cgaSimplify.cpp
  modules/cgal/c++/cgaCombine.cpp
  modules/cgal/c++/cgaIntersection.cpp
  modules/cgal/c++/cgaDifference.cpp
  modules/cgal/c++/cgaExport.cpp
  modules/cgal/c++/cgaImport.cpp
  modules/cgal/c++/cgaTranslate.cpp
  modules/cgal/c++/cgaRotate.cpp
  modules/cgal/c++/cgaMirror.cpp
  modules/cgal/c++/cgaScale.cpp
  modules/cgal/c++/cgaTransform.cpp
  modules/cgal/c++/cgaColor.cpp
  modules/cgal/c++/cgaRect.cpp
  modules/cgal/c++/cgaNgon.cpp
  modules/cgal/c++/cgaCircle.cpp
  modules/cgal/c++/cgaPolygon.cpp
  modules/cgal/c++/cgaLine.cpp
  modules/cgal/c++/cgaSection.cpp
  modules/cgal/c++/cgaExtrude.cpp
  modules/cgal/c++/cgaTube.cpp
  modules/cgal/c++/cgaRevolve.cpp
  modules/cgal/c++/cgaOffset.cpp
  modules/cgal/c++/cgaArea.cpp
  modules/cgal/c++/cgaNverts.cpp
  modules/cgal/c++/cgaVert.cpp
  modules/cgal/c++/cgaVerts.cpp
  modules/cgal/c++/cgaShell.cpp
  modules/cgal/c++/cgaShellAt.cpp
  modules/cgal/c++/cgaPartAt.cpp
  modules/cgal/c++/cgaFaceVerts.cpp
  modules/cgal/c++/cgaNfaces.cpp
  modules/cgal/c++/cgaNshells.cpp
  modules/cgal/c++/cgaNparts.cpp
  modules/cgal/c++/cgaGenus.cpp
  modules/cgal/c++/cgaDistanceAt.cpp
  modules/cgal/c++/cgaEstimateNormals.cpp
  modules/cgal/c++/cgaVoronoi.cpp
  modules/cgal/c++/cgaDelaunay.cpp
  modules/cgal/c++/cgaPart.cpp
  modules/cgal/c++/cgaValid.cpp
  modules/cgal/c++/cgaRepair.cpp
  modules/cgal/c++/cgaProjectFlatten.cpp
  modules/cgal/c++/cgaVolume.cpp
  modules/cgal/c++/cgaPerimeter.cpp
  modules/cgal/c++/cgaCentroid.cpp
  modules/cgal/c++/cgaBbox.cpp
  modules/cgal/c++/cgaDistance.cpp
  modules/cgal/c++/cgaClosest.cpp
  modules/cgal/c++/cgaFarthest.cpp
  modules/cgal/c++/cgaThinSpots.cpp
  modules/cgal/c++/cgaCast.cpp
  modules/cgal/c++/cgatsAgent.cpp
)

add_executable(srava_agent
  src/main/srava_agent_main.cpp
  # ${AGENT_HOST_SRC} は libpig(共有) に移動 (2026-08-12)
)
add_dependencies(srava_agent pigts_codegen gen_srava_parser)
target_include_directories(srava_agent PRIVATE ${SRAVA_GEN})
target_compile_definitions(srava_agent PRIVATE SRAVA_VALUE_ONLY)
target_link_libraries(srava_agent PRIVATE pig Threads::Threads m ${CMAKE_DL_LIBS})
set_target_properties(srava_agent PROPERTIES ENABLE_EXPORTS ON)   # -rdynamic: dlopen した .so から host シンボルを解決

# ------------------------------------------------------------------
# points.so — 点群モジュール (#3528)。**外部ライブラリを持たない**カーネル中立の型。
#
#   点群を受け取る外部ライブラリは 5 つあり (CGAL / geogram / OCCT / OpenVDB / manifold)、
#   そのどれもが「平坦な double 配列」で受け取る (API の一覧は modules/points/h/pt/c++/ptCloud.h)。
#   メッシュと違って保存すべきカーネル固有表現が無いので、型をどれか 1 つのカーネルに置くと
#   残り 4 つがそこへ依存する (cgal に置けば geogram / manifold / openvdb が GPL を引き込む)。
#   ⇒ 本体クラス + codec + reader/writer を中立の libsrava_pt に置き、points.so が型を名乗る。
#   ⇒ 消費側 (voronoi / delaunay / Poisson / RANSAC …) は自分の provides に &ptCloud::WIRE を
#     並べ、LINK に srava_pt を足すだけでよい (新しいクラスも wire 形式も作らない)。
# ★ CGAL/Manifold/FetchContent 非依存なので **常にビルドする** (SRAVA_MODULE_* の条件を持たない)。
# ------------------------------------------------------------------
tinystate_codegen(pt_ts_codegen
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptsptWireCacheStreamWriterCloud.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptsptWireCacheStreamReaderCloud.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaPoints2D.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaPoints3D.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaRand.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaRandPt2D.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaRandPt3D.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaRandGaussian.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaRandGaussianPt2D.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaRandGaussianPt3D.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaRandGaussianMix2D.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaRandGaussianMix3D.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaImport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaExport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaNverts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaVert.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaBbox.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaCentroid.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaValid.cpp
  # ★ #3578: transform 一族 と union
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaTranslate.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaRotate.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaScale.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaMirror.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaTransform.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/ptaUnion.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/c++/pttsAgent.cpp)

# 本体クラスと codec/reader/writer = **モジュール共通**。cg/gg/vd と同じ理由で共有ライブラリにする
# (各 .so がコピーを持つと in-proc で d_cast が失敗する)。
set(PT_GEOM_SRC
  modules/points/c++/ptsptWireCacheStreamWriterCloud.cpp
  modules/points/c++/ptsptWireCacheStreamReaderCloud.cpp
  modules/points/c++/ptCacheCodec.cpp
  modules/points/c++/ptCloud.cpp
  modules/points/c++/ptRandom.cpp)   # ★ 乱数の生成器と区間の写像 (op ではなく型と同じ側)
add_library(srava_pt SHARED ${PT_GEOM_SRC})

# ★★★ #3576 (2026-09-22): ptRandom.cpp だけ **FMA 融合を禁じる**。
#   rand 系の約束は「同じ引数なら同じ結果・**OS と機械をまたいでも**」。ところが
#   @a*b+c@ を 1 命令 (FMA) に融合すると **丸めが 2 回から 1 回に減って値が変わる**。
#   融合するかは *コンパイラと -march の裁量* なので、同じソースが機で違う値を出す。
#   ⚠⚠ これは「libm が機に依る」のとは **別の穴**。ソースを自前にしても塞がらない
#     (自前の pt_log_det が 13 か所・next_gauss が 1 か所・next_flt が 1 か所 融合されていた)。
#   ⇒ 実測 (2026-09-22・bench): Linux で -mfma -ffp-contract=fast を付けると
#     **mac (arm64/clang) の黄金値がそのまま再現した** = 機の差ではなく融合の差と確定。
#   ★ off にすれば FMA 命令のある機でも融合しない ⇒ 両機で一致する。
#   ⚠ next_flt も融合されるので、**#3572 の rand の浮動小数も同じ病気**だった
#     (検定にビット単位の黄金値が無く見えていなかった)。この 1 行が両方を塞ぐ。
#   ⚠⚠ **一覧に無いコンパイラは黙って無防備になる** — それが一番まずい形なので、
#     知らないコンパイラは *止める*。約束 (OS をまたいでビット一致) が載っている所なので、
#     「気づかないまま破れている」より「建たない」方がよい (黙るフォールバック禁止)。
#   ★ Windows はこの木では **Cygwin / MinGW の GCC** (cygbuild.sh ・ if(CYGWIN)) なので
#     GNU の枝で拾える。MSVC の口は無い ⇒ 足すときに **フラグを調べて**ここへ書くこと。
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang|IntelLLVM")
  set_source_files_properties(modules/points/c++/ptRandom.cpp
    PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
else()
  message(FATAL_ERROR
    "rand needs bit-identical floating point across machines, but this build does not know "
    "how to disable FMA contraction for '${CMAKE_CXX_COMPILER_ID}'. Add the right flag for "
    "this compiler next to modules/points/c++/ptRandom.cpp in CMakeLists.txt "
    "(MSVC: /fp:precise with contraction off). Do not just widen the match: without the flag "
    "rand_gaussian and rand's float path return different values on different machines.")
endif()

# ⚠ **FMA だけが穴ではない**。32bit x86 を x87 で建てると中間結果が 80bit で持たれ
#   (FLT_EVAL_METHOD=2)、-ffp-contract=off でも値がずれる。⇒ その構成を作るなら
#   -mfpmath=sse -msse2 も要る。★ いまの対象は x86-64 / arm64 だけなので該当しない
#   (どちらも既定が SSE2 / NEON で余分な精度を持たない)。
#   ⇒ **見張りは検定側** — test/srava_rand_gaussian.sh の黄金値が唯一の番人なので、
#     新しい OS / 構成を足したら **必ずその 1 本を走らせる**こと。
set_target_properties(srava_pt PROPERTIES OUTPUT_NAME "srava_pt" INSTALL_RPATH "$ORIGIN")
srava_export_all(srava_pt)   # ★ Windows: 明示エクスポート
target_link_libraries(srava_pt PUBLIC pig)
target_compile_definitions(srava_pt PRIVATE SRAVA_VALUE_ONLY)
target_include_directories(srava_pt PUBLIC ${SRAVA_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/h)
add_dependencies(srava_pt pigts_codegen pt_ts_codegen gen_srava_parser)
install(TARGETS srava_pt LIBRARY DESTINATION lib RUNTIME DESTINATION bin)

srava_add_module(points_module
  OUTPUT   points
  SOURCES  modules/points/c++/ptaPoints2D.cpp
           modules/points/c++/ptaPoints3D.cpp
           modules/points/c++/ptaRand.cpp
           modules/points/c++/ptaRandPt2D.cpp
           modules/points/c++/ptaRandPt3D.cpp
           modules/points/c++/ptaRandGaussian.cpp
           modules/points/c++/ptaRandGaussianPt2D.cpp
           modules/points/c++/ptaRandGaussianPt3D.cpp
           modules/points/c++/ptaRandGaussianMix2D.cpp
           modules/points/c++/ptaRandGaussianMix3D.cpp
           modules/points/c++/ptaImport.cpp
           modules/points/c++/ptaExport.cpp
           modules/points/c++/ptaNverts.cpp
           modules/points/c++/ptaVert.cpp
           modules/points/c++/ptaBbox.cpp
           modules/points/c++/ptaCentroid.cpp
           modules/points/c++/ptaValid.cpp
           # ★ #3578: transform 一族 と union
           modules/points/c++/ptaTranslate.cpp
           modules/points/c++/ptaRotate.cpp
           modules/points/c++/ptaScale.cpp
           modules/points/c++/ptaMirror.cpp
           modules/points/c++/ptaTransform.cpp
           modules/points/c++/ptaUnion.cpp
           modules/points/c++/pttsAgent.cpp
           modules/points/manifest.cpp
  DEPENDS  pigts_codegen pt_ts_codegen gen_srava_parser
  LINK     srava_pt
  DEFINES  SRAVA_VALUE_ONLY
  INCLUDES ${SRAVA_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/h
  INSTALL)

# points.so が dlopen でき記述子が読めること + 型軸に pt-cloud2d / pt-cloud3d が登録されること。
add_test(NAME srava_module_points_so
  COMMAND srava_module_probe $<TARGET_FILE:points_module>)
set_tests_properties(srava_module_points_so PROPERTIES
  PASS_REGULAR_EXPRESSION "MODULE_OK name=points .*make_agent=yes")
add_test(NAME srava_type_points
  COMMAND srava_module_probe $<TARGET_FILE:points_module>)
set_tests_properties(srava_type_points PROPERTIES
  PASS_REGULAR_EXPRESSION "TYPES pt-cloud2d,pt-cloud3d")

# ------------------------------------------------------------------
# geomutils.so — 素性を訊く計算の中立モジュール (#3527)。**外部ライブラリを持たない**。
#
#   bbox / area / centroid / valid / 位相 の中身は既に src/h/common/meshprops.h に定義ごと
#   1 本で書かれており、manifold / geogram / cherchi はどれも内部表現から素の配列へ写して
#   そこを通っている (mfMesh.cpp:1443 / ggMesh.cpp:246 …)。⇒ 実質「同じ型」を 3 者が名乗らずに
#   作っていた。型として名乗らせ、1 モジュールが所有する (ひさ設計 2026-09-17)。
#   ★ 2D も同じ相似形 (3D = 符号つき体積が正のシェル / 2D = 符号つき面積が正のリング) なので
#     ringprops.h を対で新設し、型は 3 本 (gu-mesh3d / gu-cross2d / gu-face3d)。
#   ★★ 4CC は manifold / geogram / cherchi と **共有** (MFM3 / MFC2)。こちらが書いた cache を
#     向こうの create_for_meta が無改造で読めるので、往復に新しい変換が要らない。
# ★ CGAL/Manifold/FetchContent 非依存なので **常にビルドする** (points.so と同じ)。
# ------------------------------------------------------------------
tinystate_codegen(gu_ts_codegen
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/ptsguWireCacheStreamWriterGeom.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/ptsguWireCacheStreamReaderGeom.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaArea.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaBbox.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaCentroid.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaGenus.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaNfaces.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaNparts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaNshells.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaNverts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaValid.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaCast.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaVolume.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaPart.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaPartAt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaDistanceAt.cpp   # ★ #3553
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaShell.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaShellAt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaPtIntersection.cpp   # ★ #3579
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaPtDifference.cpp     # ★ #3579
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaVert.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaVerts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/guaFaceVerts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/c++/gutsAgent.cpp)

# 本体クラスと codec/reader/writer = **モジュール共通**。各 .so がコピーを持つと in-proc で
# d_cast が失敗するので共有ライブラリにする (cg/gg/vd/pt と同じ理由)。
set(GU_GEOM_SRC
  modules/geomutils/c++/ptsguWireCacheStreamWriterGeom.cpp
  modules/geomutils/c++/ptsguWireCacheStreamReaderGeom.cpp
  modules/geomutils/c++/guCacheCodec.cpp
  modules/geomutils/c++/guGeom.cpp)
# ★ #3527 段 5: guCacheCodec.cpp が &ptCloud::WIRE を並べる (verts が返す **値の器** を借りる)
#   ので srava_pt に依存する。cgal / geogram と同じ形。
add_library(srava_gu SHARED ${GU_GEOM_SRC})
set_target_properties(srava_gu PROPERTIES OUTPUT_NAME "srava_gu" INSTALL_RPATH "$ORIGIN")
srava_export_all(srava_gu)   # ★ Windows: 明示エクスポート
target_link_libraries(srava_gu PUBLIC pig srava_pt)
target_compile_definitions(srava_gu PRIVATE SRAVA_VALUE_ONLY)
target_include_directories(srava_gu PUBLIC ${SRAVA_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/h
                                          ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/h)
add_dependencies(srava_gu pigts_codegen gu_ts_codegen gen_srava_parser)
install(TARGETS srava_gu LIBRARY DESTINATION lib RUNTIME DESTINATION bin)

srava_add_module(geomutils_module
  OUTPUT   geomutils
  SOURCES  modules/geomutils/c++/guaCast.cpp
           modules/geomutils/c++/guaArea.cpp
           modules/geomutils/c++/guaBbox.cpp
           modules/geomutils/c++/guaCentroid.cpp
           modules/geomutils/c++/guaGenus.cpp
           modules/geomutils/c++/guaNfaces.cpp
           modules/geomutils/c++/guaNparts.cpp
           modules/geomutils/c++/guaNshells.cpp
           modules/geomutils/c++/guaNverts.cpp
           modules/geomutils/c++/guaValid.cpp
           modules/geomutils/c++/guaVolume.cpp
           modules/geomutils/c++/guaPart.cpp
           modules/geomutils/c++/guaPartAt.cpp
           modules/geomutils/c++/guaDistanceAt.cpp   # ★ #3553
           modules/geomutils/c++/guaShell.cpp
           modules/geomutils/c++/guaShellAt.cpp
           modules/geomutils/c++/guaPtIntersection.cpp   # ★ #3579
           modules/geomutils/c++/guaPtDifference.cpp     # ★ #3579
           modules/geomutils/c++/guaVert.cpp
           modules/geomutils/c++/guaVerts.cpp
           modules/geomutils/c++/guaFaceVerts.cpp
           modules/geomutils/c++/gutsAgent.cpp
           modules/geomutils/manifest.cpp
  DEPENDS  pigts_codegen gu_ts_codegen gen_srava_parser
  LINK     srava_gu srava_pt
  DEFINES  SRAVA_VALUE_ONLY
  INCLUDES ${SRAVA_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/modules/geomutils/h
           ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/h
  INSTALL)

# ★ #3579: 点群と mesh の積と差。⚠ 番犬の予算は素の所要を測ってから (既定 10 秒)。
# ⚠ points は常に建つ (option が無い) ので manifold だけ見る。★ 存在しない変数で
#   囲うと **テストが黙って登録されない** — 1 度そう書いた (SRAVA_MODULE_POINTS)。
if(SRAVA_MODULE_MANIFOLD)
  add_test(NAME srava_ptsplit
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_ptsplit.sh
            $<TARGET_FILE:srava> manifold.so)
  set_tests_properties(srava_ptsplit PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/ptsplitcache"
    PASS_REGULAR_EXPRESSION "PTSPLIT-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|PTSPLIT_FAIL|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endif()

# geomutils.so が dlopen でき記述子が読めること + 型軸に 3 本が登録されること (#3527 段 1)。
add_test(NAME srava_module_geomutils_so
  COMMAND srava_module_probe $<TARGET_FILE:geomutils_module>)
set_tests_properties(srava_module_geomutils_so PROPERTIES
  PASS_REGULAR_EXPRESSION "MODULE_OK name=geomutils .*make_agent=yes")
add_test(NAME srava_type_geomutils
  COMMAND srava_module_probe $<TARGET_FILE:geomutils_module>)
set_tests_properties(srava_type_geomutils PROPERTIES
  PASS_REGULAR_EXPRESSION "TYPES gu-mesh3d,gu-cross2d,gu-face3d")

# hull(点群) の回帰 (#3528 続き)。★ "(*!)" = 分解禁止が効いていることの回帰でもある。
if(SRAVA_MODULE_CGAL)
  add_test(NAME srava_hull_points
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_hull_points.sh $<TARGET_FILE:srava>)
  set_tests_properties(srava_hull_points PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/hullptcache"
    PASS_REGULAR_EXPRESSION "HULLPT_OK"
    FAIL_REGULAR_EXPRESSION "HULLPT_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly")
endif()

# 点群の振る舞い回帰 (往復 / 法線 / 閉形式 / 明示エラー)。意図は test/srava_points.sh 冒頭。
add_test(NAME srava_points
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_points.sh $<TARGET_FILE:srava>)
set_tests_properties(srava_points PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/ptcache"
  PASS_REGULAR_EXPRESSION "POINTS_OK"
  FAIL_REGULAR_EXPRESSION "POINTS_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly")

# 擬似乱数 rand (行 rand / rand#pt2d / rand#pt3d) の回帰。意図は test/srava_rand.sh 冒頭。
#   ★★★ 検定の本体は「同じ引数なら必ず同じ結果」 — **別のキャッシュ dir で 2 回**走らせて
#     一致することと、**ハードコードした黄金値**に一致すること (後者だけが OS をまたぐ)。
add_test(NAME srava_rand
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_rand.sh $<TARGET_FILE:srava>)
set_tests_properties(srava_rand PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/randcache"
  PASS_REGULAR_EXPRESSION "RAND_OK"
  FAIL_REGULAR_EXPRESSION "RAND_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly")

# 正規分布の擬似乱数 rand_gaussian (#3576)。意図は test/srava_rand_gaussian.sh 冒頭。
#   ★ SRAVA_HANG_SECS は **書かない** — 素の所要は 1.61 秒 (mac 実測 2026-09-22 ・ #3577 込み) で
#     既定 10 秒に対し余裕 x6.2。⇒ 番犬が空振りしない。
#     ⚠ 一度 50 秒まで膨らんだ: 2000 点を srava の for で vert() して数えていたため (48.6 秒)。
#       ⇒ centroid で O(1) に置き換えた。**同じ情報が O(1) で取れるときにループを書かない**。
#     ⚠ 遅い機で 3.3 秒を越えたら予算を書くこと (両機の遅い方 x3 が原則 ・ 0df54df)。
add_test(NAME srava_rand_gaussian
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_rand_gaussian.sh $<TARGET_FILE:srava>)
set_tests_properties(srava_rand_gaussian PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/gausscache"
  PASS_REGULAR_EXPRESSION "GAUSS_OK"
  FAIL_REGULAR_EXPRESSION "GAUSS_FAIL|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 120)

# Voronoi (#3525) の振る舞い回帰。cgal・2D。意図は test/srava_voronoi.sh 冒頭。
#   ★ cgal (voronoi / part / nparts の 2D) と points.so (サイトの点群) の両方が要る。
if(SRAVA_MODULE_CGAL)
  add_test(NAME srava_voronoi
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_voronoi.sh $<TARGET_FILE:srava>)
  # ⚠⚠ **TIMEOUT を必ず置く** (bench・2026-09-16)。置かずに合流したところ、Linux (CGAL 6.0.1) で
  #   3D の voronoi が **CGAL の corefinement 内で無限ループ**し、ctest が **80 分止まった**
  #   (ctest の既定 TIMEOUT は 1500 秒だが、他の並列枠を道連れにするので実質そこで終わる)。
  #   ★ 番犬が T+10s で撮った bt が真因を名指ししている:
  #       CGAL::PMP::Corefinement::compute_border_edge_map (face_graph_utils.h:1848)
  #         → next_marked_halfedge_around_target_vertex    (face_graph_utils.h:851)
  #     = 印のある稜に当たるまで頂点まわりを回る関数で、**見つからないと戻らない**。
  #   ⇒ 最小再現は **2 サイトで足りる**:
  #       voronoi(points3d([[0.5,1.0,1.0],[1.5,1.0,1.0]]), [[0,0,0],[2,2,2]])
  #     1 サイトは通る / 2D は通る ⇒ 3D の最初の clip で起きる。
  #   ⚠ 併せて ⑨ の同一平面 10 サイトは **SIGSEGV** (別の壊れ方)。
  #   ★ mac (macMINI) では 487/487 緑 ⇒ **CGAL の版差**の疑い。#3525 の担当で切り分け中。
  #   ⚠ SRAVA_HANG_SECS も要る — この本は 10 秒を越えるので、既定のままだと
  #     *緑のときも* 毎回 /tmp/srava-hang/ に捕獲が残り、本物が埋もれる。
  set_tests_properties(srava_voronoi PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/vorcache;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_HANG_SECS=120"
    PASS_REGULAR_EXPRESSION "VORONOI_OK"
    FAIL_REGULAR_EXPRESSION "VORONOI_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly"
    TIMEOUT 300)
endif()

# ★ estimate_normals は **1 つの op に実装が 2 つ** — cgal (pca + mst) と geogram (Co3Ne)。
#   両方ロードしていれば priority で cgal (20 > 6) が受け、"geogram"::estimate_normals で名指せる。
#   ⇒ cgal が要る (型と points.so は要るが、推定の実装がここに 1 本も無いと検定できない)。
#   geogram があれば 2 実装の比較とキャッシュ分離まで見る。意図は test/srava_estimate_normals.sh 冒頭。
if(SRAVA_MODULE_CGAL)
  if(SRAVA_MODULE_GEOGRAM)
    set(_EN_GG geogram)
  else()
    set(_EN_GG "")
  endif()
  add_test(NAME srava_estimate_normals
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_estimate_normals.sh $<TARGET_FILE:srava> ${_EN_GG})
  set_tests_properties(srava_estimate_normals PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/encache"
    PASS_REGULAR_EXPRESSION "EN_OK"
    FAIL_REGULAR_EXPRESSION "EN_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly")

  # ★★ #3527: vert(m, i) — 「数える」(nverts) と「取り出す」(vert) が **同じ列を同じ順**で
  #   見ていることの検査。⚠ 本体は ② の **索引の再現性** (索引は実装依存で、値としては
  #   常に「正しい頂点のどれか」なので **値の検査では壊れても捕まらない**)。
  #   ⇒ *別のキャッシュ dir で計算し直しても並びが同じか* を見る (#3518 の face と同じ形)。
  #   ⚠ 同じ cache dir で 2 回流すのは検定にならない (2 回目は HIT で読み直すだけ)。
  # ★★ #3527: shell(m, i) — 殻 (面の連結成分) を取り出す。★ 検査の本体は
  #   **Σ 符号つき体積 == 全体の体積**。向きを保つ (案 A) からこそ成り立つ性質で、
  #   向きを揃える案 (B) にすると 64+8=72 になって落ちる ⇒ **設計の選択を検査が区別する**。
  # ★ shell / shell_at を 1 本で見る。⑦ が位置で指す側 (同距離は断る)。
  add_test(NAME srava_shell
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_shell.sh $<TARGET_FILE:srava> cgal.so)
  set_tests_properties(srava_shell PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/shellcache"
    PASS_REGULAR_EXPRESSION "SHELL-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly"
    TIMEOUT 180)

  add_test(NAME srava_vert
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_vert.sh $<TARGET_FILE:srava> cgal.so)
  set_tests_properties(srava_vert PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/vertcache"
    PASS_REGULAR_EXPRESSION "VERT-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly"
    TIMEOUT 120)
endif()

# ==================================================================
# .so 化 Phase 3b/3c: カーネル .so (cgal.so / manifold.so) + dlopen ローダ検証プローブ。
#   幾何ソース (AGENT_HOST_SRC / CGAL_KERNEL_SRC) は srava_agent の直前で定義済み。
#   srava_module_probe は .so を dlopen して記述子取得 + 全シンボル解決を検証する。
#   ★ カーネル .so は pig/pts/tinyState を bundle しない。host (probe / srava_agent) が -rdynamic で
#     export → dlopen(RTLD_NOW) が .so の未解決シンボルを host から解決する (docs §1.2)。
# ==================================================================

# cgal.so: 幾何 + manifest。CGAL は bundle・pig/pts/tinyState は未解決のまま (host 供給)。
# ★ SRAVA_MODULE_CGAL=OFF では cgal.so を作らない (CGAL 非依存ビルド)。
if(SRAVA_MODULE_CGAL)
# ★ #3434 (2026-08-22・案 B'): cgal の**幾何クラスを共有ライブラリへ**。
#   openvdb_cg.so が **本物の cgMesh** を受け取るため (各 .so がコピーを持つと in-proc で
#   d_cast が失敗する)。境界は manifold と同じ: cga*.cpp = op / それ以外 = 幾何クラスと codec。
set(CG_GEOM_SRC
  modules/cgal/c++/ptscgWireCacheStreamWriterMesh.cpp
  modules/cgal/c++/ptscgWireCacheStreamReaderMesh.cpp
  modules/cgal/c++/cgCacheCodec.cpp
  modules/cgal/c++/cgMesh.cpp
  modules/cgal/c++/cgMesh3D.cpp
  modules/cgal/c++/cgMesh2D.cpp
  modules/cgal/c++/cgHull.cpp
  modules/cgal/c++/cgVoronoi.cpp
  modules/cgal/c++/cgDelaunay.cpp
  modules/cgal/c++/cgLoft.cpp
  modules/cgal/c++/cgRemesh.cpp
  modules/cgal/c++/cgEstimateNormals.cpp)   # ★ #3545 段 2: 点集合処理 (EPICK) を op から引き取った
add_library(srava_cg SHARED ${CG_GEOM_SRC})
set_target_properties(srava_cg PROPERTIES OUTPUT_NAME "srava_cg" INSTALL_RPATH "$ORIGIN")
srava_export_all(srava_cg)   # ★ Windows: 明示エクスポート
# ★ #3528: cgCacheCodec.cpp が &ptCloud::WIRE を並べる (点群型を借りる) ので srava_pt に依存する。
target_link_libraries(srava_cg PUBLIC pig srava_pt CGAL::CGAL)
target_compile_definitions(srava_cg PRIVATE SRAVA_VALUE_ONLY)
target_include_directories(srava_cg PUBLIC ${SRAVA_GEN})
add_dependencies(srava_cg pigts_codegen gen_srava_parser)
install(TARGETS srava_cg LIBRARY DESTINATION lib RUNTIME DESTINATION bin)

list(REMOVE_ITEM CGAL_KERNEL_SRC ${CG_GEOM_SRC})

srava_add_module(cgal
  OUTPUT   cgal
  SOURCES  ${CGAL_KERNEL_SRC} modules/cgal/manifest.cpp
  DEPENDS  pigts_codegen gen_srava_parser
  # ★ #3528: srava_pt = 点群の本体クラス + codec + reader/writer (カーネル中立)。
  #   estimate_normals が **本物の ptCloud** を受け取るために借りる (コピーを持つと in-proc で
  #   d_cast が失敗する)。新しいクラスも wire 形式も作らない。
  LINK     srava_cg srava_pt CGAL::CGAL
  DEFINES  SRAVA_VALUE_ONLY
  INCLUDES ${SRAVA_GEN}
  INSTALL)
endif()

# srava_module_probe: .so を dlopen して記述子を検証する host (§8 並走確認・3c srava_agent の原型)。
add_executable(srava_module_probe
  src/main/srava_module_probe_main.cpp
  # ${AGENT_HOST_SRC} は libpig(共有) に移動 (2026-08-12)
)
add_dependencies(srava_module_probe pigts_codegen gen_srava_parser)
target_include_directories(srava_module_probe PRIVATE ${SRAVA_GEN})
target_compile_definitions(srava_module_probe PRIVATE SRAVA_VALUE_ONLY)
target_link_libraries(srava_module_probe PRIVATE pig Threads::Threads m ${CMAKE_DL_LIBS})
set_target_properties(srava_module_probe PROPERTIES ENABLE_EXPORTS ON)   # -rdynamic

# ★ #3439 ⑦: 記述子の自己矛盾検査の自己テスト (import/export op を持つのに exts 未申告は違反)。
#   壊れた .so をビルドせずに規則だけを検証する。cgal 等の有無に依存しないのでガード外。
add_test(NAME srava_descriptor_violation
  COMMAND srava_module_probe --selftest)
set_tests_properties(srava_descriptor_violation PROPERTIES
  PASS_REGULAR_EXPRESSION "DESCRIPTOR-VIOLATION-SELFTEST-OK"
  FAIL_REGULAR_EXPRESSION "SELFTEST-FAIL|Segmentation|Aborted"
  TIMEOUT 20)

# cgal.so が dlopen でき、記述子 (make_agent 付き) が読めることを検証。
# ★ cgal.so を作らない SRAVA_MODULE_CGAL=OFF では登録しない (probe 自体は他カーネルで使う)。
if(SRAVA_MODULE_CGAL)
add_test(NAME srava_module_cgal_so
  COMMAND srava_module_probe $<TARGET_FILE:cgal>)
set_tests_properties(srava_module_cgal_so PROPERTIES
  PASS_REGULAR_EXPRESSION "MODULE_OK name=cgal .*make_agent=yes")

# ★ rev4 Phase A: cgal.so の manifest 静的初期化が型軸レジストリへ実装型を登録できること
# (cg-mesh3d=MESH / cg-cross2d=PLY2)。type_name↔タグ 1:1 の土台配線の end-to-end 検証。
add_test(NAME srava_type_cgal
  COMMAND srava_module_probe $<TARGET_FILE:cgal>)
set_tests_properties(srava_type_cgal PROPERTIES
  PASS_REGULAR_EXPRESSION "TYPES cg-mesh3d,cg-cross2d")

# ★ 2026-08-19: 非幾何型の判定が **構造** (op を 1 つも持たないモジュールが申告する型) で
#   行われていること。agent の「消費できる型リスト」はこれを使って非幾何型を除くので、
#   名前の列挙 ("value"/"ref" を除く) をどこにも持たなくてよい。
#   probe が積むのは組込 "pig" + 対象 .so の 2 つだけなので、期待値は **どの .so でも同じ**
#   (組込の value,ref だけ・幾何モジュールの型は入らない)。
add_test(NAME srava_type_nongeom
  COMMAND srava_module_probe $<TARGET_FILE:cgal>)
set_tests_properties(srava_type_nongeom PROPERTIES
  PASS_REGULAR_EXPRESSION "OPLESS value,ref")
endif()

# ★ test_cgatsagent は cgatsAgentTest.cpp (CGAL include) + srava_agent 経由で cgal.so を要する
#   cgal 専用テスト → SRAVA_MODULE_CGAL=OFF ではビルド/登録しない。
if(SRAVA_MODULE_CGAL)
add_executable(test_cgatsagent
  src/main/cgatsagent_main.cpp
  src/main/cgal_test_fixture.cpp             # Phase D-1: 旧 pigfModuleAgent placeholder の移設先
  src/classes/cg/c++/cgptsLemonParser.cpp   # pigfAgent が値結果を VALUE パース(対称化)
  src/classes/pig/c++/pigfOps.cpp            # pigDataOperatorVariable(値規則 IDENT 用)
  modules/cgal/c++/cgatsAgentTest.cpp
  ${SRAVA_GEN}/ns_sravaParser.cpp            # 値規則のみ(SRAVA_VALUE_ONLY)
)
add_dependencies(test_cgatsagent pigts_codegen srava_agent gen_srava_parser)
target_include_directories(test_cgatsagent PRIVATE ${SRAVA_GEN})
target_compile_definitions(test_cgatsagent PRIVATE SRAVA_VALUE_ONLY)
target_link_libraries(test_cgatsagent PRIVATE pig Threads::Threads m)
add_test(NAME cgatsagent COMMAND test_cgatsagent)
set_tests_properties(cgatsagent PROPERTIES
  ENVIRONMENT SRAVA_AGENT=$<TARGET_FILE:srava_agent>)
endif()   # SRAVA_MODULE_CGAL (test_cgatsagent)

# ------------------------------------------------------------------
# step17 cgptsPlanner: プランナープロセス srava(ptsApplication 派生 = プランナー実態元祖)
#   起動時スイープ → パーズ(スタブ手組み union(box,box)) → 評価 → 終了時クリーンアップ → exit_code
#   CGAL 非リンク(プランナーは CGAL 非依存)。エージェントは srava_agent を ts2System で起動。
# ------------------------------------------------------------------
add_executable(srava
  src/main/srava_main.cpp
  src/classes/cg/c++/cgptsLemonParser.cpp
  src/classes/cg/c++/cgptsPlanner.cpp
  ${SRAVA_GEN}/ns_sravaParser.cpp
)
add_dependencies(srava pigts_codegen srava_agent gen_srava_parser)
target_include_directories(srava PRIVATE ${SRAVA_GEN})
target_link_libraries(srava PRIVATE pig Threads::Threads m)
# ★ .so 化 Phase 3c-2: planner は起動時にカーネル .so を dlopen し in-proc thread 実行する。
#   dlopen した .so の未解決 pig/pts シンボルを planner 本体から解決させるため -rdynamic で export。
set_target_properties(srava PROPERTIES ENABLE_EXPORTS ON)

# ==================================================================
# ★★ アロケータの既定 (2026-09-07・#3493)
#   Linux は **jemalloc**・それ以外は system malloc。
#
#   理由: mimalloc で回すと ts2 の GC が「vptr を 0 で潰された **生きた** オブジェクト」を
#   delete して agent が SIGSEGV する (#3493)。mimalloc は解放ブロックの **先頭 8 バイトに
#   free-list のリンク** を書く = オブジェクトなら vptr の位置なので、別所の欠陥が
#   vptr の破壊として顔を出す。同一ビルドで繰り返すと **mimalloc でだけ**この SIGSEGV が出て、
#   glibc / jemalloc では出ない (試行数と内訳は #3493)。
#   ⚠ これは **対症療法**。#3493 の真因 (生きた領域への野良書き込み) は未解決。
#     切り分けのときは敢えて mimalloc を LD_PRELOAD すると露出しやすい。
#
#   ⚠ macOS を既定にしない理由は **jemalloc 固有**: Homebrew の jemalloc は prefix つき
#     (je_malloc) で入ることが多く、リンクしても malloc は差し替わらない。
#   ★ 2026-09-07 訂正 (Darwin 実機で確認): ここは以前「Darwin は Linux 式の
#     シンボル差し替えをしない」と一般化して書いていたが、**mimalloc には当てはまらない**。
#     mimalloc の dylib は自身に `__DATA,__interpose` を持つので、**-lmimalloc を足すだけで
#     malloc も operator new も置き換わる** (静的リンクと素の -lmimalloc の両方で
#     mi_is_in_heap_region() が両者に対して真を返すことを確認済み)。dlopen した .so にも届く。
#     ⚠ DYLD_INSERT_LIBRARIES は SIP 非保護の実行体には効くが、**/bin/sh 経由では剥がされる**
#       (ctest のテストは全部シェル経由なので、そこでは効かない)。リンクの方が確実。
#     ⇒ Darwin で mimalloc を選べるようにするかは **保留** (ひさ判断 2026-09-07・測定を
#       終えてから決める)。どちらにせよ auto の既定にはしない — mimalloc で露出しないことと
#       安全であることは別で、#3493 の真因は未解決のままであるため。
#   ⚠ リンクするのは **実行体だけ**。モジュール (.so) には付けない (プロセスに 1 つの原則)。
#   ⚠ --no-as-needed が要る。本体は jemalloc のシンボルを直接呼ばないので、既定の
#     --as-needed だと DT_NEEDED ごと落ちて何も置き換わらない。push-state で局所化する。
set(SRAVA_ALLOCATOR "auto" CACHE STRING "malloc 実装: auto (Linux=jemalloc) / jemalloc / system")
set_property(CACHE SRAVA_ALLOCATOR PROPERTY STRINGS auto jemalloc system)
if(SRAVA_ALLOCATOR STREQUAL "auto")
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_srava_alloc jemalloc)
  else()
    set(_srava_alloc system)
  endif()
else()
  set(_srava_alloc "${SRAVA_ALLOCATOR}")
endif()
if(_srava_alloc STREQUAL "jemalloc")
  find_library(SRAVA_JEMALLOC NAMES jemalloc libjemalloc.so.2)
  if(SRAVA_JEMALLOC)
    foreach(_t srava srava_agent)
      target_link_libraries(${_t} PRIVATE
        -Wl,--push-state,--no-as-needed "${SRAVA_JEMALLOC}" -Wl,--pop-state)
    endforeach()
    message(STATUS "allocator: jemalloc (${SRAVA_JEMALLOC})")
  elseif(SRAVA_ALLOCATOR STREQUAL "jemalloc")
    message(FATAL_ERROR
      "SRAVA_ALLOCATOR=jemalloc だが libjemalloc が見つからない。\n"
      "  libjemalloc-dev を入れるか -DSRAVA_ALLOCATOR=system を指定すること。")
  else()
    message(WARNING "allocator: jemalloc が無いので system malloc を使う (#3493 の回避が効かない)")
  endif()
else()
  message(STATUS "allocator: system malloc")
endif()
# ==================================================================

# ★ in-proc 実行体の host インフラ (ptsCalcBody / D_REF codec) を planner に **無条件で**リンクする
#   (2026-08-10)。カーネル .so (d2/d3/manifold/pipe) は ptsCalcBody 派生の計算本体を bundle せず host
#   供給に頼る。planner は起動時に探索路の全 .so を dlopen して記述子を読むが、typeinfo (_ZTI…) は
#   データシンボルなので RTLD_LAZY でも即解決される → ptsCalcBody を持たない planner はそれを使う .so
#   (d3.so 等) を **PROCESS カーネルであっても**ロードできず落ちる。以前は manifold/pipeprox ブロックが
#   条件付きで足していたため、両方 OFF (例: SRAVA_MODULE_CGAL=OFF の最小構成) で d2/d3 がロード不能だった。
#   常に足すことで「どのカーネル集合でも planner が全 .so を dlopen できる」を保証する。
target_sources(srava PRIVATE
  src/classes/pig/c++/ptsCalcBody.cpp            # in-proc 計算本体基盤 (.so 非 bundle・全カーネルが依存)
  src/classes/pig/c++/ptsGenericAgent.cpp        # モジュール実行体の共通基底 (in-proc 派生 agent が使う host インフラ)
  src/classes/pig/c++/pigRefCacheCodec.cpp       # D_REF codec (in-proc pigDataCache 共有)
  src/classes/pig/c++/ptsWireCacheStreamWriterRef.cpp
  src/classes/pig/c++/ptsWireCacheStreamReaderRef.cpp
)

# ------------------------------------------------------------------
# demo.so — 第3モジュール実証 (docs §7 Phase 6・完成条件)。CGAL も Manifold も FetchContent も
#   使わない最小の value-only モジュール (demo_add / demo_range)。**ホストを一切改修せず**、この .so を
#   探索路 (planner/srava_agent と同 dir) に置くだけで新 op が使えることを示す。
#   EXEC_PROCESS 専用 = 2 個目の in-proc thread カーネル境界 (⑤・保留中) を踏まない。
#   スクリプト側は `module("demo.so",{priority:99})` で demo を既定カーネルにする (opt-in)。
# ------------------------------------------------------------------
# ★ #3417 (2026-09-06): demaCompute (ptsCalcBody 派生) を追加。実行体は待ち状態のまま
#   イベントを受け続け、計算は calc の専用 thread で回す — ppatsAgent + ppaCompute と同じ形。
tinystate_codegen(demo_ts_codegen
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/demo/dematsAgent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/demo/demaCompute.cpp)
# ★ INSTALL を付けない = 配布物には入れない (第3モジュールの実証用でテスト専用)。
srava_add_module(demo_module
  OUTPUT   demo
  SOURCES  modules/demo/dematsAgent.cpp
           modules/demo/demaCompute.cpp
           modules/demo/demo_compute.cpp
           modules/demo/manifest.cpp
  DEPENDS  pigts_codegen demo_ts_codegen
  INCLUDES modules/demo)

# demo.so が dlopen でき記述子 (make_agent 付き) が読めることを検証 (RTLD_NOW 全解決)。
add_test(NAME srava_module_demo_so
  COMMAND srava_module_probe $<TARGET_FILE:demo_module>)

# ---- ★ #3503: in-proc の居座り panic。d4 (in-proc テストモジュール) の d4_wedge が
#   中断要求を無視するので、実カーネルでは作れない「in-proc なのに居座る」を再現できる。
#     off = 既定では panic しない (居座る)
#     on  = panic するが **子プロセスを迷子にしない** (demo=process 専用と同時に走らせる)
#   ⚠ RUN_SERIAL — 生きている agent プロセスを数えるので、並行するテストの agent が混ざると誤判定する。
# ★ #3521 (2026-09-15): 撃ち方は OS で違う。Windows は MSYS の kill が **native プロセスに
#   届かない** (しかも rc=0 を返す) ので、win_sigbreak 経由で新プロセスグループへ CTRL_BREAK を送る。
#   ⚠ これを入れるまで **off は「届かないので居座った」を「期待どおり居座った」と空振りで
#     合格**していた (box で実測)。⚠ win_sigbreak は if(WIN32) の中でしか定義されない。
set(_ip_env "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/inproc-panic;SRAVA_MODULE_PATH=${CMAKE_BINARY_DIR};SRAVA_HANG_SECS=90")
if(WIN32)
  set(_ip_env "${_ip_env};WIN_SIGBREAK=$<TARGET_FILE:win_sigbreak>")
endif()
foreach(_pm off on)
  add_test(NAME srava_inproc_panic_${_pm}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_inproc_panic.sh $<TARGET_FILE:srava> ${_pm})
  set_tests_properties(srava_inproc_panic_${_pm} PROPERTIES
    ENVIRONMENT "${_ip_env}"
    PASS_REGULAR_EXPRESSION "INPROC-PANIC-.*-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|core dumped"
    RUN_SERIAL TRUE
    TIMEOUT 120)
endforeach()

# ★ #3417 (2026-09-06): 撤収 (Ctrl+C 等) の回帰。demo_spin で「計算中の agent」を作り、
#   SIGINT で短時間に畳まれること・agent が残らないこと・終了コードが 130 になることを見る。
#   ⚠ demo モジュールが要る (遅い op を持つのは demo だけ。実カーネルには触らない)。
# ★★ #3541 (2026-09-15): **system() の子まで撤収が届くか** + **終了コードが 128+signum か**。
#   srava_teardown_sigint (agent 経路) と対になる検定。⚠ agent 経路では評価がエラーに落ちるので
#   ①(終了コード) が隠れており、**system 経路でしか見えない**。
#   ★ 形 A (var に受ける) と 形 B (組込の引数位置) の **両方**を回す — 経路が違うので、
#     形 A だけでは ② の退行が見えない。
#   ⚠ `x 15` を直接見ない — 15 は POSIX の raw waitpid status の表現で、Windows は raw の
#     終了コードを返すので 15 にならない。⇒ **対照 (撃たない run) との差**で見る。
#   ★ 較正: ②を直す前は 形 B が「17 秒 / x が対照と同じ」で **両方の判定が独立に赤**だった
#     (simu01 が develop-v1 e830fb9 で確認)。①を直す前は rc=1 で (c) が赤。
add_test(NAME srava_teardown_sysint
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_teardown.sh $<TARGET_FILE:srava> sysint)
# ⚠⚠ 番犬を 60 秒へ上げる (bench 指摘 2026-09-15)。このテストは **対照 (撃たない run) が
#   system("sleep 12") を完走させる**設計なので、既定 10 秒の番犬に **必ず**引っかかる。
#   ⇒ 捕獲は「テスト自身が意図的に待っているところ」(State=S / TracerPid 0 / wchan=do_wait) で、
#     ★ テストは **緑のまま** /tmp/srava-hang/ に証拠だけが溜まる = **本物が埋もれる**。
#   ⚠ 番犬はテストを落とさないので、**緑では気づけない**。mac でも実際に 26 枚溜まっていた。
# ⚠ 60 → 120 (2026-09-22): Linux 実測 30.2s で **余裕が x2.0 しかなかった**。この本は対照が
#   system("sleep 12") を完走させる設計なので所要が縮まない ⇒ 遅い機で越える。TIMEOUT は 180。
set(_td_sysint_env "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/tdcache-sysint;SRAVA_HANG_SECS=120")
# ⚠⚠ 2026-09-19: ここは以前 `if(WIN32 AND TARGET win_sigbreak)` だった。**TARGET は configure 時に
#   その場で評価される**ので、win_sigbreak の add_executable (この下の方・1899 行あたり) より前に
#   在るこの行では **常に偽**になり、WIN_SIGBREAK が **黙って落ちていた**。
#   ⇒ Windows でこのテストは POSIX の `kill -INT` 経路へ落ち、native プロセスに届かないまま
#     (しかも kill は rc=0 を返す) 子が完走し、「子まで撤収が届いていない (#3541 ②)」と
#     **製品のせいにして赤くなっていた** (box で毎回赤・実際は撃てていなかった)。
#   ★ 他の 3 箇所 (inproc_panic / teardown_sigint / teardown_ctrlc) は `if(WIN32)` だけで、
#     $<TARGET_FILE:> は **生成時**に解決されるので後方定義でも効く。⇒ そちらに揃える。
if(WIN32)
  set(_td_sysint_env "${_td_sysint_env};WIN_SIGBREAK=$<TARGET_FILE:win_sigbreak>")
endif()
set_tests_properties(srava_teardown_sysint PROPERTIES
  ENVIRONMENT "${_td_sysint_env}"
  PASS_REGULAR_EXPRESSION "TEARDOWN-SYSINT-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
  RUN_SERIAL TRUE
  TIMEOUT 180)

add_test(NAME srava_teardown_sigint
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_teardown.sh $<TARGET_FILE:srava> sigint)
# ★ #3520 (2026-09-15): 撃ち方は OS で違う。Windows は MSYS の kill が **native プロセスに
#   届かない** (しかも rc=0 を返す) ので、win_sigbreak 経由で新プロセスグループへ CTRL_BREAK を送る。
#   ⚠ win_sigbreak は if(WIN32) の中でしか定義されないので、$<TARGET_FILE:> も WIN32 でだけ使う。
set(_td_sigint_env "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/tdcache-sigint")
if(WIN32)
  set(_td_sigint_env "${_td_sigint_env};WIN_SIGBREAK=$<TARGET_FILE:win_sigbreak>")
endif()
set_tests_properties(srava_teardown_sigint PROPERTIES
  ENVIRONMENT "${_td_sigint_env}"
  PASS_REGULAR_EXPRESSION "TEARDOWN-SIGINT-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
  RUN_SERIAL TRUE
  TIMEOUT 120)


# ★★ #3482 (2026-09-20): **利用者が書いた try の中の async に撤収が届くか**。
#   ⚠⚠ agent の登録先は「囲む最も内側の try 1 つだけ」で、根の台帳へは登録しない。
#     外側から内側を畳むのは **destroy が pigData の木を伝わる**方が担う。
#     ⇒ その経路が切れると **try の中だけ畳まれない**が、srava_teardown_sigint は
#       try を使わないので気づけない。
#   ★ 判別できるのは @async@ だけ — @try { print(demo_spin(30)) }@ の形は statement1 への
#     destroy 転送で畳まれてしまい、**待ちリスト経由の転送を殺しても緑のまま**だった
#     (2026-09-20 に陰性対照で実測。async 版は 27 秒 = spin 完走になる)。
#   ★ 撃ち方は srava_teardown_sigint と同じ関数を使う (スクリプト内 shoot)。
add_test(NAME srava_teardown_tryasync
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_teardown.sh $<TARGET_FILE:srava> tryasync)
set(_td_tryasync_env "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/tdcache-tryasync")
if(WIN32)
  set(_td_tryasync_env "${_td_tryasync_env};WIN_SIGBREAK=$<TARGET_FILE:win_sigbreak>")
endif()
set_tests_properties(srava_teardown_tryasync PROPERTIES
  ENVIRONMENT "${_td_tryasync_env}"
  PASS_REGULAR_EXPRESSION "TEARDOWN-TRYASYNC-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
  RUN_SERIAL TRUE
  TIMEOUT 120)

# ★★ #3542 の退行検出 (2026-09-15・ひさ判断で srava 側に置く)。**Windows/MinGW 専用**。
#   ⚠ srava_teardown_sigint は移植性のため CTRL_BREAK で撃つ (「Ctrl+C を無視」状態の対象外)。
#     ⇒ **Ctrl+C の入口が 1 度も検定されない** — #3542 が退行しても緑のままになる。
#   ★ CREATE_NEW_PROCESS_GROUP の子は Ctrl+C が無効で、子が SetConsoleCtrlHandler(NULL,FALSE)
#     で解除して初めて届く。⇒ **CTRL_C が通ること自体が判定**になる。
#   ⚠ 対照は srava_teardown_sigint (撃ち方だけ違う同じ仕事)。あちらが緑でこちらだけ赤なら
#     違いは **イベント種別だけ**なので原因が一意に絞れる。
#   ⚠⚠ **古い tinyState では必ず赤くなる** — 修正は develop-v2 74722f5 で、公開版では
#     **v2.0.0-rc17 から**入っている (rc16 以前には無い)。
#     赤が出たら *リンクしている tinyState が古い* を先に疑う。
if(WIN32)
  add_test(NAME srava_teardown_ctrlc
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_teardown.sh $<TARGET_FILE:srava> ctrlc)
  set_tests_properties(srava_teardown_ctrlc PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/tdcache-ctrlc;WIN_SIGBREAK=$<TARGET_FILE:win_sigbreak>;SRAVA_HANG_SECS=60"
    PASS_REGULAR_EXPRESSION "TEARDOWN-CTRLC-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    RUN_SERIAL TRUE
    TIMEOUT 120)
endif()

set_tests_properties(srava_module_demo_so PROPERTIES
  PASS_REGULAR_EXPRESSION "MODULE_OK name=demo .*make_agent=yes")

# ★ 完成条件の実証: ホスト無改修で demo.so の新 op が使えるか (generic mk_call 層 + agent() opt-in)。
add_test(NAME srava_module_demo_ops
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> module_demo_ops)
set_tests_properties(srava_module_demo_ops PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-demo-ops"
  PASS_REGULAR_EXPRESSION "DEMO_OK"
  FAIL_REGULAR_EXPRESSION "DEMO_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly")


# ★★ #3554 段1: **`op#変種` の行**が routing に効いていることの回帰 (demo.so の検定用の行を走らせる)。
#   ⚠ 製品の op はまだ変種行を持たないので、これが無いと段1 は「機能不変」しか確かめられない。
add_test(NAME srava_op_variant
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_op_variant.sh $<TARGET_FILE:srava>)
set_tests_properties(srava_op_variant PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-opvariant;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR}"
  PASS_REGULAR_EXPRESSION "OP-VARIANT-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|unknown op|undefined variable")

# ------------------------------------------------------------------
# d3.so — 第3(mesh 出力)カーネル実証 (rev4 Phase D-3・エージェント非依存の最終形)。
#   demo.so (value-only) に続き **mesh (cacheable typed body) を出力する**第3カーネル。CGAL も
#   Manifold も FetchContent も使わない (依存は host 供給の pig/pts/tinyState のみ)。d3.so を探索路に
#   置くだけで d3_cube/d3_merge/d3_nfaces/d3_nverts が使え、mesh の codec/wire-stream/cache 往復が成立。
#   exec_default=PROCESS (in-proc だと mesh が in-memory 共有で codec 非経由になるため・⑤ 回避)。
# ------------------------------------------------------------------
tinystate_codegen(d3_ts_codegen
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d3/ptsd3WireCacheStreamWriterMesh.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d3/ptsd3WireCacheStreamReaderMesh.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d3/d3aCube.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d3/d3aMerge.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d3/d3aNfaces.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d3/d3aNverts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d3/d3atsAgent.cpp)
# ★ INSTALL を付けない = テスト専用モジュール (配布物には入れない)。
srava_add_module(d3_module
  OUTPUT   d3
  SOURCES
    modules/d3/d3Mesh.cpp
    modules/d3/d3CacheCodec.cpp
    modules/d3/ptsd3WireCacheStreamWriterMesh.cpp
    modules/d3/ptsd3WireCacheStreamReaderMesh.cpp
    modules/d3/d3aCube.cpp
    modules/d3/d3aMerge.cpp
    modules/d3/d3aNfaces.cpp
    modules/d3/d3aNverts.cpp
    modules/d3/d3atsAgent.cpp
    modules/d3/manifest.cpp
  DEPENDS  pigts_codegen d3_ts_codegen
  INCLUDES modules/d3)

# d3.so が dlopen でき記述子 (make_agent 付き) が読めること (RTLD_NOW 全解決)。
add_test(NAME srava_module_d3_so
  COMMAND srava_module_probe $<TARGET_FILE:d3_module>)
set_tests_properties(srava_module_d3_so PROPERTIES
  PASS_REGULAR_EXPRESSION "MODULE_OK name=d3 .*make_agent=yes")

# d3.so の manifest 静的初期化が型軸レジストリへ実装型を登録できること (d3-mesh3d=D3M3)。
add_test(NAME srava_type_d3
  COMMAND srava_module_probe $<TARGET_FILE:d3_module>)
set_tests_properties(srava_type_d3 PROPERTIES
  PASS_REGULAR_EXPRESSION "TYPES d3-mesh3d")

# ★ mesh 出力の第3カーネルが host 無改修で走り、mesh の codec/cache 往復が成立すること:
#   d3_nfaces(d3_merge(d3_cube(1), d3_cube(2))) == 24 かつ d3_nverts == 16 (立方体 2 個=16v/24f)。
add_test(NAME srava_module_d3_mesh
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> module_d3_mesh)
set_tests_properties(srava_module_d3_mesh PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-d3-mesh"
  PASS_REGULAR_EXPRESSION "D3_OK"
  FAIL_REGULAR_EXPRESSION "D3_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly")

# ★ #3440: 型で解決できない呼び出しが **明示エラー**になること (op 名で振る経路の撤去の回帰)。
#   d3_nfaces(cgal の箱) は受ける sig が無い → "no module can execute op" + op 名 + 入力型。
add_test(NAME srava_route_no_sig
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> route_no_sig)
set_tests_properties(srava_route_no_sig PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-route-nosig"
  PASS_REGULAR_EXPRESSION "ROUTE_OK"
  FAIL_REGULAR_EXPRESSION "ROUTE_FAIL|Segmentation|Aborted|core dumped|parse error|closed unexpectedly")

# ★ rev4 最終形の実証: planner (+ generic srava_agent) と d3.so **だけ**を隔離 dir にコピーして走らせ、
#   CGAL/Manifold なしで mesh カーネルが完走すること = 「planner + 第3カーネル 1 個だけで走る
#   (エージェント非依存)」。ホストは全てカーネル中立で、幾何は d3.so のみが供給する。
add_test(NAME srava_module_d3_standalone
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_d3_standalone.sh
          $<TARGET_FILE:srava> $<TARGET_FILE:srava_agent> $<TARGET_FILE:d3_module>)
set_tests_properties(srava_module_d3_standalone PROPERTIES
  PASS_REGULAR_EXPRESSION "D3_STANDALONE_OK"
  FAIL_REGULAR_EXPRESSION "D3_STANDALONE_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly")

# ------------------------------------------------------------------
# d4.so — 第4(in-proc mesh 消費)モジュール (⑤ cross-module 型変換・P4)。
#   d3 のクローンだが exec_default=EXEC_THREAD (in-proc)。**2 個目の in-proc mesh モジュール**として、
#   別モジュール (manifold=in-proc) が作った mf-mesh3d を **自型 d4-mesh3d として in-proc 消費**し、
#   converted get_body(type) 経路 (MFM3→d4-mesh3d の d4-mf-upgrade reader) を初めて exercise する。
#   d4.so 自体は Manifold/CGAL/FetchContent 非依存 — MFM3 の生バイトを読むだけ (framing は d4Mesh と同一)。
# ------------------------------------------------------------------
tinystate_codegen(d4_ts_codegen
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d4/ptsd4WireCacheStreamWriterMesh.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d4/ptsd4WireCacheStreamReaderMesh.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d4/d4aCube.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d4/d4aMerge.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d4/d4aNfaces.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d4/d4aNverts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d4/d4aWedge.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d4/d4atsAgent.cpp)
# ★ INSTALL を付けない = テスト専用モジュール (配布物には入れない)。
srava_add_module(d4_module
  OUTPUT   d4
  SOURCES
    modules/d4/d4Mesh.cpp
    modules/d4/d4CacheCodec.cpp
    modules/d4/ptsd4WireCacheStreamWriterMesh.cpp
    modules/d4/ptsd4WireCacheStreamReaderMesh.cpp
    modules/d4/d4aCube.cpp
    modules/d4/d4aMerge.cpp
    modules/d4/d4aNfaces.cpp
    modules/d4/d4aNverts.cpp
    modules/d4/d4aWedge.cpp
    modules/d4/d4atsAgent.cpp
    modules/d4/manifest.cpp
  DEPENDS  pigts_codegen d4_ts_codegen
  INCLUDES modules/d4)

# d4.so が dlopen でき記述子が読めること + 型軸に d4-mesh3d が登録されること。
add_test(NAME srava_module_d4_so
  COMMAND srava_module_probe $<TARGET_FILE:d4_module>)
set_tests_properties(srava_module_d4_so PROPERTIES
  PASS_REGULAR_EXPRESSION "MODULE_OK name=d4 .*make_agent=yes")
add_test(NAME srava_type_d4
  COMMAND srava_module_probe $<TARGET_FILE:d4_module>)
set_tests_properties(srava_type_d4 PROPERTIES
  PASS_REGULAR_EXPRESSION "TYPES d4-mesh3d")

# d4 ネイティブ mesh 往復 (d3 と同じ・自型消費の健全性確認・in-proc fast path)。
add_test(NAME srava_module_d4_mesh
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> module_d4_mesh)
set_tests_properties(srava_module_d4_mesh PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-d4-mesh"
  PASS_REGULAR_EXPRESSION "D4_OK"
  FAIL_REGULAR_EXPRESSION "D4_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly")

# ------------------------------------------------------------------
# ★ ⑤ P4 の本丸: **in-proc cross-module 型変換** の実証 (manifold 必須)。
#   manifold(in-proc) が作った mf mesh を d4(in-proc) が d4_nfaces/d4_nverts で消費する。routing は
#   d4 op の foreign sig (mf-mesh3d) で d4 へ振られ、d4 の get_body(d4-mesh3d) が MFM3 file を
#   d4-mf-upgrade reader で変換読みする (converted 経路)。dedup (同一 mf cache を 2 消費で変換 1 回) も
#   SRAVA_DBG_CONV の計装行数で検証する。
# ------------------------------------------------------------------
if(SRAVA_MODULE_MANIFOLD)
  add_test(NAME srava_module_d4_xconv
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_d4_xconv.sh $<TARGET_FILE:srava>)
  set_tests_properties(srava_module_d4_xconv PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>"
    PASS_REGULAR_EXPRESSION "D4X_OK"
    FAIL_REGULAR_EXPRESSION "D4X_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly")
endif()

# ------------------------------------------------------------------
# d5.so — 2 個目の in-proc mesh 消費モジュール (⑤ conv **多型共存**テスト用・d4 のクローン・別自型 d5-mesh3d)。
#   d4 と d5 が同一の mf mesh を各々の自型 (d4-mesh3d / d5-mesh3d) として同時 in-proc 消費 → pigDataCache の
#   conv body-list に型別エントリが 2 つ共存することを実証する (旧・単一 conv スロットは互いに上書き/競合した)。
# ------------------------------------------------------------------
tinystate_codegen(d5_ts_codegen
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d5/ptsd5WireCacheStreamWriterMesh.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d5/ptsd5WireCacheStreamReaderMesh.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d5/d5aCube.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d5/d5aMerge.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d5/d5aNfaces.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d5/d5aNverts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d5/d5atsAgent.cpp)
# ★ INSTALL を付けない = テスト専用モジュール (配布物には入れない)。
srava_add_module(d5_module
  OUTPUT   d5
  SOURCES
    modules/d5/d5Mesh.cpp
    modules/d5/d5CacheCodec.cpp
    modules/d5/ptsd5WireCacheStreamWriterMesh.cpp
    modules/d5/ptsd5WireCacheStreamReaderMesh.cpp
    modules/d5/d5aCube.cpp
    modules/d5/d5aMerge.cpp
    modules/d5/d5aNfaces.cpp
    modules/d5/d5aNverts.cpp
    modules/d5/d5atsAgent.cpp
    modules/d5/manifest.cpp
  DEPENDS  pigts_codegen d5_ts_codegen
  INCLUDES modules/d5)

add_test(NAME srava_module_d5_so
  COMMAND srava_module_probe $<TARGET_FILE:d5_module>)
set_tests_properties(srava_module_d5_so PROPERTIES
  PASS_REGULAR_EXPRESSION "MODULE_OK name=d5 .*make_agent=yes")

# ★ conv 多型共存: 同一 mf cache を d4(→d4-mesh3d) と d5(→d5-mesh3d) が同時 in-proc 消費し、両変換が
#   成立 (両者 12) かつ SRAVA_DBG_CONV に **2 本の異なる [CONV] 行** が出る (= body-list に 2 エントリ共存)。
if(SRAVA_MODULE_MANIFOLD)
  add_test(NAME srava_module_d45_multitype
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_d45_multitype.sh $<TARGET_FILE:srava>)
  set_tests_properties(srava_module_d45_multitype PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>"
    PASS_REGULAR_EXPRESSION "D45_OK"
    FAIL_REGULAR_EXPRESSION "D45_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly")
endif()

# ------------------------------------------------------------------
# d2.so — 第2(2D)カーネル (rev4「同一形式を 2 カーネルが次元分担で I/O」実演・§9.4/§9.7 Q-E)。
#   d3 (3D 専用) の対になる 2D 専用カーネル。両者は同じ「生配列 mesh 形式」を共有しつつ **異なる次元型**
#   (d3-mesh3d / d2-shape2d) を持ち、**同じ op 名 `dcount`** をそれぞれ自分の次元型で申告する。
#   dcount は 2 owner なので op-owner routing は発火せず、decide_executor が入力型 (次元) で正しいカーネルへ
#   振る = (kernel×次元) 2 軸問題の型ディスパッチによる解決。d3 と同じく CGAL/Manifold/FetchContent 非依存。
# ------------------------------------------------------------------
tinystate_codegen(d2_ts_codegen
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d2/ptsd2WireCacheStreamWriterShape.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d2/ptsd2WireCacheStreamReaderShape.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d2/d2aSquare.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d2/d2aCount.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d2/d2atsAgent.cpp)
# ★ INSTALL を付けない = テスト専用モジュール (配布物には入れない)。
srava_add_module(d2_module
  OUTPUT   d2
  SOURCES
    modules/d2/d2Shape.cpp
    modules/d2/d2CacheCodec.cpp
    modules/d2/ptsd2WireCacheStreamWriterShape.cpp
    modules/d2/ptsd2WireCacheStreamReaderShape.cpp
    modules/d2/d2aSquare.cpp
    modules/d2/d2aCount.cpp
    modules/d2/d2atsAgent.cpp
    modules/d2/manifest.cpp
  DEPENDS  pigts_codegen d2_ts_codegen
  INCLUDES modules/d2)

# d2.so が dlopen でき記述子が読めること + 型軸に d2-shape2d が登録されること。
add_test(NAME srava_module_d2_so
  COMMAND srava_module_probe $<TARGET_FILE:d2_module>)
set_tests_properties(srava_module_d2_so PROPERTIES
  PASS_REGULAR_EXPRESSION "MODULE_OK name=d2 .*make_agent=yes")
add_test(NAME srava_type_d2
  COMMAND srava_module_probe $<TARGET_FILE:d2_module>)
set_tests_properties(srava_type_d2 PROPERTIES
  PASS_REGULAR_EXPRESSION "TYPES d2-shape2d")

# ★ 次元分担ルーティングの実演: d2.so + d3.so **だけ**を隔離 dir に置き、**同一 op `dcount`** が
#   入力型の次元で正しいカーネルへ振られること: dcount(d3_cube(1))==8 (→d3・頂点数)・
#   dcount(d2_square(1))==4 (→d2・点数)。CGAL/Manifold なしで (kernel×次元) 2 軸問題の解決を実証。
add_test(NAME srava_shared_dim
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_shared_dim.sh
          $<TARGET_FILE:srava> $<TARGET_FILE:srava_agent> $<TARGET_FILE:d2_module> $<TARGET_FILE:d3_module>)
set_tests_properties(srava_shared_dim PROPERTIES
  PASS_REGULAR_EXPRESSION "SHARED_DIM_OK"
  FAIL_REGULAR_EXPRESSION "SHARED_DIM_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly")

# ------------------------------------------------------------------
# 起動方式 registry の remote 枠 確認 (docs §7 Phase 6・実装は別)。ダミー "remote" backend を
#   register_backend → make("remote") が dispatch すること・未登録名は dispatch しないことを確認。
#   pigExecBackend の静的初期化が thread/process = ptsMediatorInternal/External を thNEW するので
#   それらと host インフラ (AGENT_HOST_SRC) をリンクする。
# ------------------------------------------------------------------
add_executable(exec_backend_remote_probe
  src/main/exec_backend_remote_probe_main.cpp
  # ${AGENT_HOST_SRC} は libpig(共有) に移動 (2026-08-12)
)
add_dependencies(exec_backend_remote_probe pigts_codegen)
target_include_directories(exec_backend_remote_probe PRIVATE ${SRAVA_GEN})
target_compile_definitions(exec_backend_remote_probe PRIVATE SRAVA_VALUE_ONLY)
target_link_libraries(exec_backend_remote_probe PRIVATE pig Threads::Threads m ${CMAKE_DL_LIBS})
add_test(NAME exec_backend_remote COMMAND exec_backend_remote_probe)
set_tests_properties(exec_backend_remote PROPERTIES
  PASS_REGULAR_EXPRESSION "REMOTE_SLOT_OK"
  FAIL_REGULAR_EXPRESSION "REMOTE_SLOT_FAIL|Segmentation|Aborted|core dumped")

# ------------------------------------------------------------------
# インストール: srava(プランナー)と srava_agent(CGAL ワーカ)を bin へ。
#   既定の CMAKE_INSTALL_PREFIX=/usr/local なので /usr/local/bin に入る。
#   → SRAVA_AGENT 未定義時の既定パス /usr/local/bin/srava_agent と一致する。
#   `cmake --install build`(or `sudo cmake --install build`)で配置。
# ------------------------------------------------------------------
install(TARGETS srava srava_agent RUNTIME DESTINATION bin)
# include ライブラリ(.sra の stdlib)を $PREFIX/share/srava/lib へ。`include "std/layout.sra"` で引ける。
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib/ DESTINATION share/srava/lib)
# k-Wave 連携サンプル(Python)を $PREFIX/share/srava/srava2kwave へ。docs/srava_kwave.md 参照。
#   ビルド対象ではなく素のコピー。手元の k-Wave で動くかは保証しない位置づけ(README.md 参照)。
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/srava2kwave/ DESTINATION share/srava/srava2kwave
        PATTERN "__pycache__" EXCLUDE)

# 単体実行テスト: srava が union(box,box) を end-to-end 評価(cache 生成 + sweep/cleanup + exit 0)。
# 結果メッシュの頂点/面数(25v 46f)を stdout に出すので正規表現で検証する。
# 毎回 dir を消して MISS(agent 起動→ストリーミング→countAgent 待ち→cleanup)の全経路を通す。
# 結果メッシュの頂点/面数(25v 46f)を stdout に出すので正規表現で検証する。
add_test(NAME srava_planner
  COMMAND sh -c "rm -rf /tmp/srava-planner-test-cache && exec '$<TARGET_FILE:srava>'")
set_tests_properties(srava_planner PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-planner-test-cache;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped")

# 解決後 agent エラーの集約検証: 根(union)が A_SAVE_BEGIN(継続解決)後にエラーを出す。
# プランナーは ptsApp->set_agentError 経由でこれを拾い、countAgent==0 後にエラーを表示し異常終了する。
# 毎回 MISS にするため dir を消してから実行。集約エラーメッセージが出力に出れば経路 OK。
add_test(NAME srava_planner_agenterr
  COMMAND sh -c "rm -rf /tmp/srava-agenterr-test && exec '$<TARGET_FILE:srava>'")
set_tests_properties(srava_planner_agenterr PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-agenterr-test;PIG_TEST_ERR_AFTER_SAVE=1;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib"
  PASS_REGULAR_EXPRESSION "injected error after save"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped")

# SIGINT 配線検証: 評価中(slow agent)に SIGINT → tsSignal→filter→sig_int_flag→
# INTERRUPT→set_agentError(全 agent 撤収)→ countAgent==0 待ち→cleanup→exit。
# デッドロック退行を時間切れ失敗で検出するため TIMEOUT を付ける。
if(WIN32)
  # Windows は POSIX signal が無く、tinyState は SetConsoleCtrlHandler でコンソール制御イベントを
  # 捕まえる(CTRL_BREAK_EVENT → SIGINT にマップ)。テストの raise() 自己発火は console handler に
  # 載らないため、srava を **新プロセスグループ** で起動し CTRL_BREAK を **group 限定** で送る launcher
  # (win_sigbreak)経由でテストする。slow な SRAVA_SOURCE(30 連 union)で in-flight 中に届かせる。
  # NB: SRAVA_SOURCE 内の ';' は CMake の ENVIRONMENT リスト区切りと衝突するので '\;' で退避。
  add_executable(win_sigbreak test/win_sigbreak.c)
  add_test(NAME srava_planner_sigint COMMAND win_sigbreak $<TARGET_FILE:srava>)
  set_tests_properties(srava_planner_sigint PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sigint-test;PIG_TEST_SLOW=1;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib;SRAVA_SOURCE=include \"module/all.sra\"\; var acc = box(1,1,1)\; for (var i = 1\; i < 30\; i = i + 1) { acc = acc ||| box(i,i,1)\; } export(acc)\;"
    PASS_REGULAR_EXPRESSION "interrupted by SIGINT"
    FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|HUNG"
    TIMEOUT 40)
  # SIGTERM: Windows は CTRL_CLOSE_EVENT(→SIGTERM)をプログラム生成できず、graceful SIGTERM-kill
  # 相当も無いため自己テスト不可 → 除外(グレースフル終了経路は上の SIGINT で検証済み)。
else()
add_test(NAME srava_planner_sigint
  COMMAND sh -c "rm -rf /tmp/srava-sigint-test && exec '$<TARGET_FILE:srava>'")
set_tests_properties(srava_planner_sigint PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sigint-test;PIG_TEST_RAISE_SIGINT=1;PIG_TEST_SLOW=1;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib"
  PASS_REGULAR_EXPRESSION "interrupted by SIGINT"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# abort 精査: 素の kill(SIGTERM=15)も SIGINT と同じ撤収経路に合流(in-flight agent kill + クリーン
# cleanup、ハングなし)。exit 143=128+15。SIGINT 専用でなく終了系シグナル全般を捌けること。
add_test(NAME srava_planner_sigterm
  COMMAND sh -c "rm -rf /tmp/srava-sigterm-test && exec '$<TARGET_FILE:srava>'")
set_tests_properties(srava_planner_sigterm PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sigterm-test;PIG_TEST_RAISE_SIGNAL=15;PIG_TEST_SLOW=1;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib"
  PASS_REGULAR_EXPRESSION "interrupted by SIGTERM"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)
endif()

# 終了時クリーンアップ(1.2.5): この run で使われなかった完了キャッシュを usedCaches 登録簿で判定し削除。
add_test(NAME srava_planner_cleanup_unused
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_cleanup_unused.sh $<TARGET_FILE:srava>)
set_tests_properties(srava_planner_cleanup_unused PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-cleanup-test;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib"
  PASS_REGULAR_EXPRESSION "CLEANUP_OK"
  TIMEOUT 20)

# キャッシュ保持方針(SRAVA_CACHE_RETAIN): 即削除(既定)/期日保持/全保持 の切替が効くこと。
add_test(NAME srava_cache_retain
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_cache_retain.sh $<TARGET_FILE:srava>)
set_tests_properties(srava_cache_retain PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-retain-test"
  PASS_REGULAR_EXPRESSION "RETAIN_OK"
  TIMEOUT 30)

# lemonc++ パーサ(2.3): ソース文字列(SRAVA_SOURCE)を parse → tree → 評価。
#   呼び出し形 union(box,box) を別ソースで与え 25v46f を検証(default test は var/代入/||| 形)。
add_test(NAME srava_parser_callform
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> callform)
set_tests_properties(srava_parser_callform PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-parser-cf"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# length(array/hash): 要素数を返す planner 側 op(agent 不要)。print で観測。
add_test(NAME srava_length
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> length)
set_tests_properties(srava_length PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-length"
  PASS_REGULAR_EXPRESSION "4"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 20)

# 2D ガイド line: 部品に寸法線を +++ で重ね SVG に <path>(塗り)+ <polyline>(ストローク)が出る。
add_test(NAME srava_line_guide
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> line_guide)
set_tests_properties(srava_line_guide PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-lineguide"
  PASS_REGULAR_EXPRESSION "LINE_GUIDE_OK"
  FAIL_REGULAR_EXPRESSION "LINE_GUIDE_FAIL|Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# prism/pyramid を Z 軸統一 → prism(n,h,r) ≡ extrude(ngon(n,r),h)(体積一致=1)。
add_test(NAME srava_prism_axis
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> prism_axis)
set_tests_properties(srava_prism_axis PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-prismaxis"
  PASS_REGULAR_EXPRESSION "PEQ= 1"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 20)

# 3D→2D 断面 section(mesh,P,N): 中空箱を平面で切ると穴つき断面(area=64)。
add_test(NAME srava_section
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> section)
set_tests_properties(srava_section PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-section"
  PASS_REGULAR_EXPRESSION "SECAREA= 64"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 30)

# section: 平面が面と共面のとき [0]=空 / [1]=直下 / [2]=直上 (旧実装はここでキメラ断面を返していた)
add_test(NAME srava_section_coplanar
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> section_coplanar)
set_tests_properties(srava_section_coplanar PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-section-copl"
  PASS_REGULAR_EXPRESSION "COPL= 40"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 30)

# empty2d()/empty3d() = 値としての空集合。{} (fold の中立元) と区別されることを見る
add_test(NAME srava_empty_set
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> empty_set)
set_tests_properties(srava_empty_set PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-emptyset"
  PASS_REGULAR_EXPRESSION "EMPTY= 88"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 30)

# 制御文 return/break/continue。for の continue で step が走る(無限ループしない)ことを TIMEOUT で担保。
add_test(NAME srava_control_flow
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> control_flow)
set_tests_properties(srava_control_flow PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-control"
  PASS_REGULAR_EXPRESSION "CF= 122"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 15)

# ★★ #3482 段 1: try/catch 文。⚠ FAIL 正規表現に "ERROR" を入れない —
#   これらのモードは **エラー文言そのもの** を検査対象にするので、入れると必ず赤になる。
add_test(NAME srava_trycatch
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch)
set_tests_properties(srava_trycatch PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch"
  PASS_REGULAR_EXPRESSION "TRYCATCH_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 15)

# コントロール系 (break/continue/return) は try で捕まえずそのまま抜ける。
add_test(NAME srava_trycatch_control
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_control)
set_tests_properties(srava_trycatch_control PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-ctrl"
  PASS_REGULAR_EXPRESSION "TRYCTRL_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 15)

# catch 無し = エラーを握り潰さずそのまま戻り値にする (後続の文は走らない)。
add_test(NAME srava_trycatch_nocatch
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_nocatch)
set_tests_properties(srava_trycatch_nocatch PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-nc"
  PASS_REGULAR_EXPRESSION "TRYNOCATCH_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|TRYNOCATCH_FAIL"
  TIMEOUT 15)

# ★ error() は catch 本体の中だけ — 外は **パース時** に弾く (実行前に分かる)。
add_test(NAME srava_trycatch_static
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_static)
set_tests_properties(srava_trycatch_static PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-st"
  PASS_REGULAR_EXPRESSION "TRYSTATIC_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|TRYSTATIC_FAIL"
  TIMEOUT 15)

# ★★ try の帰属は動的 (定義地点ではなく呼び出し元)。#3482 §4.6。
add_test(NAME srava_trycatch_dynamic
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_dynamic)
set_tests_properties(srava_trycatch_dynamic PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-dyn"
  PASS_REGULAR_EXPRESSION "TRYDYN_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 15)

# catch の外へ持ち出した lambda の error() は実行時に弾く (構文では見切れない経路)。
add_test(NAME srava_trycatch_escape
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_escape)
set_tests_properties(srava_trycatch_escape PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-esc"
  PASS_REGULAR_EXPRESSION "TRYESCAPE_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|TRYESCAPE_FAIL"
  TIMEOUT 15)

# ★ error() が返す **中身のハッシュ** (message / class / file / line) の値を 1 つずつ見る。
add_test(NAME srava_trycatch_hash
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_hash)
set_tests_properties(srava_trycatch_hash PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-hash"
  PASS_REGULAR_EXPRESSION "TRYHASH_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 15)

# ★★ `try { s }` ≡ `try { s } catch { throw error(); }` を **出力の一致**で見る。
add_test(NAME srava_trycatch_throw
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_throw)
set_tests_properties(srava_trycatch_throw PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-throw"
  PASS_REGULAR_EXPRESSION "TRYTHROW_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|TRYTHROW_FAIL"
  TIMEOUT 20)

# ★★ #3482 段 2: try は自分のスコープで起動した agent を **見送る** (待ちリスト)。
#   同じ検査を try の中と後で行い、差が出ることを見る。
add_test(NAME srava_trycatch_barrier
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_barrier)
set_tests_properties(srava_trycatch_barrier PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-barrier"
  PASS_REGULAR_EXPRESSION "TRYBARRIER_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|TRYBARRIER_SKIPPED|parse error"
  TIMEOUT 60)

# destroy(): try の外では実行時エラー (送る先が無い)。
add_test(NAME srava_trycatch_destroyop
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_destroyop)
set_tests_properties(srava_trycatch_destroyop PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-dop"
  PASS_REGULAR_EXPRESSION "TRYDESTROYOP_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|TRYDESTROYOP_FAIL"
  TIMEOUT 15)

# ★★ #3482: try が畳んだ agent の失敗は「畳まれた跡」(PE_DERIVED) で報告・終了コードから外れる。
#   ⚠ 本物の失敗が今も rc!=0 で報告されることを同じテストで併せて見る (握り潰しとの区別)。
add_test(NAME srava_trycatch_derived
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_derived)
set_tests_properties(srava_trycatch_derived PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-der"
  PASS_REGULAR_EXPRESSION "TRYDERIVED_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|TRYDERIVED_FAIL"
  TIMEOUT 60)

# ★★ #3482 段 4: async 本体の失敗が try/catch で捕まる。
#   ⚠ catch 無し / トップレベルが **今も報告される**ことを同じテストで併せて見る (握り潰しとの区別)。
add_test(NAME srava_trycatch_async
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_async)
set_tests_properties(srava_trycatch_async PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-async"
  PASS_REGULAR_EXPRESSION "TRYASYNC_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|TRYASYNC_FAIL"
  TIMEOUT 60)

# ★★ #3482: flush() は「その try の待ちが空になるまで」のバリア (本体 / catch / 根 の 3 か所)。
add_test(NAME srava_trycatch_flush
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_flush)
#   ⚠ SRAVA_HANG_SECS も要る — flush() は「待ちが空になるまで」を 3 か所で見る本なので
#     素の所要が **10 秒を越える**。既定 10 秒のままだと番犬が毎回撮るが *テストは緑* なので
#     気づけず、捕獲だけが溜まる (09-19〜09-22 で 37 件)。
#   ★ 予算は **両機の実測を並べて遅い方の x3** (2026-09-22 に 2 機で測り直した):
#       mac (arm64/OCCT 7.9.3)  **21.26s**  ← 遅い方。⚠ 負荷で 16〜21s と揺れるので最大を採る
#       Linux (x86_64/7.8.1)     15.80s     (3 走で 15.79 / 15.80 / 15.80)
#     ⇒ 21.26 x 3 = 63.8 ⇒ **75s** (⚠ TIMEOUT 90 より小さく。60 では mac で x2.8 しか無かった)
set_tests_properties(srava_trycatch_flush PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-flush;SRAVA_HANG_SECS=75"
  PASS_REGULAR_EXPRESSION "TRYFLUSH_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|TRYFLUSH_FAIL"
  TIMEOUT 90)

# ★★ #3482: agent が途中で死んだときの文言にも module/op が前置きされる (#3475 の規約)。
add_test(NAME srava_agent_fail_name
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> agent_fail_name)
set_tests_properties(srava_agent_fail_name PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-agentfailname"
  PASS_REGULAR_EXPRESSION "AGENTFAILNAME_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|AGENTFAILNAME_FAIL"
  TIMEOUT 30)

# ★★ #3482: 撤収は pigData の木を通る (system() は待ちリストに載らないので木でしか届かない)。
#   陽性対照つき: destroy しない側では マーカーが出来ることも見る。
add_test(NAME srava_trycatch_tree_destroy
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_tree_destroy)
# ⚠⚠ 番犬の予算 — **この本には「実測 x3」を当ててはいけない** (2026-09-23 に誤りと判明)。
#   旧: 「最も遅い機の実測 x3」で 15s (mac 3.80s / Linux 3.37s)。
#   ★ 誤りの中身: 測っていたのは **速い経路の所要**だけ。この本は下の待ちループで
#     **設計上 20 秒まで待つ** (b の done を取りこぼさないための上限つきループ)。
#     ⇒ 予算 15 < 内部の上限 20 という逆転になっていて、**b が遅い回 = まさにループを
#       足した理由の回**に、緑のまま番犬の捕獲だけが溜まる。
#   ⇒ **内部に明示の上限を持つ本は「上限 + 前段の所要」から決める**:
#       20 (ループ上限) + 約 5 (srava 2 回) = 正当な最悪 ~25s ⇒ 余裕をみて **45s**。
#     TIMEOUT 60 は据え置き (45 + 捕獲の採取 ~4s = ~49s で、60 に収まる)。
#   実測 (2026-09-23 simu01 ・ ctest -j8 の負荷下 ・ 2 回): **4.40s / 4.40s** = 速い経路は健全。
set_tests_properties(srava_trycatch_tree_destroy PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-tree;SRAVA_HANG_SECS=45"
  PASS_REGULAR_EXPRESSION "TRYTREEDESTROY_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|TRYTREEDESTROY_FAIL"
  TIMEOUT 60)

# ★★ #3482: 「env を作る場所は全部 try をリレーする」の不変条件。制御 op を一通り回して
#   どこでも agent が登録できる (= no enclosing try が出ない) ことを見る。
add_test(NAME srava_try_relay
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> try_relay)
set_tests_properties(srava_try_relay PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-try-relay"
  PASS_REGULAR_EXPRESSION "TRYRELAY_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|TRYRELAY_FAIL"
  TIMEOUT 90)

# ★★ #3482 の上位互換: try を書かないプログラムでの「主エラー + other agents reported: の列挙」。
add_test(NAME srava_agent_error_list
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> agent_error_list)
set_tests_properties(srava_agent_error_list PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-agent-errlist"
  PASS_REGULAR_EXPRESSION "AGENTERRLIST_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|AGENTERRLIST_FAIL"
  TIMEOUT 30)

# ★★ #3482 §2-A: catch の中で error() を何度でも呼べる (1 件ずつ・尽きたら 0)。
#   2 件目は error() が待って取れる = 「待ちを担うのは error()」も同時に見る。
add_test(NAME srava_trycatch_multi
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_multi)
set_tests_properties(srava_trycatch_multi PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-multi"
  PASS_REGULAR_EXPRESSION "TRYMULTI_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|TRYMULTI_FAIL"
  TIMEOUT 30)

# ★★ #3482: catch の destroy() は **その try で生成された分だけ**を壊す (外の async は完走する)。
add_test(NAME srava_trycatch_scope
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_scope)
set_tests_properties(srava_trycatch_scope PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-scope"
  PASS_REGULAR_EXPRESSION "TRYSCOPE_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|TRYSCOPE_FAIL"
  TIMEOUT 60)

# 幾何エラー (モジュール名前置きつき) も直列に伝播するものは捕まる。
add_test(NAME srava_trycatch_geom
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> trycatch_geom)
set_tests_properties(srava_trycatch_geom PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-trycatch-geom"
  PASS_REGULAR_EXPRESSION "TRYGEOM_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 30)

# concat: 配列連結(planner 側 op)。concat([1,2,3],[4,5],6) の長さ = 6。
add_test(NAME srava_concat
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> concat)
set_tests_properties(srava_concat PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-concat"
  PASS_REGULAR_EXPRESSION "6"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 20)

# 添字/メンバ代入: a[i]=v / a.key=v(ループでの空配列成長・ネスト・ハッシュ)。
add_test(NAME srava_idx_assign
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> idx_assign)
set_tests_properties(srava_idx_assign PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-idxassign"
  PASS_REGULAR_EXPRESSION "0,1,4,9"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 20)

# 配列の要素ごと算術: ([1,2]+[3,4])*2-[1,1] = [7,11]。
add_test(NAME srava_arr_arith
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> arr_arith)
set_tests_properties(srava_arr_arith PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-arrarith"
  PASS_REGULAR_EXPRESSION ".7,11."
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 20)

# print(値): int/string/float を stdout に表示。
add_test(NAME srava_print_value
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> printval)
set_tests_properties(srava_print_value PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-printval"
  PASS_REGULAR_EXPRESSION "hello"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 20)

# 論理演算子 && || !(真偽=get_bool・結果 0/1)と優先順位(&&>||、比較>&&、!>==)。
# 値返し op(valid/volume)を論理オペランドに使えることも確認。
add_test(NAME srava_logic
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> logic)
set_tests_properties(srava_logic PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-logic"
  PASS_REGULAR_EXPRESSION "L 1 0 1 0 1 0 1 0 1 1"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 20)

# fold 単位元 []: 空配列を mesh ブールの中立元に(|||/&&& を if(i==0) なしで畳める)。valid([])=0。
add_test(NAME srava_identity
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> identity)
set_tests_properties(srava_identity PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-identity"
  PASS_REGULAR_EXPRESSION "I 24 800 8 0"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 30)

# ★★ #3594: stdlib の対 tube_fw / tube_fw_ruled が **別の形**を作ること (背骨が違う)。
#   ⚠ tube_fw は occt の tube を呼ぶので occt が要る。
if(SRAVA_MODULE_OCCT AND SRAVA_MODULE_CGAL)
  add_test(NAME srava_tube_fw
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> tubefw)
  set_tests_properties(srava_tube_fw PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-tube-fw"
    PASS_REGULAR_EXPRESSION "TUBEFW_OK"
    FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
    TIMEOUT 60)
endif()

# std/curve.sra(arc/bezier/spline/clothoid)。指数表記座標の round-trip も検証。
add_test(NAME srava_curvelib
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> curvelib)
set_tests_properties(srava_curvelib PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-curvelib;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib"
  PASS_REGULAR_EXPRESSION "CU 17 11 1"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 30)

# transpose / cumsum / sum(planner 側・curve 生成の土台)。
add_test(NAME srava_arrayops
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> arrayops)
set_tests_properties(srava_arrayops PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-arrayops"
  PASS_REGULAR_EXPRESSION "1,3,6,10. 10"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 20)

# 初等関数(カーネル・ベクトル化・ラジアン)。
add_test(NAME srava_mathfn
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> mathfn)
set_tests_properties(srava_mathfn PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-mathfn"
  PASS_REGULAR_EXPRESSION "MA 4 256 3 7 .2,3."
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 20)
# std/math.sra(PI/range/linspace)を include。
add_test(NAME srava_mathlib
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> mathlib)
set_tests_properties(srava_mathlib PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-mathlib;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib"
  PASS_REGULAR_EXPRESSION "ML .0,1,2,3. .0,2,4,6."
  TIMEOUT 20)

# stdlib(std/layout.sra)を include して row/grid を使う(.sra で書かれたライブラリ + include の総仕上げ)。
add_test(NAME srava_layout
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> layout)
set_tests_properties(srava_layout PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-layout;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib"
  PASS_REGULAR_EXPRESSION "LAY 73 3 13"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 30)

# grid の軸別 gap([gx,gy]=格子ピッチ) と 3D grid3(層は Z 自動)。列1 x=1*4=4 / 層1 z=1*10=10。
add_test(NAME srava_grid_axes
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> grid_axes)
set_tests_properties(srava_grid_axes PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-gridaxes;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib"
  PASS_REGULAR_EXPRESSION "GAXES 4 10"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 30)

# std/guide.sra の ruler(細い tube の ものさし・combine で part に重ねる)。
add_test(NAME srava_guide_ruler
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> guide_ruler)
set_tests_properties(srava_guide_ruler PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-guideruler;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib"
  PASS_REGULAR_EXPRESSION "RULER 50"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 30)

# color(mesh,c): 面色。combine で各成分の色を保持し、色対応 export(COFF/PLY/3MF/AMF)で出る。
add_test(NAME srava_color_export
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> color_export)
set_tests_properties(srava_color_export PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-colorexp"
  PASS_REGULAR_EXPRESSION "COLOR_OK"
  FAIL_REGULAR_EXPRESSION "COLOR_FAIL|Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 30)

# combine(+++)入力へのブール分配則 (a+++b)&&&c / (a+++b)---c。
add_test(NAME srava_dist_bool
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> dist_bool)
set_tests_properties(srava_dist_bool PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-distbool"
  PASS_REGULAR_EXPRESSION "DIST 1000 1000"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 30)

# 連鎖代入 a=b=c=expr(右結合・RHS は 1 度評価)。
add_test(NAME srava_chain_assign
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> chain_assign)
set_tests_properties(srava_chain_assign PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-chainassign"
  PASS_REGULAR_EXPRESSION "CA 6 3 3 15 5"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 30)

# 肉厚 SDF(thin_spots)。薄板=全 12 面が危険点 / 厚いブロック=0。値返し(入れ子配列)。
add_test(NAME srava_thin_spots
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> thin_spots)
set_tests_properties(srava_thin_spots PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-thinspots"
  PASS_REGULAR_EXPRESSION "THIN 12 0 4"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 30)

# transform 演算子(>>> <> *** @)の配列対応: broadcast / instancing / zip / 単一。
add_test(NAME srava_xformbcast
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> xformbcast)
set_tests_properties(srava_xformbcast PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-xformbcast"
  PASS_REGULAR_EXPRESSION "X 3 3 2 8"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 30)

# include "path": 字句インクルード(相対解決 + 多重 include 防止 + 定義の可視化)。
add_test(NAME srava_include
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> includetest)
set_tests_properties(srava_include PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-include"
  PASS_REGULAR_EXPRESSION "INC 42"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error|unknown op"
  TIMEOUT 20)
add_test(NAME srava_include_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> includeerr)
set_tests_properties(srava_include_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-includeerr"
  # 「見つからない」だけでなく **どこを探したか** を言うこと (SRAVA_PATH 未設定の切り分けに要る)。
  PASS_REGULAR_EXPRESSION "include: cannot find.*searched:.*SRAVA_PATH="
  TIMEOUT 20)

# map(array, fn): 各要素に lambda を適用し配列を返す(1引数/2引数(index)・instancing)。
add_test(NAME srava_map
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> maptest)
set_tests_properties(srava_map PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-map"
  PASS_REGULAR_EXPRESSION "M .1,8,27. .10,120,230. 3"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 30)

# union/intersection/combine(配列1引数): eval 時に均衡二分木で並列畳み込み(直列 fold 回避)。
add_test(NAME srava_arrayfold
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> arrayfold)
set_tests_properties(srava_arrayfold PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-arrayfold"
  PASS_REGULAR_EXPRESSION "AF 32 27 0"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 30)

# export_async(非ブロッキング書き出し) + flush(明示バリア)。flush 後に system がファイルを観測できる。
add_test(NAME srava_asyncexport
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> asyncexport)
set_tests_properties(srava_asyncexport PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-asyncexport"
  PASS_REGULAR_EXPRESSION "ASYNC_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 20)

# async 文(統一プリミティブ): 複数 async 並列・sync は出現順整列・body スコープ共有。
add_test(NAME srava_async
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> async)
set_tests_properties(srava_async PROPERTIES
  ENVIRONMENT "SRAVA_CACHE_DIR=/tmp/srava-async"
  PASS_REGULAR_EXPRESSION "ASYNCSYNC_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error|ASYNCSYNC_FAIL"
  TIMEOUT 20)

# 分割代入 `var [a,b,…] = 式;`: 基本・余りは無視・不足はエラー・非配列はエラー・1 文なので並列。
add_test(NAME srava_destructure
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> destructure)
set_tests_properties(srava_destructure PROPERTIES
  ENVIRONMENT "SRAVA_CACHE_DIR=/tmp/srava-destructure"
  PASS_REGULAR_EXPRESSION "DESTRUCT_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error|DESTRUCT_FAIL"
  TIMEOUT 30)

# async のエラーは continue-and-collect(中断せず末尾報告・当該 sync スキップ・他は走る・exit 1)。
add_test(NAME srava_async_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> async_err)
set_tests_properties(srava_async_err PROPERTIES
  ENVIRONMENT "SRAVA_CACHE_DIR=/tmp/srava-async-err"
  PASS_REGULAR_EXPRESSION "ASYNCERR_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error|ASYNCERR_FAIL"
  TIMEOUT 20)

# 構文エラーは該当トークン + 行 + キャレットで表示。
add_test(NAME srava_parseerr
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> parseerr)
set_tests_properties(srava_parseerr PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-parseerr"
  PASS_REGULAR_EXPRESSION "parse error near 'export'"
  TIMEOUT 20)

# bbox(m): 軸平行 AABB を [min隅,max隅] の入れ子配列で返す(2D/3D 多態・添字 b[i][j] 可)。
add_test(NAME srava_bbox
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> bbox)
set_tests_properties(srava_bbox PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-bbox"
  PASS_REGULAR_EXPRESSION "B 1 5 10 22"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 30)

# float(x): 文字列/整数 → 浮動小数への値変換(planner 側 op)。
add_test(NAME srava_tofloat
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> tofloat)
set_tests_properties(srava_tofloat PROPERTIES
  ENVIRONMENT "SRAVA_CACHE_DIR=/tmp/srava-tofloat"
  PASS_REGULAR_EXPRESSION "F 3.5 3.5 42"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 20)

# int(x): 文字列/浮動小数 → 整数への値変換(planner 側 op・切り捨て)。
add_test(NAME srava_toint
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> toint)
set_tests_properties(srava_toint PROPERTIES
  ENVIRONMENT "SRAVA_CACHE_DIR=/tmp/srava-toint"
  PASS_REGULAR_EXPRESSION "I 42 3 8"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|unknown op"
  TIMEOUT 20)

# export([]) は実体化できないので明示エラー(クラッシュ/ハングしない)。
add_test(NAME srava_identity_export_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> identity_export_err)
set_tests_properties(srava_identity_export_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-identerr"
  PASS_REGULAR_EXPRESSION "empty mesh"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# ★ #3479: 復号を拒んだモジュールの **理由** が利用者のエラー文に届くこと。
#   これが無いと「materialize できない」までしか出ず、原因の切り分けが利用者にできない。
add_test(NAME srava_decode_refusal_reason
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> decode_refusal_reason)
set_tests_properties(srava_decode_refusal_reason PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-decrefuse"
  # ⚠ 2026-09-06: 題材を「非 2-多様体」から「非有界」へ替えた (前者は読めるようになったため)。
  PASS_REGULAR_EXPRESSION "it is unbounded"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error|unknown op"
  TIMEOUT 60)

# print(mesh): 継続を辿って agent 完了後に pigDataCache のハッシュファイル名(.cache パス)を表示。
add_test(NAME srava_print_mesh
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> printmesh)
set_tests_properties(srava_print_mesh PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-printmesh"
  PASS_REGULAR_EXPRESSION "[.]cache"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error|delayed"
  TIMEOUT 20)

# +++ combine(交差許容の単純合体・viewer 用): 2 箱 = 16v24f(2 連結成分・ブールなし)。
add_test(NAME srava_combine_op
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> combine_op)
set_tests_properties(srava_combine_op PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-combineop"
  PASS_REGULAR_EXPRESSION "NVF 16 24"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# combine(a,b,c): n-ary も二項分解で合体。3 箱 = 24v36f。
add_test(NAME srava_combine_fn
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> combine_fn)
set_tests_properties(srava_combine_fn PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-combinefn"
  PASS_REGULAR_EXPRESSION "NVF 24 36"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 接触で自己交差した中間結果を次の boolean に渡すと CGAL segfault していた回帰の防止
# (throw_on_self_intersection + is_closed ゲート)。クラッシュ/ハングせず "boolean failed" で弾く。
add_test(NAME srava_selfint_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> selfint_err)
set_tests_properties(srava_selfint_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-selfint"
  PASS_REGULAR_EXPRESSION "boolean failed"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|agent closed"
  TIMEOUT 30)

# 3D boolean が同一平面一致で非多様体 → 黙って空でなく "boolean failed" エラー(coplanar 案内付き)。
add_test(NAME srava_coplanar_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> coplanar_err)
set_tests_properties(srava_coplanar_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-coplanar"
  PASS_REGULAR_EXPRESSION "boolean failed"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 30)

# 配列リテラル要素のエラーが agent の "inline arg parse error" に化けず本当の原因が出る。
add_test(NAME srava_arrerr_prop
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> arrerr_prop)
set_tests_properties(srava_arrerr_prop PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-arrerr"
  PASS_REGULAR_EXPRESSION "hash key not found"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|inline arg parse error"
  TIMEOUT 20)

# 範囲外添字/未定義変数エラーが ERROR[file,line] で位置付きになる(varref/index に位置を刻む)。
add_test(NAME srava_idx_errloc
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> idxerrloc)
set_tests_properties(srava_idx_errloc PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-idxerrloc"
  # ★ #3569: 前置をやめたので **報告される行 = ファイルの行** (4 行目)。
  PASS_REGULAR_EXPRESSION "ERROR\\[.*,4\\] array index out of range"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 負の幅 rect は退化ポリゴン → 2D union でエージェントがクラッシュしていた回帰の防止。
# rect が明示エラー(位置付き)で弾き、agent クラッシュしないことを確認。
add_test(NAME srava_rect_neg
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> rect_neg)
set_tests_properties(srava_rect_neg PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-rectneg"
  PASS_REGULAR_EXPRESSION "rect: width and height must be > 0"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|agent closed"
  TIMEOUT 20)

# polygon が連続重複頂点(曲線 concat の継ぎ目等)を間引き、単純多角形(valid=1)になる回帰。
# 重複が残ると非単純(valid=0)→ offset が空になっていた。
add_test(NAME srava_polygon_dedup
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> polygon_dedup)
set_tests_properties(srava_polygon_dedup PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-polydedup"
  PASS_REGULAR_EXPRESSION "DEDUP 1"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|agent closed"
  TIMEOUT 20)

# 不正(自己交差)ポリゴンの 2D offset は黙って空でなく明示エラー(union 等と一貫)。
add_test(NAME srava_offset_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> offset_err)
set_tests_properties(srava_offset_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-offseterr"
  PASS_REGULAR_EXPRESSION "offset failed"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|agent closed"
  TIMEOUT 30)

# 型エラーの行番号: mesh + mesh は「+ の式の行」(=3)を指す(被演算子値の行に引きずられない)。
add_test(NAME srava_add_lineno
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> add_lineno)
set_tests_properties(srava_add_lineno PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-addlineno"
  # ★ #3569: 前置をやめたので **報告される行 = ファイルの行** (3 行目)。
  PASS_REGULAR_EXPRESSION ",3] unsupported operation"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|agent closed"
  TIMEOUT 30)

# hash リテラル内の兄弟キー参照(逐次スコープ・let*)。{b:1,c:b+2,d:c+b} → c=3,d=4。
add_test(NAME srava_hash_sibling
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> hash_sibling)
set_tests_properties(srava_hash_sibling PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-hashsib"
  PASS_REGULAR_EXPRESSION "SIB 3 4"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|agent closed|ERROR"
  TIMEOUT 20)

# エラーの ERROR[file,line] 表示 + エラー時はキャッシュ掃除を抑止(Feature1/2)。
# ※このテストは意図的にエラーを出すので FAIL 正規表現に ERROR を含めない。
add_test(NAME srava_errloc
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> errloc)
set_tests_properties(srava_errloc PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-errloc"
  # ★ #3569: 前置をやめたので **報告される行 = ファイルの行** (3 行目)。
  PASS_REGULAR_EXPRESSION "ERROR\\[.*,3\\].*volume"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 通常代入の右辺エラーは代入地点で伝播する (#3476)。変数を使わなくても発覚すること。
add_test(NAME srava_assign_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> assignerr)
set_tests_properties(srava_assign_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-assignerr"
  # ★ #3569: 前置をやめたので **報告される行 = ファイルの行** (2 行目)。
  PASS_REGULAR_EXPRESSION "ERROR\\[.*,2\\].*volume"
  # 修正前は右辺エラーが黙って束縛され、未使用のまま最後まで走り抜けた。
  FAIL_REGULAR_EXPRESSION "REACHED_END|Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# ★ #3474: 基本立体を全カーネルへ配ったので、manifold 単独でも 5 立体すべてが引ける。
add_test(NAME srava_solids_one_kernel
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> solids_one_kernel)
set_tests_properties(srava_solids_one_kernel PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-solids1k"
  PASS_REGULAR_EXPRESSION "S 1.333333[0-9]* 6.242890[0-9]* 2.080963[0-9]* 9.743419[0-9]* 0.513200[0-9]*"
  # 修正前は pyramid が cgal 専用だったので manifold 単独では routing できなかった。
  FAIL_REGULAR_EXPRESSION "no module can execute|Segmentation|Aborted|core dumped"
  TIMEOUT 30)

# ★ #3475: 幾何エラーがどのカーネルの物か名乗る + 属性タグは表示に漏れない。
add_test(NAME srava_err_module
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> errmodule)
set_tests_properties(srava_err_module PROPERTIES
  # ★ SRAVA_TEST_MODULES = 実際に建ったモジュール (定義は冒頭の option 集約の直後)。
  #   スクリプト側の kernels() がこれと突き合わせ、建っていないカーネルを検査から外す。
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-errmodule;SRAVA_TEST_MODULES=${SRAVA_TEST_MODULES}"
  PASS_REGULAR_EXPRESSION "ERRMODULE-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
  TIMEOUT 60)

# ★ #3477: 内省 op 3 本 (modules / type_of / which)。カーネルが混ざる状況で正しく答えること。
add_test(NAME srava_introspect
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> introspect)
set_tests_properties(srava_introspect PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-introspect"
  PASS_REGULAR_EXPRESSION "INTROSPECT-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|unknown op"
  TIMEOUT 40)

# ★ #3474: occt の基本立体の置き場所を閉形式で固定 (解析曲面は kernel_agree に入れられない)。
# ⚠ **occt が建つ構成でだけ登録する。** これが if の外にあったため、Cygwin (occt 自動 OFF) で
#   値が返らず 7 項目すべて FAIL していた (2026-09-13)。他の occt テストは
#   if(SRAVA_MODULE_OCCT) の中の foreach(_oc_modes) で登録されており、これだけ漏れていた。
if(SRAVA_MODULE_OCCT)
  add_test(NAME srava_occt_place
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_occt.sh $<TARGET_FILE:srava> place)
  # ⚠ 番犬の予算 — **最も遅い機**の実測 x3 (2026-09-22)。mac(arm64/OCCT 7.9.3) 6.75s ・ Linux(x86_64/7.8.1) 3.45s ⇒ 30s。
  set_tests_properties(srava_occt_place PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/occache-place;SRAVA_HANG_SECS=30"
    PASS_REGULAR_EXPRESSION "OCCT-PLACE-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|ERROR"
    TIMEOUT 60)
endif()

# ★ #3474 続き: 省略可能な引数 (nreq)。省略形は通り、余分は黙って捨てず弾く。
add_test(NAME srava_arg_arity
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> argarity)
set_tests_properties(srava_arg_arity PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-argarity"
  PASS_REGULAR_EXPRESSION "ARGARITY-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
  TIMEOUT 60)

# ★ #3474 続き: empty3d() が全カーネルで **空集合**として振る舞うこと (中立元 {} と別物)。
add_test(NAME srava_empty3d_set
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> empty3dset)
set_tests_properties(srava_empty3d_set PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-empty3d;SRAVA_TEST_MODULES=${SRAVA_TEST_MODULES};SRAVA_HANG_SECS=60"
  PASS_REGULAR_EXPRESSION "EMPTY3D-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|uncaught exception"
  TIMEOUT 90)

# ★★ #3535: **上流の可変大域状態を「設定する」呼び出しが持ち込まれたら赤** (srava_upstream_globals.sh)。
#   ⚠ cgal.so と libsrava_cg は CGAL の可変大域 (default_random / _error_handler /
#     _error_behaviour / IO::Static::get_mode) を **2 コピー持っている**。CGAL 6.x はヘッダ
#     オンリーで上流の実体が無いので、geogram (#3535①) のように借りる形へ寄せられない。
#   ★ いま害が出ていないのは **誰も設定していないから**。⇒ 設定を持ち込んだ瞬間に赤くする。
#   ⚠ とくに CGAL::IO::set_mode は ASCII/binary なので **書き出しの形式が黙って変わる**。
#   ⇒ これは *柵* であって直しではない。直ったら (cgal.so が可変大域を定義しなくなったら) 不要。
add_test(NAME srava_upstream_globals
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_upstream_globals.sh
          ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR})
set_tests_properties(srava_upstream_globals PROPERTIES
  PASS_REGULAR_EXPRESSION "UPSTREAM-GLOBALS-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:"
  TIMEOUT 60)

# ★★ #3535: **上流ライブラリのコピーが 2 つになっていないか数える** (test/srava_upstream_copies.sh)。
#   モジュール .so が上流シンボルを *自分で定義* していたら赤。geogram は #3535① で
#   「借りる」形へ寄せたので 0 が約束・manifold / cherchi は元から理想形なので固定するだけ。
#   ⚠ この形が崩れると、大域状態を持つ上流 (geogram) では初期化したコピーと使うコピーが
#     食い違って SIGSEGV する (2026-09-14・Linux 実測)。⇒ **目視では見えないので数える**。
#   ★ モジュール構成に依らないので if() の外に置く (建っていない .so は skip する)。
#   ⚠ nm / c++filt が無い環境では UPSTREAM-SKIP を出す。⇒ **Skipped と表示させる**
#     (2026-09-16: 緑のまま抜けると「検定した」と見分けが付かない。srava_op_cgal_free と同型)。
add_test(NAME srava_upstream_copies
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_upstream_copies.sh ${CMAKE_BINARY_DIR})
# ⚠ 番犬の予算 — **最も遅い機**の実測 x3 (2026-09-22)。mac(arm64/OCCT 7.9.3) 9.90s ・ Linux(x86_64/7.8.1) 1.04s ⇒ 30s。
set_tests_properties(srava_upstream_copies PROPERTIES
  ENVIRONMENT "SRAVA_HANG_SECS=30"
  PASS_REGULAR_EXPRESSION "UPSTREAM-OK"
  SKIP_REGULAR_EXPRESSION "UPSTREAM-SKIP"
  FAIL_REGULAR_EXPRESSION "FAIL:"
  TIMEOUT 120)

# ★★ #3545 段 3: **op の TU が上流 (CGAL) を引いていないことを数える** (test/srava_op_cgal_free.sh)。
#   srava_upstream_copies.sh が見るのは *.so 1 本ごとの合計* なので、「どの op が引いているか」
#   も「境界が守られているか」も分からない。⇒ **リンクされた .o を 1 本ずつ**数える。
#   ⚠ 対象は @link.txt@ から取る。@CMakeFiles/<t>.dir/**/*.o@ を glob すると **リンクされて
#     いない古い .o** まで数える (2026-09-16 実測: nef_snc は 44 本中 4 本に見えたが、
#     正しくは 40 本中 0 本。cgal も 63/64 に見えたが正しくは 62/63)。
#   ★ 陽性対照つき — 幾何 lib の TU が 0 に見えたら *検出器の方が壊れている* として赤にする。
#   ★ 生成器は **Makefile (link.txt) と Ninja (build.ninja) の両方**を読む (2026-09-16)。
# ⚠⚠ **SKIP を Passed にしない**。2026-09-16 に box (Windows・Ninja 生成器) でこの検査が
#   @OPFREE-SKIP link.txt が無い@ → **Passed** となり、#3545 段 2/4 の成果が Windows で
#   1 度も検定されないまま「緑」に見えていた (simu01 報告)。⇒ ctest に Skipped と言わせる。
#   ★ SKIP_RETURN_CODE / SKIP_REGULAR_EXPRESSION は どちらも PASS_REGULAR_EXPRESSION に
#     **優先する** (実測 2026-09-16)。両方付けてあるのは、スクリプトの終了コードと出力の
#     どちらが先に壊れても Skipped に倒れるようにするため。
# ⚠ **構成によって検べる本数が変わる** (macMINI 指摘 2026-09-16)。既定 (NEF_SNC=OFF) では
#   nef_snc / nef_cg / nef_mf が建たないので 6 本中 3 本が対象外になる。⇒ スクリプトは
#   **検べた本数を必ず出し、0 本なら OPFREE-SKIP** にする (0 本で緑にしない)。
add_test(NAME srava_op_cgal_free
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_op_cgal_free.sh ${CMAKE_BINARY_DIR})
# ⚠ 番犬の予算 — **最も遅い機**の実測 x3 (2026-09-22)。mac(arm64/OCCT 7.9.3) 6.73s ・ Linux(x86_64/7.8.1) 0.78s ⇒ 30s。
set_tests_properties(srava_op_cgal_free PROPERTIES
  ENVIRONMENT "SRAVA_HANG_SECS=30"
  PASS_REGULAR_EXPRESSION "OPFREE-OK"
  SKIP_REGULAR_EXPRESSION "OPFREE-SKIP"
  SKIP_RETURN_CODE 77
  FAIL_REGULAR_EXPRESSION "FAIL:"
  TIMEOUT 300)

# ★★ #3564: **sPtr::__get() を木に置かない** (ひさ 2026-09-20: 利用禁止)。
#   ⚠ 生ポインタを剥がすと参照カウントに載らないので、保持すれば「借りたポインタ越しの操作」
#     (#3560)、null 検査は「入ったが畳まれた」を素通り (#3562 の panic が本物にならない)。
#   ★ ソースだけを見る検査なので構成にもビルドにも依らない ⇒ if() の外。
#   ★ 陽性対照つき (同じ探し方で @sPtr<@ を持つファイルが 0 なら探し方が壊れている)。
add_test(NAME srava_no_sptr_rawget
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_no_sptr_rawget.sh ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(srava_no_sptr_rawget PROPERTIES
  PASS_REGULAR_EXPRESSION "RAWGET-OK"
  SKIP_REGULAR_EXPRESSION "RAWGET-SKIP"
  SKIP_RETURN_CODE 77
  FAIL_REGULAR_EXPRESSION "FAIL:"
  TIMEOUT 60)

# ★★ #3559: **変種で建てる TU が具体クラス名を直に書いていないことを数える**
#   (test/srava_nef_variant_guard.sh)。
#   ⚠⚠ nef の幾何クラスを 2 型 (nfMesh / nfMeshSnc) に割ったので、op の TU が具体クラス名を
#     直に書くと **もう一方の変種の値を作ってもコンパイルが通る**。落ちるのは実行時、それも
#     その op ではなく下流の materialize (2026-09-19 に nfTriSink.h で実際に踏んだ)。
#   ★ ソースだけを見る検査なので、モジュール構成にもビルドにも依らない ⇒ if() の外。
#   ★ 陽性対照つき (nfWire.cpp は 2 型とも名指ししていて正しい) — 0 に見えたら数え方が壊れている。
add_test(NAME srava_nef_variant_guard
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_nef_variant_guard.sh ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(srava_nef_variant_guard PROPERTIES
  PASS_REGULAR_EXPRESSION "NEFVAR-OK"
  SKIP_REGULAR_EXPRESSION "NEFVAR-SKIP"
  SKIP_RETURN_CODE 77
  FAIL_REGULAR_EXPRESSION "FAIL:"
  TIMEOUT 60)

# ★★ #3522: **番犬の歯抜けを機械で数える** (test/srava_hangwatch_coverage.sh)。
#   番犬は 1 行 source しないとカバーされず、入れ忘れは目視では見えない。
#   ⚠ 2026-09-14 に 47 本中 7 本が漏れていた (番犬は bench 側・7 本は dev-macmini-1 側で
#     独立に書かれ、合流時に約束が落ちた)。⇒ 数える検査を常時走らせる。
#   ★ モジュール構成に依らないので if() の外に置く (この検査自身は srava を起動しない)。
add_test(NAME srava_hangwatch_coverage
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_hangwatch_coverage.sh
          ${CMAKE_CURRENT_SOURCE_DIR}/test)
set_tests_properties(srava_hangwatch_coverage PROPERTIES
  PASS_REGULAR_EXPRESSION "HWCOV-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:"
  TIMEOUT 60)

# ★ #3474 続き: openvdb 系の例外境界 (投げても agent が死なず、名前つきエラーになる)。
# ⚠ **openvdb が建つ構成でだけ登録する。** スクリプト側にも have openvdb のガードを入れたが、
#   そもそも登録しないのが筋 (2026-09-13 に Cygwin で赤くなった 4 本のうちの 1 本)。
if(SRAVA_MODULE_OPENVDB)
  add_test(NAME srava_openvdb_guard
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> vdguard)
  set_tests_properties(srava_openvdb_guard PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-vdguard"
    PASS_REGULAR_EXPRESSION "VDGUARD-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|uncaught exception"
    TIMEOUT 60)
endif()

# ★ #3485: routing への **暗黙依存** の見張り。
#   テストが priority で X を上げているのに式の中に X が持たない op があると、その op は
#   別カーネルへ落ちる = 「X はこの op を持たない」に暗黙に依存している。あとで X にその op を
#   配ると実行者が静かに入れ替わり、値が一致してしまう場合は **テストは落ちず意味だけ失われる**
#   (#3474 で tube を配ったとき、4 箇所のうち落ちたのは 1 箇所だけだった)。
#   ⇒ 依存箇所の一覧を承認済みリストと突き合わせ、増減したら止めて判断を促す。
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME srava_routing_dependency
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/audit_routing_dependency.py --check)
  set_tests_properties(srava_routing_dependency PROPERTIES
    PASS_REGULAR_EXPRESSION "ROUTING-DEP-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:"
    TIMEOUT 60)
else()
  message(STATUS "Python3 が無いので routing 依存の見張り (srava_routing_dependency) は登録しない")
endif()

# ソースファイル実行 (srava file.sra) + 先頭シェバング行 (#!...) の読み飛ばし。
add_test(NAME srava_filearg
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> filearg)
set_tests_properties(srava_filearg PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-filearg"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error|cannot open"
  TIMEOUT 20)

# transform 系(translate/rotate/mirror/transform): Aff_transformation_3<EPECK> を mesh に適用。
# 座標が実際に動く証明として union の頂点/面数で検証(回転は任意角=double cos/sin、mirror は向き補正)。
add_test(NAME srava_xf_translate
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> xlate)
set_tests_properties(srava_xf_translate PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-xf-xl"
  PASS_REGULAR_EXPRESSION "NVF 24 44"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR"
  TIMEOUT 20)
add_test(NAME srava_xf_rotate
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> rotate)
set_tests_properties(srava_xf_rotate PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-xf-rot"
  PASS_REGULAR_EXPRESSION "NVF 22 40"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR"
  TIMEOUT 20)
add_test(NAME srava_xf_mirror
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> mirror)
set_tests_properties(srava_xf_mirror PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-xf-mir"
  PASS_REGULAR_EXPRESSION "NVF 16 24"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR"
  TIMEOUT 20)
add_test(NAME srava_xf_transform
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> xform)
set_tests_properties(srava_xf_transform PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-xf-xfm"
  PASS_REGULAR_EXPRESSION "NVF 24 44"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR"
  TIMEOUT 20)
add_test(NAME srava_xf_rotate_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> rotate_err)
set_tests_properties(srava_xf_rotate_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-xf-err"
  PASS_REGULAR_EXPRESSION "rotate: unknown axis"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 演算子シュガー(>>>=translate / @(axis,d)=rotate / <>=mirror)。mesh ブールより強く結合。
add_test(NAME srava_op_xlate
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> op_xlate)
set_tests_properties(srava_op_xlate PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-op-xl"
  PASS_REGULAR_EXPRESSION "NVF 24 44"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_op_rotate
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> op_rotate)
set_tests_properties(srava_op_rotate PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-op-rot"
  PASS_REGULAR_EXPRESSION "NVF 22 40"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_op_mirror
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> op_mirror)
set_tests_properties(srava_op_mirror PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-op-mir"
  PASS_REGULAR_EXPRESSION "NVF 16 24"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 任意ベクトル軸: rotate(m,[0,0,1],deg) は文字列 "z" と一致(Rodrigues の主軸特例)。
add_test(NAME srava_xf_vec_axis
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> vec_axis)
set_tests_properties(srava_xf_vec_axis PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-vecax"
  PASS_REGULAR_EXPRESSION "NVF 22 40"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR"
  TIMEOUT 20)
# 退化軸ベクトル [0,0,0] は明示エラー。
add_test(NAME srava_xf_vec_degenerate
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> vec_degenerate)
set_tests_properties(srava_xf_vec_degenerate PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-vecdeg"
  PASS_REGULAR_EXPRESSION "degenerate axis vector"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# scale(均等/軸別/演算子 ***)+ 退化(0)エラー。
add_test(NAME srava_scale_uniform
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> scale_uniform)
set_tests_properties(srava_scale_uniform PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sc-u"
  PASS_REGULAR_EXPRESSION "NVF 8 12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_scale_op
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> scale_op)
set_tests_properties(srava_scale_op PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sc-op"
  PASS_REGULAR_EXPRESSION "NVF 8 12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_scale_vec
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> scale_vec)
set_tests_properties(srava_scale_vec PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sc-v"
  PASS_REGULAR_EXPRESSION "NVF 16 24"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_scale_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> scale_err)
set_tests_properties(srava_scale_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sc-e"
  PASS_REGULAR_EXPRESSION "scale: degenerate"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)
# 単項マイナス(負リテラル/負ベクトル)。配列内の負値が serialize↔VALUE で round-trip すること。
add_test(NAME srava_neg_literal
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> neg_literal)
set_tests_properties(srava_neg_literal PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-neg"
  PASS_REGULAR_EXPRESSION "NVF 16 24"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 2D(Step C): cgMesh2D + rect + extrude + 2D booleans + 多態スピン(reader/writer/op_* が次元で分岐)。
add_test(NAME srava_2d_extrude
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> extrude)
set_tests_properties(srava_2d_extrude PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-ext"
  PASS_REGULAR_EXPRESSION "NVF 8 12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_poly_union
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> poly2d_union)
set_tests_properties(srava_2d_poly_union PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-pu"
  PASS_REGULAR_EXPRESSION "NVF 16 28"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_extrude_union3d
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> extrude_union3d)
set_tests_properties(srava_2d_extrude_union3d PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-eu"
  PASS_REGULAR_EXPRESSION "NVF 17 30"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_mixed_dim_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> mixed_dim_err)
set_tests_properties(srava_2d_mixed_dim_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-mix"
  PASS_REGULAR_EXPRESSION "no module can execute op 'union' on input types \\(cg-cross2d,cg-mesh3d\\)"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# ★ #3436 P4: n 項ノードの評価時分解の回帰 (2026-08-25 新設)。木の形は **キャッシュの共有**で見る。
add_test(NAME srava_nary_fold
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_nary_fold.sh $<TARGET_FILE:srava>)
set_tests_properties(srava_nary_fold PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-nary-fold"
  PASS_REGULAR_EXPRESSION "NARY-FOLD-OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|NARY_FAIL|parse error"
  # ★ 2026-08-27 (MinGW 実機): 単独では十秒未満だが、ctest の並列下では旧 TIMEOUT を超えることが
  #   ある (agent を多数起こすので Windows のプロセス起動コストが効く)。★ハングではない
  #   (単独走行では通ることを実機で確認済み)。
  TIMEOUT 300)

# ★ export_vox (voxel化→vox.h5) の routing 回帰 (2026-08-19 新設)。HDF5 が無いビルドでは op ごと
#   無効なのでテストも作らない。manifold 必須 (③④ が mf 入力を使う)。
# ★ #3468: export_vox は openvdb_cg.so が提供するので、その前提を足す
#   (SRAVA_MODULE_OPENVDB は既定 ON・SRAVA_MODULE_OPENVDB_CG_GPL も既定 ON)。
if(HDF5_FOUND AND SRAVA_MODULE_MANIFOLD AND SRAVA_MODULE_OPENVDB AND SRAVA_MODULE_CGAL AND SRAVA_MODULE_OPENVDB_CG_GPL)
  add_test(NAME srava_export_vox
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_export_vox.sh $<TARGET_FILE:srava>)
  set_tests_properties(srava_export_vox PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/voxcache"
    PASS_REGULAR_EXPRESSION "EXPORT-VOX-OK"
    FAIL_REGULAR_EXPRESSION "VOX_FAIL:|Segmentation|Aborted|core dumped|ERROR"
    TIMEOUT 120)
endif()

# 2D プリミティブ拡充(ngon/circle/polygon)。
add_test(NAME srava_2d_ngon
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> prim_ngon)
set_tests_properties(srava_2d_ngon PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-ng"
  PASS_REGULAR_EXPRESSION "NVF 12 20"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_circle
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> prim_circle)
set_tests_properties(srava_2d_circle PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-ci"
  PASS_REGULAR_EXPRESSION "NVF 64 124"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_circle_segs
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> prim_circle_segs)
set_tests_properties(srava_2d_circle_segs PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-ciseg"
  PASS_REGULAR_EXPRESSION "NVF 16 28"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_sphere_subdiv
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> prim_sphere_subdiv)
set_tests_properties(srava_sphere_subdiv PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sph-sd"
  PASS_REGULAR_EXPRESSION "NVF 162 320"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 30)
add_test(NAME srava_2d_polygon
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> prim_polygon)
set_tests_properties(srava_2d_polygon PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-pg"
  PASS_REGULAR_EXPRESSION "NVF 6 8"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_polygon_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> prim_polygon_err)
set_tests_properties(srava_2d_polygon_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-pge"
  PASS_REGULAR_EXPRESSION "polygon: needs"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 穴対応 extrude(CDT 三角化): 穴あき多角形 → トンネル付きプリズム + 閉多様体検証(box union)。
add_test(NAME srava_2d_extrude_hole
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> extrude_hole)
set_tests_properties(srava_2d_extrude_hole PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-eh"
  PASS_REGULAR_EXPRESSION "NVF 16 32"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_extrude_hole_union
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> extrude_hole_union)
set_tests_properties(srava_2d_extrude_hole_union PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-ehu"
  PASS_REGULAR_EXPRESSION "NVF 19 34"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 2D rotate 軸不要(単一角度)+ 演算子 @(deg)。
add_test(NAME srava_2d_rotate
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> rotate2d)
set_tests_properties(srava_2d_rotate PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-rot"
  PASS_REGULAR_EXPRESSION "NVF 8 12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 値返し op(area)を式で観測 = value-parse 対称化 + 演算子の継続 deref + A_SAVE_BEGIN 本文修正。
add_test(NAME srava_area_expr
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> area_expr)
set_tests_properties(srava_area_expr PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-area-expr"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_area_arith
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> area_arith)
set_tests_properties(srava_area_arith PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-area-arith"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# レキシカルスコープ(eager-DEF): 遅延束縛の自由変数が内側シャドウを誤って拾わない。
add_test(NAME srava_lexical_shadow
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> lexical_shadow)
set_tests_properties(srava_lexical_shadow PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-lexshadow"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 配列構築の演算子化: インライン配列内 varref がネスト agent op(union mesh 引数)でも正しい env で解決。
add_test(NAME srava_arr_varref
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> arr_varref)
set_tests_properties(srava_arr_varref PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-arrvarref"
  PASS_REGULAR_EXPRESSION "NVF 16 24"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# SVG/DXF の単位指定(export 第3引数)。SVG=width/height・DXF=$INSUNITS。出力ファイルを grep で検証。
add_test(NAME srava_export_unit
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> export_unit)
set_tests_properties(srava_export_unit PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-exunit-env"
  PASS_REGULAR_EXPRESSION "EXPORT_UNIT_OK"
  FAIL_REGULAR_EXPRESSION "FAIL|Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 20)

# task2: cache dir 初期化 first-agent 移動 + mkdir -p(プログラムが CACHE_DIR を深い新規 dir に設定)。
add_test(NAME srava_cachedir_ctl
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> cachedir_ctl)
set_tests_properties(srava_cachedir_ctl PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-cachectl-env"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# trigger(並列 spark): volume(a)==volume(b) を並列起動。正当性(8!=3→else 25v46f)を検証。
add_test(NAME srava_parallel_cmp
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> parallel_cmp)
set_tests_properties(srava_parallel_cmp PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-parcmp"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# valid/repair(検査=値返し / 修復=mesh 返し。多態 op_valid/op_repair。2D=is_simple/even-odd, 3D=自己交差/autorefine)。
add_test(NAME srava_valid_ok
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> valid_ok)
set_tests_properties(srava_valid_ok PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-valid-ok"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_valid_bad
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> valid_bad)
set_tests_properties(srava_valid_bad PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-valid-bad"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_repair_2d
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> repair_2d)
set_tests_properties(srava_repair_2d PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-repair-2d"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
# ★ #3442 追補: 3D repair (autorefine) は自己交差を **解消しない** ことを固定する
#   (ソースコメントが「valid(=1) に持ち込む」と誤って主張していた・実測で否定された)。
add_test(NAME srava_repair_3d_selfx
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> repair_3d_selfx)
set_tests_properties(srava_repair_3d_selfx PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-repair-selfx"
  PASS_REGULAR_EXPRESSION "REPAIRSELFX_OK"
  FAIL_REGULAR_EXPRESSION "REPAIRSELFX_FAIL|Segmentation|Aborted|core dumped"
  TIMEOUT 60)

add_test(NAME srava_repair_3d
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> repair_3d)
set_tests_properties(srava_repair_3d PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-repair-3d"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 計測の残り(volume/perimeter/centroid)。値返し(scalar)+ 配列返し(centroid・VALUE 復元+添字)+ dim ガード。
add_test(NAME srava_volume
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> volume)
set_tests_properties(srava_volume PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-volume"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_volume_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> volume_err)
set_tests_properties(srava_volume_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-volume-err"
  PASS_REGULAR_EXPRESSION "no module can execute op 'volume' on input types \\(cg-cross2d\\)"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|nv="
  TIMEOUT 20)
add_test(NAME srava_perimeter
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> perimeter)
set_tests_properties(srava_perimeter PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-perimeter"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_centroid_2d
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> centroid_2d)
set_tests_properties(srava_centroid_2d PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-centroid-2d"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_centroid_3d
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> centroid_3d)
set_tests_properties(srava_centroid_3d PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-centroid-3d"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 近接(distance/closest/farthest)= 二項 value-return(新組合せ BINMESH_IN+out=AK_INLINE)・AABB 近接 + 配列返し。
add_test(NAME srava_distance
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> distance)
set_tests_properties(srava_distance PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-distance"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_distance_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> distance_err)
set_tests_properties(srava_distance_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-distance-err"
  PASS_REGULAR_EXPRESSION "no module can execute op 'distance' on input types \\(cg-cross2d,cg-cross2d\\)"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|nv="
  TIMEOUT 20)
add_test(NAME srava_closest
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> closest)
set_tests_properties(srava_closest PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-closest"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_farthest
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> farthest)
set_tests_properties(srava_farthest PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-farthest"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# #4 性能崖の回帰ガード: 巨大インライン配列(2048 点・serialize 後 >64KB)。崖が再発すると TIMEOUT で落ちる。
add_test(NAME srava_bigtube
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> bigtube)
set_tests_properties(srava_bigtube PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-bigtube"
  PASS_REGULAR_EXPRESSION "NVF 12290 24576"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)

# tube(3D 折れ線まわりの掃引管)+ 太さ可変/尖り端 + 閉多様体検証。
add_test(NAME srava_tube
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> tube)
set_tests_properties(srava_tube PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-tube"
  PASS_REGULAR_EXPRESSION "NVF 18 32"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_tube_taper
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> tube_taper)
set_tests_properties(srava_tube_taper PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-tube-tp"
  PASS_REGULAR_EXPRESSION "NVF 10 16"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_tube_union
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> tube_union)
set_tests_properties(srava_tube_union PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-tube-un"
  PASS_REGULAR_EXPRESSION "NVF 34 64"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)

# tube: 連続重複頂点を弾かず間引く(spline 等の区間境界重複を吸収)。間引き後 2 頂点 = 素の管 18v32f。
add_test(NAME srava_tube_dedup
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> tube_dedup)
set_tests_properties(srava_tube_dedup PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-tube-dd"
  PASS_REGULAR_EXPRESSION "NVF 18 32"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)

# tube: 2D 次元ディスパッチ(位置=[x,y]→可変半幅の帯 cgMesh2D)。valid な単一領域になることを確認。
add_test(NAME srava_tube2d
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> tube2d)
set_tests_properties(srava_tube2d PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-tube-2d"
  PASS_REGULAR_EXPRESSION "TUBE2D_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)

# ★★★ #3588: tube_ruled の **型スタンプ** (2D の帯が cg-cross2d / mf-cross2d を名乗ること)。
#   ⚠ 上の srava_tube2d は valid() しか見ないので **このバグが在っても緑だった**。
add_test(NAME srava_tube2d_type
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> tube2d_type)
set_tests_properties(srava_tube2d_type PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-tube-2dt"
  PASS_REGULAR_EXPRESSION "TUBE2DTYPE_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)

# revolve(2D→3D 回転体・全周 360°)+ 閉多様体検証 + 部分角エラー。
add_test(NAME srava_2d_revolve
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> revolve)
set_tests_properties(srava_2d_revolve PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-rev"
  PASS_REGULAR_EXPRESSION "NVF 66 128"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_2d_revolve_union
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> revolve_union)
set_tests_properties(srava_2d_revolve_union PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-revu"
  PASS_REGULAR_EXPRESSION "NVF 70 136"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_2d_revolve_partial
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> revolve_partial)
set_tests_properties(srava_2d_revolve_partial PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-revp"
  PASS_REGULAR_EXPRESSION "NVF 20 36"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_2d_revolve_segs
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> revolve_segs)
set_tests_properties(srava_2d_revolve_segs PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-revs"
  PASS_REGULAR_EXPRESSION "NVF 18 32"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_revolve_partial_union
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> revolve_partial_union)
set_tests_properties(srava_2d_revolve_partial_union PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-revpu"
  PASS_REGULAR_EXPRESSION "NVF 22 40"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)

# 2D SVG/DXF I/O: SVG round-trip(穴保持)+ DXF export。
add_test(NAME srava_2d_svg_roundtrip
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> svg_roundtrip)
set_tests_properties(srava_2d_svg_roundtrip PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-svg"
  PASS_REGULAR_EXPRESSION "NVF 16 32"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
# ★カーネル混成の回帰 (2026-08-06): manifold 既定下で cg agent が MFM3/MFC2 を昇格読みできること。
# SRAVA_MODULE_MANIFOLD=ON の中でしか意味を持たないが、add_test はこの位置 (dxf 系と同居) に置き、
# ゲートは下の if で行う。
if(SRAVA_MODULE_MANIFOLD)
# ★manifold 既定の煙テスト (2026-08-06): env に SRAVA_DEFAULT_OUTPUT を入れない = 出荷既定
#   (Manifold 内蔵ビルドでは manifold) そのものを踏む。既存スイートは exact をピン留めして
#   CGAL カバレッジを維持しているため、既定経路はこの群だけが検証する。
add_test(NAME srava_default_kernel_union
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> default_kernel_union)
set_tests_properties(srava_default_kernel_union PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-defk-union"
  PASS_REGULAR_EXPRESSION "VOL 9"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_default_kernel_booleans
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> default_kernel_booleans)
set_tests_properties(srava_default_kernel_booleans PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-defk-bool"
  PASS_REGULAR_EXPRESSION "IVOL 2(\n|.)*DVOL 6"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_default_kernel_export
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> default_kernel_export)
set_tests_properties(srava_default_kernel_export PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-defk-exp"
  PASS_REGULAR_EXPRESSION "TRI 28"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_default_kernel_2d_extrude
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> default_kernel_2d_extrude)
set_tests_properties(srava_default_kernel_2d_extrude PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-defk-ext"
  PASS_REGULAR_EXPRESSION "EVOL 600"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_default_kernel_offset3d
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> default_kernel_offset3d)
set_tests_properties(srava_default_kernel_offset3d PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-defk-off"
  PASS_REGULAR_EXPRESSION "OVOL 1"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 40)

add_test(NAME srava_default_kernel_3mf
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> default_kernel_3mf)
set_tests_properties(srava_default_kernel_3mf PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-defk-3mf"
  PASS_REGULAR_EXPRESSION "ZIP3MF 1"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_default_kernel_import_obj
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> default_kernel_import_obj)
set_tests_properties(srava_default_kernel_import_obj PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-defk-impobj"
  PASS_REGULAR_EXPRESSION "IVOL 8"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

add_test(NAME srava_kernel_mix_cast
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> kernel_mix_cast)
set_tests_properties(srava_kernel_mix_cast PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-kmix-cast"
  PASS_REGULAR_EXPRESSION "VOL 9"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_kernel_mix_cast_downgrade
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> kernel_mix_cast_downgrade)
set_tests_properties(srava_kernel_mix_cast_downgrade PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-kmix-cast-dn"
  PASS_REGULAR_EXPRESSION "VOL 9"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
# ★ leaf 入力 × cross-module 変換の COLD 回帰 (2026-08-12): A_SAVE_BEGIN の mark_valid が無いと
# leaf 生産者の CV_INVALID 焼き込みが下流へ渡り panic。テスト側 (srava_parse.sh) が cache dir を
# 毎回消して cold を保証する。
add_test(NAME srava_kernel_mix_cast_downgrade_leaf
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> kernel_mix_cast_downgrade_leaf)
set_tests_properties(srava_kernel_mix_cast_downgrade_leaf PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-kmix-cast-dnleaf"
  PASS_REGULAR_EXPRESSION "VOL 8"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error|panic"
  TIMEOUT 20)
add_test(NAME srava_kernel_mix_cast_downgrade_2d
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> kernel_mix_cast_downgrade_2d)
set_tests_properties(srava_kernel_mix_cast_downgrade_2d PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-kmix-cast-dn2d"
  PASS_REGULAR_EXPRESSION "AREA 12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error|panic"
  TIMEOUT 20)
add_test(NAME srava_kernel_mix_dxf
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> kernel_mix_dxf)
set_tests_properties(srava_kernel_mix_dxf PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-kmix-dxf"
  PASS_REGULAR_EXPRESSION "result (cache|value)"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
# ★ choice A: cgal 専用 op (manifold が持たない) に mf mesh を渡す暗黙クロスカーネル (sig 化後も維持)。
add_test(NAME srava_kernel_mix_cgalonly
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> kernel_mix_cgalonly)
set_tests_properties(srava_kernel_mix_cgalonly PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-kmix-cgonly"
  PASS_REGULAR_EXPRESSION "CGONLY_OK"
  FAIL_REGULAR_EXPRESSION "CGONLY_FAIL|Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# ★ agent("so","off"/"on") 実行時無効化 (2026-08-10)。cgal を切って manifold へ落ちる/戻すのを
#   三角形数 (cgal union=46 / manifold union=28) で判別。cgal+manifold 両方が要る (cgal.so をロードして
#   off にするので SRAVA_MODULE_CGAL も要件) → manifold ブロック内 + cgal ガード。
if(SRAVA_MODULE_CGAL)
add_test(NAME srava_disable_cgal
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> disable_cgal)
set_tests_properties(srava_disable_cgal PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-disable-cgal"
  PASS_REGULAR_EXPRESSION "TRI 28"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_disable_cgal_reenable
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> disable_cgal_reenable)
set_tests_properties(srava_disable_cgal_reenable PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-reenable-cgal"
  PASS_REGULAR_EXPRESSION "TRI 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
# ★ 1 引数 module(so) = 再ロード (2026-08-18 の糖衣)。off (アンロード) した cgal を 1 引数で戻せること。
add_test(NAME srava_module_sugar_on
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> disable_cgal_sugar)
set_tests_properties(srava_module_sugar_on PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sugar-cgal"
  PASS_REGULAR_EXPRESSION "TRI 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
# ★ 2026-08-28 (ひさ指摘): 同じファイル名で別の実ファイルを module() したら明示エラー。
#   旧実装は黙って no-op = ロードしたつもりで別の .so が動いていた。
# ★ 2026-08-28 (ひさ指摘): cast の routing は sig の**出力型だけ**を見ていた = sig が「受けられない」
#   と申告している型が入力に来ても通っていた。入力型も照合する (sig 本来のチェック + 出力型)。
add_test(NAME srava_cast_sig_input
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> cast_sig_input)
set_tests_properties(srava_cast_sig_input PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-castsig"
  PASS_REGULAR_EXPRESSION "CAST-SIG-INPUT-OK"
  FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 30)
# ★ 2026-08-28 (ひさ要望): srava --module-info = 記述子の申告ダンプ (sig 全リスト等)。
#   --modules (配置の問い) とは別コマンド — 混ぜると配置表が申告に埋もれるため。
add_test(NAME srava_module_info
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> module_info)
set_tests_properties(srava_module_info PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-modinfo"
  PASS_REGULAR_EXPRESSION "MODULE-INFO-OK"
  FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 30)

# ★ #3467: `module::op(...)` — op ごとのモジュール指名。
#   occt / manifold が要る (指名の効き目を「値が構造的に違う」ことで見るため)。
# ★ #3471: occt の text / extrude / revolve / prism (TrueType を 2D 曲線のまま取り込む)。
#   prism を cgal と突き合わせる項があるので cgal も要る。フォントが無い機械では
#   text 系はスクリプト側でスキップされる (prism だけ走る)。
# ★ #3472 の polygonize は occt_mf.so (manifold も要る) が提供する。
#   ★ 2026-09-07: SRAVA_OCCT_TEXT=OFF のビルドでは text op 自体が無いので登録しない
#     (このスクリプトは extrude/revolve/prism も見るが、text と混ざっているので丸ごと外す)。
if(SRAVA_MODULE_OCCT AND SRAVA_MODULE_CGAL AND SRAVA_MODULE_MANIFOLD AND SRAVA_OCCT_TEXT)
  add_test(NAME srava_occt_text
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_occt_text.sh $<TARGET_FILE:srava>)
  # ⚠ 番犬の予算 — **最も遅い機**の実測 x3 (2026-09-22)。mac(arm64/OCCT 7.9.3) 8.25s ・ Linux(x86_64/7.8.1) 4.03s ⇒ 30s。
  set_tests_properties(srava_occt_text PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/occt-text-cache;SRAVA_HANG_SECS=30"
    PASS_REGULAR_EXPRESSION "OCCT-TEXT-OK"
    FAIL_REGULAR_EXPRESSION "TEXT_FAIL|Segmentation|Aborted|core dumped|parse error"
    TIMEOUT 180)
endif()

# ★ #3470: occt の tube (点を通る B-spline の背骨 + 厳密な円断面)。
#   cgal の tube と比べる項があるので cgal も要る。
if(SRAVA_MODULE_OCCT AND SRAVA_MODULE_CGAL)
  add_test(NAME srava_occt_tube
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_occt_tube.sh $<TARGET_FILE:srava>)
  # ⚠ 番犬の予算 — **最も遅い機**の実測 x3 (2026-09-22)。mac(arm64/OCCT 7.9.3) 4.46s ・ Linux(x86_64/7.8.1) 2.05s ⇒ 15s。
  set_tests_properties(srava_occt_tube PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/occt-tube-cache;SRAVA_HANG_SECS=15"
    PASS_REGULAR_EXPRESSION "OCCT-TUBE-OK"
    FAIL_REGULAR_EXPRESSION "TUBE_FAIL|Segmentation|Aborted|core dumped|parse error"
    TIMEOUT 180)
endif()

# ★★ #3567 (2026-09-21): `null` リテラルと比較。**モジュールを 1 本も要らない**言語の検定なので
#   ガードの外に置く (幾何カーネルの構成に関係なく必ず回る)。
#   守っているのは「null / 0 / "" / [] / {} を == で **分けられる**」— 以前の手段は真偽だけで、
#   5 つを全部ひとまとめにしていた。陰性対照 ("1"==1 は false のまま・型違いの < は
#   incomparable のまま) を同じ本の中に入れてある。
add_test(NAME srava_null
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_null.sh $<TARGET_FILE:srava>)
set_tests_properties(srava_null PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/null-cache"
  PASS_REGULAR_EXPRESSION "NULL-LITERAL-OK"
  FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 60)

if(SRAVA_MODULE_OCCT AND SRAVA_MODULE_MANIFOLD AND SRAVA_MODULE_CGAL)
  add_test(NAME srava_module_qualified
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_module_qualified.sh $<TARGET_FILE:srava>)
  # ⚠ 番犬の予算 — **最も遅い機**の実測 x3 (2026-09-22)。mac(arm64/OCCT 7.9.3) 6.90s ・ Linux(x86_64/7.8.1) 3.42s ⇒ 30s。
  set_tests_properties(srava_module_qualified PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/qualified-cache;SRAVA_HANG_SECS=30"
    PASS_REGULAR_EXPRESSION "MODULE-QUALIFIED-OK"
    FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
    TIMEOUT 180)
endif()

# ★ #3555 段3: 擬似モジュール (srava のラムダへ配線する候補)。occt_mf は occt+manifold で建つ。
if(SRAVA_MODULE_OCCT AND SRAVA_MODULE_MANIFOLD AND SRAVA_MODULE_CGAL)
  add_test(NAME srava_module_pseudo
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_module_pseudo.sh $<TARGET_FILE:srava>)
  # ⚠ 番犬の予算 — **最も遅い機**の実測 x3 (2026-09-22)。mac(arm64/OCCT 7.9.3) 12.60s ・ Linux(x86_64/7.8.1) 4.58s ⇒ 45s。
  set_tests_properties(srava_module_pseudo PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/pseudo-cache;SRAVA_HANG_SECS=45"
    PASS_REGULAR_EXPRESSION "MODULE-PSEUDO-OK"
    FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
    TIMEOUT 180)
endif()

# ★ #3574: 擬似モジュール集 lib/module/pseudo.sra (粒度 seg / dx の既定値を練り込む)。
#   ⚠ include を使うので SRAVA_PATH が要る。⚠ geogram / openvdb / occt を名指しで見るので
#     4 カーネル + openvdb が揃った構成でだけ回す (all.sra は optional:1 で黙って飛ぶため、
#     無い構成で回すと「降りた先が違う」を掴めない)。
if(SRAVA_MODULE_OCCT AND SRAVA_MODULE_MANIFOLD AND SRAVA_MODULE_CGAL
   AND SRAVA_MODULE_GEOGRAM AND SRAVA_MODULE_OPENVDB)
  add_test(NAME srava_module_pseudo_lib
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_module_pseudo_lib.sh $<TARGET_FILE:srava>)
  # ⚠ SRAVA_HANG_SECS — Linux 5.8s / **mac 17.1s** (2026-09-22 実測)。既定 10 秒では mac で撮られる。
  #   ★ pm_points (⑩) を足した後の mac 実測: 単独 **4.98s** / -j6 のフル走行で **14.14s**
  #     ⇒ 予算 60 に対し余裕 x4.2。据え置きでよい。
  set_tests_properties(srava_module_pseudo_lib PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/pseudolib-cache;SRAVA_HANG_SECS=60"
    PASS_REGULAR_EXPRESSION "MODULE-PSEUDO-LIB-OK"
    FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
    TIMEOUT 300)
endif()

# ★ #3555 段1: 指名を **候補列** として読む (配列 = 優先順位の上書き・列に無いものは呼ばれない)。
if(SRAVA_MODULE_OCCT AND SRAVA_MODULE_MANIFOLD AND SRAVA_MODULE_CGAL)
  add_test(NAME srava_module_candlist
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_module_candlist.sh $<TARGET_FILE:srava>)
  #   ⚠ SRAVA_HANG_SECS — #3555 段5 / #3570 段2 で本が伸び、素の所要が 10 秒を越えた
  #     (2026-09-22 実測 10.9 秒)。既定のままだと緑のまま捕獲だけが溜まる。
  # ⚠ 番犬の予算 — **最も遅い機**の実測 x3 (2026-09-22)。mac(arm64/OCCT 7.9.3) 26.84s ・ Linux(x86_64/7.8.1) 10.95s ⇒ 90s。
  set_tests_properties(srava_module_candlist PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/candlist-cache;SRAVA_HANG_SECS=90"
    PASS_REGULAR_EXPRESSION "MODULE-CANDLIST-OK"
    FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
    TIMEOUT 180)
endif()
# ★★ #3595 (ひさ確定仕様 2026-09-24): `use` を強くする — use の行で候補列を検査し、
#   `module(配列, opts)` で列をまとめてロードする。★ 不変式
#   「use module(L,{}) の候補列は use L と完全に同一」まで検定する (test の ⑤)。
if(SRAVA_MODULE_OCCT AND SRAVA_MODULE_CGAL)
  add_test(NAME srava_use_list
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_use_list.sh $<TARGET_FILE:srava>)
  # ⚠ 番犬の予算 — Linux(x86_64) 実測 1.63s (2026-09-24)。★ mac は **未実測**なので、
  #   既知の機差の上限 (他の本で 9.5 倍) を当てて 15.5s とみなし、その x3 で 45s。
  #   ⇒ mac 側で実測が取れたら、そちらの値 x3 へ差し替えること (両機を並べて遅い方から取る)。
  set_tests_properties(srava_use_list PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/uselist-cache;SRAVA_HANG_SECS=45"
    PASS_REGULAR_EXPRESSION "USE-LIST-OK"
    FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
    TIMEOUT 180)
endif()
# ★★ #3595 の応用 (ひさ 2026-09-24): **ライブラリ関数は自分の候補列を関数の頭で宣言する**。
#   lib/std の幾何を使う関数に `use [サポート…, module([必須],{})]` を入れた。その 4 本が
#   ①名前が実在 ②呼び手の列に引きずられない (陰性対照つき) ③必須は自動ロード
#   ④1 本も無ければ use の行で落ちる、を満たすことを見る。
#   ⚠ ①は宣言に出る **全モジュール**をロードするので、全部建っている構成でのみ意味を持つ。
if(SRAVA_MODULE_CGAL AND SRAVA_MODULE_MANIFOLD AND SRAVA_MODULE_OCCT AND SRAVA_MODULE_GEOGRAM
   AND SRAVA_MODULE_CHERCHI AND SRAVA_MODULE_OPENVDB AND SRAVA_MODULE_NEF_HYBRID
   AND SRAVA_MODULE_PIPEPROX)
  add_test(NAME srava_lib_use_decl
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_lib_use_decl.sh $<TARGET_FILE:srava>)
  # ⚠ 番犬の予算 — Linux(x86_64) 実測 0.63s (2026-09-24 ・ 網羅検査 ①-b を足して 0.45→0.63)。
  #   mac 未実測なので既知の機差の上限 (他の本で 9.5 倍) を当てて 6.0s とみなし、その x3 で 20s。
  #   mac の実測が出たら差し替える。
  set_tests_properties(srava_lib_use_decl PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/libusedecl-cache;SRAVA_HANG_SECS=20"
    PASS_REGULAR_EXPRESSION "LIB-USE-DECL-OK"
    FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
    TIMEOUT 120)
endif()

# ★★ #3595 段3: 同梱モジュールは **.so の幹 == 記述子名**。module(配列) の推測生成
#   (<記述子名> + 拡張子) が立っている前提そのものなので機械で留める。
#   ★ 較正 (幹を変えた .so を足すと見つかる) をテスト側に同梱してある。
add_test(NAME srava_module_name_matches_so
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_module_name_matches_so.sh $<TARGET_FILE:srava>)
# ⚠ 番犬の予算 — Linux(x86_64) 実測 0.36s (2026-09-24)。mac 未実測 ⇒ 上と同じ当て方で 15s。
set_tests_properties(srava_module_name_matches_so PROPERTIES
  ENVIRONMENT "SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/namecheck-cache;SRAVA_HANG_SECS=15"
  PASS_REGULAR_EXPRESSION "MODULE-NAME-MATCHES-SO-OK"
  FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 120)

# ★★ すべてのモジュールについて、agent が **C_ARG_END の前に stdin が切れたら終了する**こと。
#   planner の撤収 (piggyback / in-flight dedup / a4877e7 のキャッシュ HIT) は **wfd を閉じて
#   EOF で畳む** 一本道なので、ここが塞がると「撤収したつもりの agent が残る」。
#   ★ 較正をモジュールごとに同梱 — EOF を与えなければ切られるまで生きていること (rc=124) を
#     先に確かめ、通ったものだけ判定する。ロードに失敗した agent は EOF と無関係に即死するため、
#     較正が無いと **壊れているモジュールほど緑になる**。
#   ⚠ 予算 — 実測 71s (22 モジュール ・ Linux x86_64 と macOS arm64 で **同値** ・ 2026-09-25)。
#     ★ 所要は **固定の待ち (較正 + EOF 後の猶予) が支配的**で、機械の速さにほぼ依らない。
#       ⇒ 「速い機だから短い」ではないので、**短く返ったら走っていない**と疑ってよい
#         (飛ばした場合はほぼ即時)。モジュール数には線形に伸びる。
#   ★ 走れない機では **77 = Skipped** にする (緑でも赤でもない)。2 通りある:
#       ・Windows (MinGW/MSYS) — shell のパイプ経由の stdin が未対応 (#3600)
#       ・timeout / gtimeout が無い機 — 較正に打ち切りが要る ⇒ coreutils を入れる
#     ⚠⚠ PASS_REGULAR_EXPRESSION に skip の印を足してはいけない (**飛ばした検定が緑になる**)。
#     ★ **SKIP_RETURN_CODE と SKIP_REGULAR_EXPRESSION を両方付ける** — 既存 3 本 (2926/2940/2956)
#       と同じ形。終了コードと出力のどちらが先に壊れても Skipped に倒れる。⇒ 「2 ファイルに
#       分かれた commit の中間状態で偽の赤になる」を**運用の注意ではなく機械の性質で消す**
#       (macMINI 指摘 2026-09-25 ・ 実測: SKIP_REGULAR_EXPRESSION 単独でも PASS に勝つ)。
add_test(NAME srava_agent_eof_before_end
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_agent_eof_before_end.sh $<TARGET_FILE:srava> $<TARGET_FILE:srava_agent>)
set_tests_properties(srava_agent_eof_before_end PROPERTIES
  ENVIRONMENT "SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/agenteof-cache;SRAVA_HANG_SECS=240"
  PASS_REGULAR_EXPRESSION "AGENT_EOF_OK"
  SKIP_REGULAR_EXPRESSION "EOF_SKIP"
  SKIP_RETURN_CODE 77
  FAIL_REGULAR_EXPRESSION "EOF_FAIL|Segmentation|Aborted|core dumped"
  TIMEOUT 600)

# ★ 2026-08-28 (ひさ設計): module(so,"off") を実アンロード (dlclose) にした。
#   未ロードへの off は明示エラー・module_loaded で判定・再ロード可・使用済みは落とせない。
add_test(NAME srava_module_unload_reload
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> module_unload_reload)
set_tests_properties(srava_module_unload_reload PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-unload;LIBDIR=${CMAKE_CURRENT_SOURCE_DIR}/lib"
  PASS_REGULAR_EXPRESSION "MODULE-UNLOAD-RELOAD-OK"
  FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 60)
# ★★ #3558: module(so,{optional:1}) は「入っていない」だけを飲み込む。**在るのに使えない**
#   (ABI 不一致 / 記述子違反 / モジュールでない .so / 壊れたファイル) は optional でも落とす。
#   ⚠ 検定材料の 2 本は **わざと拒否される .so**。探索路に置くと `srava --modules` の診断に
#     常時ゴミが並ぶので、専用サブディレクトリ testmod-bad/ へ出す (名指ししたときだけ読まれる)。
#   ★ INSTALL を付けない = 配布物には入れない。
srava_add_module(testmod_badabi
  OUTPUT   badabi
  SOURCES  test/modules/srava_testmod_bad.cpp
  DEFINES  TESTMOD_BAD_ABI=1)
srava_add_module(testmod_badsig
  OUTPUT   badsig
  SOURCES  test/modules/srava_testmod_bad.cpp
  DEFINES  TESTMOD_BAD_SIG=1)
# ⚠⚠ 「モジュールでない .so」も **建てる**。libpig で代用すると OS ごとに別々に壊れる:
#   mac では libpig.dylib が拡張子正規化で libpig.so へ化けて検定が空振りし、
#   Linux では libpig のコピーを dlopen すると **大域状態が二重化して返ってこない**。
srava_add_module(testmod_notamodule
  OUTPUT   notamodule
  SOURCES  test/modules/srava_testmod_bad.cpp
  DEFINES  TESTMOD_NOT_A_MODULE=1)
set_target_properties(testmod_badabi testmod_badsig testmod_notamodule PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/testmod-bad")
add_test(NAME srava_module_optional_refused
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> module_optional_refused)
set_tests_properties(srava_module_optional_refused PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-optref;BADABI=$<TARGET_FILE:testmod_badabi>;BADSIG=$<TARGET_FILE:testmod_badsig>;NOTAMOD=$<TARGET_FILE:testmod_notamodule>"
  PASS_REGULAR_EXPRESSION "MODULE-OPTIONAL-REFUSED-OK"
  FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 30)
add_test(NAME srava_module_dup_name
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> module_dup_name)
set_tests_properties(srava_module_dup_name PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-dupname"
  PASS_REGULAR_EXPRESSION "MODULE-DUP-NAME-OK"
  FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 30)
# ★ #3439 ⑥: module(so,"off") ≡ 「最初からロードしなかった場合」。型/拡張子/routing の各層で
#   off にしたモジュールが見えないこと + 次点へ落ちること + on で戻せることを固定する。
add_test(NAME srava_module_off_invisible
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> module_off_invisible)
set_tests_properties(srava_module_off_invisible PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-off-invisible"
  PASS_REGULAR_EXPRESSION "MODULE-OFF-INVISIBLE-OK"
  FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 40)
# ★ #3439 ①: cast の目標型を産出できるモジュールが無ければ明示エラー (旧: 黙って別型が返っていた)。
#   cgal を off にして cg 型へ cast する経路も見るので cgal ガード内に置く。
add_test(NAME srava_cast_no_producer
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> cast_no_producer)
set_tests_properties(srava_cast_no_producer PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-cast-noprod"
  PASS_REGULAR_EXPRESSION "CAST-NO-PRODUCER-OK"
  FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 30)
# ★★ #3554 最後の段 2/5 (2026-09-19): cast の振り分けが行のマッチ関数に移った
#   (専用ブロック / wantOut は撤去)。守っているのは **1 行 1 出力型** —
#   1 行に 2 つ書くと「行が成立するか」と「実際に名乗る型」が別の述語になり、
#   area(cast("cg-cross2d", box(2,2,2))) が 24 (3D の表面積) を返す = 入力がそのまま返る。
add_test(NAME srava_cast_target_row
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> cast_target_row)
set_tests_properties(srava_cast_target_row PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-cast-targetrow"
  PASS_REGULAR_EXPRESSION "CAST-TARGET-ROW-OK"
  FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 30)
# ★★ #3554 最後の段 3/5: import の行が **拡張子が産む型**で選ばれること。
#   値は常に正しい (本体はファイルを読むだけ) ので、*名乗る型* を volume の routing エラーで読む。
add_test(NAME srava_import_ext_row
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> import_ext_row)
set_tests_properties(srava_import_ext_row PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-import-extrow"
  PASS_REGULAR_EXPRESSION "IMPORT-EXT-ROW-OK"
  FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 30)
# ★★ #3554 最後の段 4/5: export の行が拡張子 × sig で選ばれ、**規約① (自型優先) は無い**こと。
#   どのモジュールが書いたかは STL のヘッダ 80 バイトに出る (cgal="FileType: Binary" / manifold=0 埋め)。
add_test(NAME srava_export_ext_row
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> export_ext_row)
set_tests_properties(srava_export_ext_row PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-export-extrow"
  PASS_REGULAR_EXPRESSION "EXPORT-EXT-ROW-OK"
  FAIL_REGULAR_EXPRESSION "FAIL\\(|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 30)
add_test(NAME srava_disable_bad_option
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> disable_bad_option)
set_tests_properties(srava_disable_bad_option PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-disable-bad"
  PASS_REGULAR_EXPRESSION "the only string option is"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 20)
endif()   # SRAVA_MODULE_CGAL (disable_cgal 系)

# ★ 旧 load(so) op の廃止 (2026-08-18)。module(so) が兼ねるので撤去した。黙って別の意味に
#   ならずエラーになることを固定する (カーネルの有無に依存しないのでガード外)。
add_test(NAME srava_load_op_removed
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> load_op_removed)
set_tests_properties(srava_load_op_removed PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-load-removed"
  # ★ #3570 段3.5: 呼びの形の名前が変数でないときは op 層の診断を出すようになった。
  PASS_REGULAR_EXPRESSION "no module can execute op 'load'"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)
endif()

add_test(NAME srava_2d_dxf_export
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> dxf_export)
set_tests_properties(srava_2d_dxf_export PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-dxf"
  PASS_REGULAR_EXPRESSION "result (cache|value)"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_dxf_roundtrip
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> dxf_roundtrip)
set_tests_properties(srava_2d_dxf_roundtrip PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-dxfr"
  PASS_REGULAR_EXPRESSION "NVF 16 32"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 2D offset(straight skeleton・EPECK 出力): インセット/肉厚枠/消滅。
add_test(NAME srava_2d_offset_inset
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> offset_inset)
set_tests_properties(srava_2d_offset_inset PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-offi"
  PASS_REGULAR_EXPRESSION "NVF 8 12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_2d_offset_shell
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> offset_shell)
set_tests_properties(srava_2d_offset_shell PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-offs"
  PASS_REGULAR_EXPRESSION "NVF 16 32"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_2d_offset_vanish
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> offset_vanish)
set_tests_properties(srava_2d_offset_vanish PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-offv"
  PASS_REGULAR_EXPRESSION "NVF 0 0"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)

# ★ 3D offset のテストは **nef へ振り替えた** (#3440 の 2)。srava_nef_{snc,hybrid}_offset が
#   膨張 / 収縮 / 中空箱 / 非有界エラーを見る。cgal に残るのは 2D offset だけ (上の srava_2d_offset*)。
# キャッシュ HIT/MISS ログ: 同ソース 2 回実行 → 2 回目は全 HIT(miss=0)= 再利用が効いている。
add_test(NAME srava_cache_hit
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> cachehit)
set_tests_properties(srava_cache_hit PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-hit-test"
  # ★★ #3569 で発見: 旧 "0 miss" は **エラーで何も走らなかったとき**の
  #   「cache: 0 hit(s), 0 miss(es)」にも当たるので、**緑のままエラー**になり得た
  #   (実際、module() を足し忘れた移行途中でそうなっていた)。
  #   ⇒ 「1 つ以上 hit していて miss が 0」を要求し、ERROR も失敗語に入れる。
  PASS_REGULAR_EXPRESSION "[1-9][0-9]* hit\\(s\\), 0 miss"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error|ERROR"
  TIMEOUT 20)

# system(cmd): ts2System で非同期実行(完了まで待つ=評価順保証)。mkdir → export が成功。
add_test(NAME srava_system
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> syscmd)
set_tests_properties(srava_system PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-syscmd"
  PASS_REGULAR_EXPRESSION "SYS_OK"
  FAIL_REGULAR_EXPRESSION "SYS_FAIL|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 20)
# system の終了コードを式で観測(start_flag を _start 後に立てる修正)。true→then / false→else。
add_test(NAME srava_system_rc
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> sysrc)
set_tests_properties(srava_system_rc PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sysrc"
  PASS_REGULAR_EXPRESSION "SYSRC_OK"
  FAIL_REGULAR_EXPRESSION "SYSRC_FAIL|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 20)

# 出力ファイル削除 → 再実行で再生成(起動時スイープが stale な D_REF を削除 → export 再実行)。
add_test(NAME srava_export_regen
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> export_regen)
set_tests_properties(srava_export_regen PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-regen-cache"
  PASS_REGULAR_EXPRESSION "REGEN_OK"
  FAIL_REGULAR_EXPRESSION "REGEN_FAIL|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 20)

# 単位つき出力形式 AMF/3MF(どちらも自前・依存なし・全環境)。AMF=unit 属性+三角形、3MF=ZIP(PK)+unit。
add_test(NAME srava_export_formats
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> export_formats)
set_tests_properties(srava_export_formats PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-expfmt"
  PASS_REGULAR_EXPRESSION "EXPFMT_OK"
  FAIL_REGULAR_EXPRESSION "EXPFMT_FAIL|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 30)

# 配列/ハッシュ内 mesh の print: 要素を print() で辿り解決(キャッシュパス)。従来は (delayed . <delayed>)。
add_test(NAME srava_print_mesh_array
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> print_mesh_array)
set_tests_properties(srava_print_mesh_array PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pma"
  PASS_REGULAR_EXPRESSION "PMA_OK"
  FAIL_REGULAR_EXPRESSION "PMA_FAIL|delayed|Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 30)

# パース失敗(; 欠落)→ pigDataError → 異常終了。
add_test(NAME srava_parser_syntaxerr
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> syntaxerr)
set_tests_properties(srava_parser_syntaxerr PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-parser-se"
  PASS_REGULAR_EXPRESSION "parse error"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 2.2 mesh ブーリアン intersection(&&&)/difference(---)。box(2,2,2) op box(1,1,3) の頂点/面数で検証。
add_test(NAME srava_op_intersection
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> intersection)
set_tests_properties(srava_op_intersection PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-op-int"
  PASS_REGULAR_EXPRESSION "NVF 15 26"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op"
  TIMEOUT 20)
add_test(NAME srava_op_difference
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> difference)
set_tests_properties(srava_op_difference PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-op-diff"
  PASS_REGULAR_EXPRESSION "NVF 16 28"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op"
  TIMEOUT 20)

# 2.1 言語拡張: if/else + ブロック + 比較 + 自己代入(strict SET)。
add_test(NAME srava_lang_ifaccum
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> ifaccum)
set_tests_properties(srava_lang_ifaccum PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-if1"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)
add_test(NAME srava_lang_ifskip
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> ifskip)
set_tests_properties(srava_lang_ifskip PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-if2"
  PASS_REGULAR_EXPRESSION "NVF 8 12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 2.x array リテラル + 添字参照(ソース)。
add_test(NAME srava_lang_array
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> arrayidx)
set_tests_properties(srava_lang_array PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-arr"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 2.x source 側 hash リテラル {k:v,..} + メンバ参照(層化文法で文頭 { ブロック衝突を解消)。
add_test(NAME srava_lang_hash
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> hashlit)
set_tests_properties(srava_lang_hash PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-hash"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 2.x 構造 inline 引数の実ワイヤ接続: array を serialize→C_ARG_INLINE→agent value-parse→cgaBox 展開。
add_test(NAME srava_inline_struct
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> inlineval)
set_tests_properties(srava_inline_struct PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-inlinev"
  PASS_REGULAR_EXPRESSION "NVF 8 12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 2.1 lambda + apply(clone/thunk 再評価モデル)。
add_test(NAME srava_lambda
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> lambda)
set_tests_properties(srava_lambda PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-lambda"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 20)

# 2.1 同一 lambda の再 apply(body->clone() でメモ衝突回避)。
add_test(NAME srava_lambda_reapply
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> lambda_reapply)
set_tests_properties(srava_lambda_reapply PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-lambda-re"
  PASS_REGULAR_EXPRESSION "NVF 33 62"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 20)

# 2.1 クロージャ(カリー化): 捕捉した env を親に持つ lambda を返す。
add_test(NAME srava_lambda_closure
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> lambda_closure)
set_tests_properties(srava_lambda_closure PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-lambda-cl"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 20)

# クロージャの値捕捉(by-value): 定義時の自由変数値を凍結 → 後の再代入に影響されない(env-snapshot)。
add_test(NAME srava_closure_capture
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> closure_capture)
set_tests_properties(srava_closure_capture PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-clo-cap"
  PASS_REGULAR_EXPRESSION "CAPTURE_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 20)

# 配列/ハッシュの値捕捉は spine を deep copy: 捕捉後の破壊代入(a[i]=v)が漏れない(capture_copy)。
add_test(NAME srava_closure_deepcopy
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> closure_deepcopy)
set_tests_properties(srava_closure_deepcopy PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-clo-dc"
  PASS_REGULAR_EXPRESSION "DEEPCOPY_OK"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 20)

# ワーカーゲート: cap(=2) が木の深さより小さくてもデッドロックせず完走する(タイマー緩和)。
# 完走すると末尾に "worker gate: cap=2" サマリが出る。ハングなら TIMEOUT で失敗。
add_test(NAME srava_workergate
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> workergate)
set_tests_properties(srava_workergate PROPERTIES
  # ★ 2026-08-30: 旧名 PIG_MAX_WORKERS を撤去し SRAVA_LOAD_CPU=0 + SRAVA_LOAD_AGENT=2 へ。
  #   ⚠ あわせて SRAVA_LOAD_RAMP=0 を足した。ランプ有効だと開始値 2 のまま終わるので、
  #   **上限が壊れていても「実効 2」が出て通ってしまう** (= 検証しないテストだった)。
  #   ランプを切ると目標値が即適用されるので、上限そのものを見ることになる。
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-wgate;SRAVA_LOAD_CPU=0;SRAVA_LOAD_AGENT=2;SRAVA_LOAD_RAMP=0"
  # ★ 2026-08-21: サマリの文言を変えた (§13.7 のランプで limit が動くので「fork 上限により」が嘘になった)。
  # ★ 2026-08-30: サマリの語を「実効」→「ピーク同時」に変えた (終了時の limit と混同されたため)。
  #   このテストが見たいのは cap=2 で完走することなので、ピーク同時 2 を見るのが本来正しい。
  PASS_REGULAR_EXPRESSION "worker gate: ピーク同時 2"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 40)

# fork EAGAIN: ゲート上限(32) が実 fork 上限(2)を超えると、limit 固定方針なので backoff せず
# 「上限を下げて再実行」の明確なエラーで終了する(ハングや黙ったデッドロックを避ける)。
add_test(NAME srava_workergate_eagain
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> workergate_eagain)
set_tests_properties(srava_workergate_eagain PROPERTIES
  # ⚠ ★ 2026-08-21: **ランプを切って走らせる** (SRAVA_LOAD_RAMP=0)。このテストが見たいのは
  #   「fork が EAGAIN で失敗したときの報告」で、そこへ行くには**高い並列度が要る**。§13.7 の
  #   ランプは 2 から 1 ずつしか上げないので、ランプ有効だと PIG_TEST_FORKLIMIT=2 に届かず
  #   **何も起きないまま通ってしまう** (= 検証しないテストになる)。ランプ自体の検証は別。
  # ★ 2026-08-30: 旧名 2 つを撤去。PIG_MAX_WORKERS → SRAVA_LOAD_CPU=0 + SRAVA_LOAD_AGENT=32、
  #   SRAVA_LOAD_RAMP_OFF=1 → SRAVA_LOAD_RAMP=0。⚠ 後者も refresh_config() が読み直さないので
  #   250ms 後にランプが復活していた (PIG_MAX_WORKERS と同じ穴)。
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-wgate-ea;SRAVA_LOAD_CPU=0;SRAVA_LOAD_AGENT=32;PIG_TEST_FORKLIMIT=2;SRAVA_LOAD_RAMP=0"
  PASS_REGULAR_EXPRESSION "exceeds this machine.s fork/process limit"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|Timeout"
  TIMEOUT 40)

# fd リーク回帰: ulimit -n 64・cap=8(同時 8 agent で fd は収まる)で多数の agent を回す。FIN で rfd/pipe を
# 閉じれば fd は再利用され完走("result cache="/"result value="・export の out_cache=0 化で後者)。
# 閉じ忘れ=リークがあれば fd が累積枯渇し、launch 失敗で
# fork/process limit 超過の ERROR[ になり FAIL。★ limit 固定方針なので backoff に頼らず cap を明示する。
# ★ キャッシュの死体 (書きかけ) の後始末の回帰 (2026-08-26)。agent が SIGABRT 等で書きかけの
#   まま死んだとき、残った .cache が **黙って有効扱いされない**こと。機構 (W_END 番兵 + writer_pid
#   の生存) は前からあったが、テストが 1 本も無かった。
#   ⚠ 値だけ見るテストでは検出できない (volume の値キャッシュが短絡して正しい値が出てしまう)
#   ので、sweep の実施と **ファイルが作り直されたこと**まで見る。
# ★ モジュールに **可変なプロセス大域変数 (static)** が増えていないかの lint (ひさ指示 2026-08-26)。
#   モジュールは in-proc (EXEC_THREAD) で走りうる = 1 プロセスに複数 op が同居するので、
#   可変な static は op どうしで混線する。正当な理由があるものは test/mutable_static_allow.txt へ。
add_test(NAME srava_no_mutable_static
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_no_mutable_static.sh ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(srava_no_mutable_static PROPERTIES
  PASS_REGULAR_EXPRESSION "NO-MUTABLE-STATIC-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:"
  TIMEOUT 60)

foreach(_cp sweep recycled)
  add_test(NAME srava_cache_corpse_${_cp}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_cache_corpse.sh $<TARGET_FILE:srava> ${_cp})
  set_tests_properties(srava_cache_corpse_${_cp} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/corpsecache-${_cp};SRAVA_MODULE_ALL=1;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib"
    PASS_REGULAR_EXPRESSION "CORPSE-[A-Z]+-OK"
    SKIP_REGULAR_EXPRESSION "CORPSE-[A-Z]+-SKIP"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 120)
endforeach()

# ★ モジュールが投げた例外を **ホスト側 (ptsCalcBody) の安全網**が受け止める回帰 (ひさ判断 2026-08-26)。
#   in-proc は planner と同じプロセスなので、網が無いと **planner ごと SIGABRT** で死ぬ (実測)。
#   process は agent が死んで "agent died with SIGABRT" になる。どちらも網があれば op のエラー。
foreach(_mt inproc process)
  add_test(NAME srava_module_throw_${_mt}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> module_throw_${_mt})
  set_tests_properties(srava_module_throw_${_mt} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/throwcache-${_mt};SRAVA_HANG_SECS=60"
    PASS_REGULAR_EXPRESSION "MODULE-THROW-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|core dumped"
    TIMEOUT 120)
endforeach()

# ★ gate(x, side) の side がエラーのとき無音にならない回帰 (ひさ指摘 2026-08-26)。
#   ptsFireAndForget は値を捨てるので、誰も結果を見ない呼び手だと失敗が完全に無音だった。
add_test(NAME srava_gate_side_error
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> gate_side_error)
set_tests_properties(srava_gate_side_error PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/gatecache;SRAVA_MODULE_ALL=1;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib;SRAVA_HANG_SECS=60"
  PASS_REGULAR_EXPRESSION "GATE-SIDE-ERROR-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
  TIMEOUT 120)

add_test(NAME srava_fdleak
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> fdleak)
set_tests_properties(srava_fdleak PROPERTIES
  # 同時 agent 上限 4 (2026-08-01): ピーク fd 使用量は同時 agent 数にほぼ比例し、最小で通る
  # ulimit -n は同時 agent 数に比例して増える (実測)。W8 では上限に対する余裕が乏しく、
  # フル ctest の同時実行下で "fork failed" になることがあった。W4 なら十分な余裕がある。
  # fork 回数(240)は変わらないのでリーク検出力は落ちない (1 agent 1 fd でも漏れれば 40 個で枯渇)。
  # ★ 2026-08-30: 旧名 PIG_MAX_WORKERS を撤去し SRAVA_LOAD_CPU=0 + SRAVA_LOAD_AGENT=4 へ。
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-fdleak;SRAVA_LOAD_CPU=0;SRAVA_LOAD_AGENT=4"
  PASS_REGULAR_EXPRESSION "result (cache=|value=)"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR\\["
  TIMEOUT 60)

# 2.1 while ループ(毎周 clone 再評価・共有 env でループ変数更新)。
add_test(NAME srava_while
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> while_loop)
set_tests_properties(srava_while PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-while"
  PASS_REGULAR_EXPRESSION "NVF 52 100"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 30)

# 2.1 for ループ(while への desugar)。
add_test(NAME srava_for
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> for_loop)
set_tests_properties(srava_for PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-for1"
  PASS_REGULAR_EXPRESSION "NVF 52 100"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 30)

# 2.1 入れ子 for(内側ループが外側 body の clone で毎周再初期化される)。
add_test(NAME srava_for_nested
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> for_nested)
set_tests_properties(srava_for_nested PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-for2"
  PASS_REGULAR_EXPRESSION "NVF 33 62"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 30)

# 2.1 一般呼び出し(直接/連鎖適用): callee は任意の式。中間変数なしのカリー適用。
add_test(NAME srava_apply_chain
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> apply_chain)
set_tests_properties(srava_apply_chain PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-applychain"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 20)

# 2.1 再帰 lambda(引数 call-by-value で変数捕捉回避)。
add_test(NAME srava_recursion
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> recursion)
set_tests_properties(srava_recursion PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-recursion"
  PASS_REGULAR_EXPRESSION "NVF 52 100"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 30)

# 2.2 import(path): 外部ファイル(STL/OFF/..)読み込み。export→import 往復 + DAG 葉として union。
add_test(NAME srava_import
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> import)
set_tests_properties(srava_import PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-import"
  PASS_REGULAR_EXPRESSION "NVF 25 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 30)

# 1.2.3 可変ソート: 可換 op の引数順正規化(a|||b と b|||a が同一キャッシュキー、--- は順序保持)。
add_test(NAME srava_commsort
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_commsort.sh $<TARGET_FILE:srava>)
set_tests_properties(srava_commsort PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-commsort"
  PASS_REGULAR_EXPRESSION "COMMSORT_OK"
  FAIL_REGULAR_EXPRESSION "COMMSORT_FAIL|Segmentation|Aborted"
  TIMEOUT 30)

# 1.2.3/§4 実行木分解: n-ary 可換呼び出し union(a,b,c) を二項木に分解(agent は二項のみ)。
add_test(NAME srava_nary
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> nary)
set_tests_properties(srava_nary PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-nary"
  PASS_REGULAR_EXPRESSION "NVF 27 50"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 30)

# 2.2 import 失敗は明示エラー(従来サイレント空メッシュ nv=0 だったのを修正)。
add_test(NAME srava_import_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> import_err)
set_tests_properties(srava_import_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-importerr"
  PASS_REGULAR_EXPRESSION "import: failed to read"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|nv="
  TIMEOUT 20)

# 2.2 失敗 import が下流 agent(union)の上流: クリーン伝播 + orphan agent でハングしない。
add_test(NAME srava_import_err_union
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> import_err_union)
set_tests_properties(srava_import_err_union PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-importerru"
  PASS_REGULAR_EXPRESSION "import: failed to read"
  FAIL_REGULAR_EXPRESSION "arg type/index mismatch|Segmentation|Aborted|nv="
  TIMEOUT 15)

# 2.x 値リテラルの共有表現: serialize ↔ VALUE モードパース 往復(hash はキーソート+クォート)。
# 入力 {b:2,a:1} → 正準 {"a":1,"b":2}(ソート・クォート)を検証。
add_test(NAME srava_value_roundtrip COMMAND srava)
set_tests_properties(srava_value_roundtrip PROPERTIES
  ENVIRONMENT "SRAVA_CACHE_DIR=/tmp/srava-val;SRAVA_VALUE={b:2,a:1}"
  PASS_REGULAR_EXPRESSION "value=.*\"a\":1,\"b\":2"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 20)

# 2.2 形状生成: prism/pyramid/sphere(agent 側 cga* 生成器)。
add_test(NAME srava_shape_prism
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> prism)
set_tests_properties(srava_shape_prism PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-prism"
  PASS_REGULAR_EXPRESSION "NVF 14 24"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op"
  TIMEOUT 20)
add_test(NAME srava_shape_pyramid
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> pyramid)
set_tests_properties(srava_shape_pyramid PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pyr"
  PASS_REGULAR_EXPRESSION "NVF 6 8"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op"
  TIMEOUT 20)
add_test(NAME srava_shape_sphere
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> sphere)
set_tests_properties(srava_shape_sphere PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sph"
  PASS_REGULAR_EXPRESSION "NVF 258 512"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op"
  TIMEOUT 20)

# ★ sphere/icosphere の bit 一致 (共通 geodesic.h) と tube の一致 (共通 common/tube.h・#3415) は
#   **カーネル一致の表** (#3432・ファイル末尾 srava_add_kernel_agree) へ移した。
#   個別に手書きしていた srava_sphere_kernel_agree / srava_tube_kernel_agree は、
#   srava_agree_manifold_{sphere,icosphere,tube3d,tube2d} が同じものを見ている。
if(SRAVA_MODULE_MANIFOLD)
# ★#3415 続き: manifold 側の color + 色つき 3MF export (共通ライタ common/mesh3mf.h)。
#   palette/pid の形・単位・色を付けても幾何が変わらないことを見る。
add_test(NAME srava_mf_color_3mf
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> mf_color_3mf)
set_tests_properties(srava_mf_color_3mf PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-mfcolor"
  PASS_REGULAR_EXPRESSION "MFCOLOR-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 40)

# ★#3415 + color/3mf の到達点: pipe_clearance.sra と同じ形の連鎖 (map→tube→color→combine→色つき 3MF)
#   が **存在しない SRAVA_AGENT** で完走する = 丸ごと in-proc に乗った証明。
add_test(NAME srava_mf_pipe_scene_inproc
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> mf_pipe_scene_inproc)
set_tests_properties(srava_mf_pipe_scene_inproc PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-mfpipe"
  PASS_REGULAR_EXPRESSION "MFPIPE-INPROC-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 60)

# ★in-proc の落とし穴の回帰: map/lambda で作ったネスト配列を取る op (tube/polygon) が
#   in-proc と process で同じ答えになること。配列要素は eager 解決されないので compact() が要る。
add_test(NAME srava_mf_inproc_nested_array
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> mf_inproc_nested_array)
set_tests_properties(srava_mf_inproc_nested_array PROPERTIES
  # ★ bodies の節は pipe_proximity.so を要る。SRAVA_TEST_MODULES に無ければその節を飛ばす。
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-mfnested;SRAVA_TEST_MODULES=${SRAVA_TEST_MODULES}"
  PASS_REGULAR_EXPRESSION "NESTED-INPROC-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 60)

# ★#3415 の眼目: tube 主体の連鎖が丸ごと in-proc (manifold) に乗ること。
#   存在しない SRAVA_AGENT で完走 = agent プロセスを一度も要らなかった証明。
#   cgal 最優先の同式が失敗することも見る (空振りテスト防止)。
add_test(NAME srava_mf_tube_inproc
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> mf_tube_inproc)
set_tests_properties(srava_mf_tube_inproc PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-mftube-inproc"
  PASS_REGULAR_EXPRESSION "MFTUBE-INPROC-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 40)
endif()

# 2.2 ファイル export: 拡張子で形式判定(write_polygon_mesh)。OFF と STL を検証。
add_test(NAME srava_export_off
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_export.sh $<TARGET_FILE:srava> off)
set_tests_properties(srava_export_off PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-export-off"
  PASS_REGULAR_EXPRESSION "OFF-HEADER 25 46 0"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|FAIL:"
  TIMEOUT 20)
add_test(NAME srava_export_stl
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_export.sh $<TARGET_FILE:srava> stl)
set_tests_properties(srava_export_stl PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-export-stl"
  PASS_REGULAR_EXPRESSION "STL-TRIS 46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|FAIL:"
  TIMEOUT 20)

# ------------------------------------------------------------------
# ts2System SIGCHLD teardown バグの最小再現(ctest 非登録 = クラッシュ前提の手動実行用)
#   ./repro_ts2_sigchld  → 2 回目以降の ts2System teardown で SEGV(修正検証用)
# ------------------------------------------------------------------
add_executable(repro_ts2_sigchld
  src/main/repro_ts2_sigchld_main.cpp
  src/classes/pig/c++/tsSysRepro.cpp
)
add_dependencies(repro_ts2_sigchld pigts_codegen)
target_link_libraries(repro_ts2_sigchld PRIVATE pig Threads::Threads m)   # ★ #3427 ③: libpig リンクへ

# ------------------------------------------------------------------
# ts2IO::write_c pipe バッファ超え停止バグの最小再現(ctest 非登録 = ハング前提の手動実行用)
#   TSPIPE_BYTES=131072 timeout 10 ./repro_pipe  → 64KB 超で停止(DONE が出ずハング)
#   TSPIPE_BYTES=32768  ./repro_pipe             → 完走("[repro] DONE wrote ...")
#   TSPIPE_DIVISIBLE=1 で set_divisible の効果を切り分け
# ------------------------------------------------------------------
add_executable(repro_pipe
  src/main/repro_pipe_main.cpp
  src/classes/pig/c++/tsPipeRepro.cpp
)
add_dependencies(repro_pipe pigts_codegen)
target_link_libraries(repro_pipe PRIVATE pig Threads::Threads m)   # ★ #3427 ③: libpig リンクへ

# ------------------------------------------------------------------
# 多数 ts2System 子の一括 teardown レース(worker thread の pipe FIN/abort vs main app teardown)
# の最小再現(ctest 非登録 = 確率的 SEGV のため手動実行用)
#   for i in $(seq 50); do ./repro_teardown >/dev/null 2>&1 || echo "SEGV($?) at $i"; done
#   TSTD_N=48 TSTD_DELAY=300 が既定。N を増やすと出やすい。
# ------------------------------------------------------------------
add_executable(repro_teardown
  src/main/repro_teardown_main.cpp
  src/classes/pig/c++/tsTeardownRepro.cpp
)
add_dependencies(repro_teardown pigts_codegen)
target_link_libraries(repro_teardown PRIVATE pig Threads::Threads m)   # ★ #3427 ③: libpig リンクへ

# ------------------------------------------------------------------
# カーネル: pipe_proximity(可変太さパイプの自己接近検出・MIT・CGAL 非依存)。value 出力の
#   解析カーネル。opt-in。コアライブラリ(pipeprox)は modules/pipe_proximity/vendor/pipeProximity/ に
#   **取り込み済み**(2026-08-12・以前は別 repo を FetchContent していたが同梱に変更)。host 側アダプタ
#   (modules/pipe_proximity/)と一緒に pipe_proximity.so をビルドする(demo/d3 等と同じ .so 機構)。
#   依存方向は一方向(cgal-processor → pipe_proximity)。アダプタ + 登録は host 側。
# ------------------------------------------------------------------
# option(SRAVA_MODULE_PIPEPROX) と旧名の後方互換はファイル冒頭 (§option 集約) で宣言済み。
option(SRAVA_SLOW_TESTS "時間のかかる回帰テスト (std/roll.sra など) も登録する" OFF)
if(SRAVA_MODULE_PIPEPROX)
  # ---- pipeprox コアライブラリ: 取り込み済みソースから直接ビルド (FetchContent 廃止・2026-08-12)。
  #      upstream=v0.1.6・MIT・GLOBALBASE UMUT。取り込み内容は vendor/pipeProximity/VENDORED.md 参照。
  #      bvh.hpp/gradient.cpp の <algorithm> 欠落は取り込みソース側で修正済み (旧 -include algorithm 回避は撤去)。----
  set(PIPEPROX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/modules/pipe_proximity/vendor/pipeProximity)
  add_library(pipeprox STATIC
    ${PIPEPROX_DIR}/src/bezier.cpp
    ${PIPEPROX_DIR}/src/bvh.cpp
    ${PIPEPROX_DIR}/src/proximity.cpp
    ${PIPEPROX_DIR}/src/gradient.cpp
    ${PIPEPROX_DIR}/src/cd_parallel.cpp
    ${PIPEPROX_DIR}/src/controller.cpp)
  target_include_directories(pipeprox PUBLIC ${PIPEPROX_DIR}/include)
  # .a を pipe_proximity.so (MODULE) にリンクするため PIC でビルド (非 PIC はリロケーションエラー)。
  set_target_properties(pipeprox PROPERTIES POSITION_INDEPENDENT_CODE ON)
  find_package(Threads REQUIRED)   # cd_parallel.cpp が std::thread
  target_link_libraries(pipeprox PUBLIC Threads::Threads)

  # 取り込んだ pipeprox の単体回帰テスト (upstream tests/test_pipe.cpp・失敗数=exit code)。
  add_executable(pipeprox_test_pipe ${PIPEPROX_DIR}/tests/test_pipe.cpp
                                    ${PIPEPROX_DIR}/tests/test_cd_parallel.cpp)
  target_link_libraries(pipeprox_test_pipe PRIVATE pipeprox)
  # ---- std/roll.sra (螺旋巻きつけライブラリ・Redmine #3409) の回帰。縮小ドライバでも
  #      **全 ctest より長くかかる**ので既定には入れない。**opt-in**: configure で
  #      -DSRAVA_SLOW_TESTS=ON にしたときだけ登録する。ラベル slow も付けるので、有効化した
  #      ビルドでは `ctest -L slow` で単体指定・`ctest -LE slow` で除外ができる。
  #      (examples/roll_sample.sra のフル版は**分単位**なのでテストには一切入れない。)
  if(SRAVA_SLOW_TESTS)
  add_test(NAME srava_roll_min
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/roll_min.sh $<TARGET_FILE:srava>
            ${CMAKE_CURRENT_SOURCE_DIR}/test/roll_min.sra
            ${CMAKE_CURRENT_SOURCE_DIR}/lib
            ${CMAKE_CURRENT_BINARY_DIR}/roll_min_work)
  set_tests_properties(srava_roll_min PROPERTIES
    LABELS "slow"
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/roll_min_work/cache"
    PASS_REGULAR_EXPRESSION "ROLLMIN=OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|ERROR"
    TIMEOUT 600)
  endif()

  # ★ 2026-08-27 (MinGW 実機): pipe_proximity の 3 本は **Windows では単独でも目に見えて遅く**、
  #   ctest の並列下では更に延びて既定の TIMEOUT を超えていた。★ハングではない —
  #   単独走行では 3 本とも Pass することを実機で確認済み。数値最適化の反復が主で
  #   agent 起動は絡まない (vendored_test は agent を 1 つも起こさない純計算バイナリ) ので、
  #   プロセス起動コストではなく純粋な計算速度差。
  #   ⇒ TIMEOUT は**素で足りていなかったぶんを実態に合わせた**ものであって、
  #     詰まりを TIMEOUT で逃がしているのではない。
  # ---- ★ #3502: 走行中の中断の単体回帰。
  #   ★ ここは **vendor だけ**を叩く (pigBreak も tinyState も出てこない) — ライブラリを
  #     ホスト非依存に保ったこと自体が検査項目。srava 側の橋 (pigBreak → 述語) は
  #     end-to-end で確かめてある (SIGINT で、走り切るのを待つ場合の数百分の 1 の時間で畳まれる)。
  # ★★ 2026-09-12 (統合・Linux): 並行部分は **別 TU**。libstdc++ の C++20 版 <atomic> が
  #   <unistd.h> を引き、その ::pipe(int[2]) が vendor の namespace pipe と衝突するため
  #   (include の順では直らない・macOS の libc++ では起きない)。理由の全文は
  #   test/pipeprox_break_thread.h の冒頭。
  add_executable(test_pipeprox_break test/pipeprox_break_test.cpp
                                     test/pipeprox_break_thread.cpp)
  target_include_directories(test_pipeprox_break PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
  target_link_libraries(test_pipeprox_break PRIVATE pipeprox Threads::Threads)
  add_test(NAME pipeprox_break COMMAND test_pipeprox_break)
  set_tests_properties(pipeprox_break PROPERTIES
    PASS_REGULAR_EXPRESSION "PP-BREAK-OK"
    FAIL_REGULAR_EXPRESSION "FAIL|PP-BREAK-FAIL|Segmentation|Aborted"
    TIMEOUT 300)

  add_test(NAME srava_pipeprox_vendored_test COMMAND pipeprox_test_pipe)
  set_tests_properties(srava_pipeprox_vendored_test PROPERTIES
    FAIL_REGULAR_EXPRESSION "FAIL:" TIMEOUT 180)

  # ---- pipe_proximity.so (in-proc/process 両対応の .so モジュール・docs §5)。plugin を kernel と
  #      同じ .so 機構に載せ、exec_default=THREAD のとき planner 内 thread (ptsMediatorInternal) で、
  #      exec_default=process (opt-out) のとき srava_agent が dlopen して別プロセスで走らせる。
  #      ★ Plan A (2026-08-10): 旧 process 専用バイナリ pipe_proximity_agent + .plugin manifest +
  #      pigfPluginAgent/pigPluginRegistry は廃止し、demo.so と同じ generic 経路に一本化した。
  #      ppatsAgent (ptsAgent 派生) と ppaCompute (ptsCalcBody 派生) を tscpp2 codegen。pipeprox を
  #      bundle・pig/pts/tinyState は host 供給 (undefined のままビルド・manifold.so と同方針)。----
  tinystate_codegen(pp_ts_codegen
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/pipe_proximity/ppatsAgent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/pipe_proximity/ppaCompute.cpp)
  # 外部依存 pipeprox は **取り込みソースからの静的リンク** (規約 A・docs §7.3)。
  srava_add_module(pipe_proximity_module
    OUTPUT   pipe_proximity
    SOURCES  modules/pipe_proximity/ppatsAgent.cpp
             modules/pipe_proximity/ppaCompute.cpp
             modules/pipe_proximity/pipe_proximity_compute.cpp
             modules/pipe_proximity/pipe_proximity_adapter.cpp
             modules/pipe_proximity/manifest.cpp
    DEPENDS  pigts_codegen pp_ts_codegen
    LINK     pipeprox
    INCLUDES modules/pipe_proximity
    INSTALL)

  # in-proc 実行体 (ppatsAgent/ppaCompute) が使う host インフラ (ptsCalcBody) は .so に bundle されず
  # planner が供給する。★2026-08-10: ptsCalcBody は srava 定義直後に **無条件で**リンクするようにした
  #   (どのカーネル集合でも planner が全 .so を dlopen できるように) ので、ここでの追加は不要になった。

  # pipe_proximity.so が dlopen でき、記述子 (make_agent 付き) が読めることを検証 (RTLD_NOW 全解決)。
  add_test(NAME srava_module_pipeprox_so
    COMMAND srava_module_probe $<TARGET_FILE:pipe_proximity_module>)
  set_tests_properties(srava_module_pipeprox_so PROPERTIES
    PASS_REGULAR_EXPRESSION "MODULE_OK name=pipe_proximity .*make_agent=yes")

  # ★ Plan A: 旧 srava_plugin_pipeprox (process 専用バイナリ経路のテスト) は撤去。in-proc は
  #   srava_plugin_pipeprox_inproc、process(srava_agent 経由)は srava_plugin_pipeprox_process が覆う。

  # ★ .so 化 Phase 5: in-proc 実行の証明。manifest の bin も SRAVA_AGENT も偽にして完走する
  #   = pipe_proximity.so を planner 内 thread (ppatsAgent) で実行し、プロセスを一切 spawn していない。
  add_test(NAME srava_plugin_pipeprox_inproc
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_pipeprox_inproc)
  set_tests_properties(srava_plugin_pipeprox_inproc PROPERTIES
    ENVIRONMENT "SRAVA_CACHE_DIR=/tmp/srava-pipeinproc"
    PASS_REGULAR_EXPRESSION "PIPEINPROC_OK"
    FAIL_REGULAR_EXPRESSION "PIPEINPROC_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly"
    TIMEOUT 60)

  # ★ .so 化 Phase 5 (§5.4): process 版の存続。agent(so,{exec_default:"process"}) で in-proc を
  #   opt-out し、実 bin を別プロセス起動する経路が .so ロード後も生きていることを確認。
  add_test(NAME srava_plugin_pipeprox_process
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_pipeprox_process)
  set_tests_properties(srava_plugin_pipeprox_process PROPERTIES
    ENVIRONMENT "SRAVA_CACHE_DIR=/tmp/srava-pipeproc;SRAVA_AGENT=$<TARGET_FILE:srava_agent>"
    PASS_REGULAR_EXPRESSION "PIPEPROC_OK"
    FAIL_REGULAR_EXPRESSION "PIPEPROC_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 60)

  # 統合テスト(同一 bin の 2 つ目の op): 距離調整コントローラ pipe_adjust が違反を解消するか。
  add_test(NAME srava_plugin_pipeadjust
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_pipeadjust)
  set_tests_properties(srava_plugin_pipeadjust PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pipeadjust"
    PASS_REGULAR_EXPRESSION "PIPEADJUST_OK"
    FAIL_REGULAR_EXPRESSION "PIPEADJUST_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 60)

  # ★ #3408 回帰: fixed 指定の制御点が外力(fZ)下でも動かないこと。真因は polishScene の貪欲拡張が
  #   blocked(固定 DOF・硬ピン)を跨いで relaxSpan していたこと。修正は develop-v1 の faf03c7 を
  #   modules/ 構造へ移植したもの (bench は plugins/ 廃止後に分岐していたため渡っていなかった)。
  # ★硬ピンの joint 範囲検査 (2026-08-13)。範囲外は以前ヒープを壊していた (in-proc=corrupted
  #   double-linked list / process=agent 死)。今は ①ライブラリ側で弾き ②srava が明示エラーを返す。
  #   範囲内の硬ピンが従来どおり動くことも併せて見る (弾きすぎの検出)。
  add_test(NAME srava_pipeprox_pin_range
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> pipeprox_pin_range)
  set_tests_properties(srava_pipeprox_pin_range PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pinrange"
    PASS_REGULAR_EXPRESSION "PINRANGE-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|unknown op|parse error"
    TIMEOUT 60)

  add_test(NAME srava_pipeprox_fixed_force
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> pipeprox_fixed_force)
  set_tests_properties(srava_pipeprox_fixed_force PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib;SRAVA_CACHE_DIR=/tmp/srava-pipefixed;SRAVA_HANG_SECS=600"
    PASS_REGULAR_EXPRESSION "PIPEFIXED_OK"
    FAIL_REGULAR_EXPRESSION "PIPEFIXED_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 900)

  # wSpace(制御点間隔の均一化・上流 v0.1.7 の項)の配線回帰。既定 0=従来一致・指定時に効くこと。
  add_test(NAME srava_pipeprox_wspace
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> pipeprox_wspace)
  set_tests_properties(srava_pipeprox_wspace PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pipewspace;SRAVA_HANG_SECS=240"
    PASS_REGULAR_EXPRESSION "PIPEWSPACE_OK"
    FAIL_REGULAR_EXPRESSION "PIPEWSPACE_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 300)

  # 統合テスト: 半径プロファイル([s,r] キーポイント線形補間)がスカラ一定と整合するか。
  add_test(NAME srava_plugin_radius
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_radius)
  set_tests_properties(srava_plugin_radius PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-piperadius"
    PASS_REGULAR_EXPRESSION "PIPERADIUS_OK"
    FAIL_REGULAR_EXPRESSION "PIPERADIUS_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 60)

  # 統合テスト(N 体): scene 近接検出 + adjustScene が固定障害物を避けて調整するか。
  add_test(NAME srava_plugin_scene
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_scene)
  set_tests_properties(srava_plugin_scene PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pipescene"
    PASS_REGULAR_EXPRESSION "PIPESCENE_OK"
    FAIL_REGULAR_EXPRESSION "PIPESCENE_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 60)

  # 統合テスト: pipe_sample(弧長等間隔サンプル)がテーパ半径と一定ピッチを返すか。
  add_test(NAME srava_plugin_sample
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_sample)
  set_tests_properties(srava_plugin_sample PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pipesample"
    PASS_REGULAR_EXPRESSION "PIPESAMPLE_OK"
    FAIL_REGULAR_EXPRESSION "PIPESAMPLE_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 60)

  # 統合テスト: 射影的分離パスが、接触する 2 周コイル(ピッチ≈2r)を gap>=dMin へ押し広げるか。
  add_test(NAME srava_plugin_separate
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_separate)
  # ⚠ 番犬の予算 — **最も遅い機**の実測 x3 (2026-09-22)。mac(arm64/OCCT 7.9.3) 7.20s ・ Linux(x86_64/7.8.1) 1.42s ⇒ 30s。
  set_tests_properties(srava_plugin_separate PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib;SRAVA_CACHE_DIR=/tmp/srava-pipeseparate;SRAVA_HANG_SECS=30"
    PASS_REGULAR_EXPRESSION "PIPESEPARATE_OK"
    FAIL_REGULAR_EXPRESSION "PIPESEPARATE_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 60)

  # ★ Plan A: install は pipe_proximity.so を探索路 (lib/srava/modules) へ置く方式へ (kernel と同じ)。
  #   旧 process バイナリ + .plugin manifest 自動生成は廃止。
  #   ⚠ install 規則をここに**書かない** — srava_add_module(... INSTALL) が既に同じ宛先へ出している。
  #     二重に書くと install ログに "Installing" と "Up-to-date" が 2 行出る (冪等なので実害はないが、
  #     ログを数えて検算するときに 1 本ぶんずれる)。2026-09-12 に mac から指摘されて撤去した。
endif()

# ------------------------------------------------------------------
# 第2幾何カーネル: Manifold(github.com/elalish/manifold・Apache-2.0・#3404)。
#   ON で Manifold を FetchContent(v3.5.2 pin)取得。op 内並列 (MANIFOLD_PAR) は **既定 ON**
#   (#3419 / #3450)。かつては「並列は srava のプロセス fan-out に寄せる」方針だった(#3404)。
#   まずは疎通 mf_smoke(box∖sphere 1回)。通れば mfatsAgent(cgatsAgent ミラー)へ。
# ------------------------------------------------------------------
# option(SRAVA_MODULE_MANIFOLD) と旧名の後方互換はファイル冒頭 (§option 集約) で宣言済み。
if(SRAVA_MODULE_MANIFOLD)
  include(FetchContent)
  set(MANIFOLD_TEST   OFF CACHE BOOL "" FORCE)
  # ★ #3419: **既定は ON** (op 内 TBB 並列)。-DSRAVA_MANIFOLD_PAR=OFF でシリアルに戻せる。
  #   ⚠ 旧コメントは「並列は srava の fan-out で稼ぐ」としていたが、op 間だけでは足りず
  #     op 内と併せたほうが速いと実測で分かったので、その前提は否定された。
  #     数字は Redmine #3419 / #3450 (ソースには置かない・ひさ裁定 2026-08-27)。
  if(SRAVA_MANIFOLD_PAR)
    # ★ TBB は system (規約 C)。CGAL 同梱の FindTBB が MODULE モードで先に拾われるのを
    #   避けるため **CONFIG モードを明示**する (あの Find は oneTBB 2021+ で消えた
    #   tbb/tbb_stddef.h を探すので当てにならない)。
    # ★ 既定 ON にした以上、TBB が無い機械では「なぜ要るのか・どう外すのか」が分からないまま
    #   configure が落ちる。REQUIRED の素のエラーは理由を言わないので、自前で打ち手を出す。
    find_package(TBB CONFIG QUIET)
    if(NOT TBB_FOUND)
      message(FATAL_ERROR
              "srava: Manifold の op 内並列 (SRAVA_MANIFOLD_PAR・既定 ON) には system TBB が要りますが "
              "見つかりませんでした。TBB を入れる (Debian/Ubuntu: libtbb-dev) か、"
              "-DSRAVA_MANIFOLD_PAR=OFF でシリアルの Manifold を建ててください。")
    endif()
    set(MANIFOLD_PAR ON CACHE BOOL "" FORCE)
    set(MANIFOLD_USE_BUILTIN_TBB OFF CACHE BOOL "" FORCE)   # 同梱 TBB を建てない = system を使う
    message(STATUS "srava: Manifold intra-op parallelism = ON (system TBB ${TBB_VERSION})")
  else()
    set(MANIFOLD_PAR  OFF CACHE BOOL "" FORCE)   # シリアル (従来どおり)
  endif()
  set(MANIFOLD_CBIND  OFF CACHE BOOL "" FORCE)   # C++ API を直利用(C FFI 不要)
  set(MANIFOLD_PYBIND OFF CACHE BOOL "" FORCE)
  # ★ Manifold を **静的リンク**(自己完結の installed バイナリに。共有だと libmanifold.so.3 を
  #    RPATH で辿れず「agent closed before handshake」で死ぬ)。pig/tinyState と同じ方針。
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  # ★ EXCLUDE_FROM_ALL (CMake 3.28+): 依存 (manifold と、その下の clipper2) の **install ルールを無視**する。
  #   FetchContent_MakeAvailable は内部で add_subdirectory するので、これが無いと manifold 側の install()
  #   が srava の install に相乗りし、libmanifold.a(58MB) / include/manifold/ / lib/cmake/manifold/ /
  #   pkgconfig/manifold.pc まで $PREFIX に撒かれる。srava は manifold を **静的リンクして自己完結**して
  #   いるので実行にはこれらは不要で、しかもユーザが自前で入れた manifold を上書きして壊しうる。
  #   ビルドには影響しない (srava のターゲットが manifold::manifold に依存しているので、依存関係により
  #   通常どおりビルドされる = "inter-target dependencies supersede this exclusion")。
  FetchContent_Declare(manifold
    GIT_REPOSITORY https://github.com/elalish/manifold.git
    GIT_TAG v3.5.2
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(manifold)
  # ★ Cygwin 可搬性回避(ソースは fork しない): Cygwin の <ctype.h> は _S/_N/... をマクロ定義するため、
  #   manifold svd.h のローカル変数 `_S`(Symmetric3x3 _S)がマクロ 8 に置換され compile が壊れる。
  #   Cygwin ビルド時のみ svd.h の識別子 `_S` を rename する(冪等・他 OS は無関係。上流 Issue で戻す)。
  if(CYGWIN AND manifold_SOURCE_DIR)
    execute_process(COMMAND sed -i "s/\\b_S\\b/svdS_/g" "${manifold_SOURCE_DIR}/src/svd.h")
  endif()

  # mf 幾何ラッパ(mfMesh)+ 疎通テスト。pig(pigData)+ tinyState + Manifold をリンク(CGAL 非依存)。
  add_executable(mf_smoke
    src/main/mf_smoke_main.cpp
    modules/manifold/c++/mfMesh.cpp
    modules/manifold/c++/mfCross.cpp)
  target_link_libraries(mf_smoke PRIVATE manifold::manifold pig Threads::Threads m)

  # ---- srava-agent-mf: Manifold カーネルのエージェントプロセス(cgatsAgent 版のミラー・CGAL 非依存)----
  # mf の状態機械クラスを tscpp2 codegen(pig 層状態機械は pigts_codegen が生成済)。
  tinystate_codegen(mf_ts_codegen
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/ptsmfWireCacheStreamWriterMesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/ptsmfWireCacheStreamReaderMesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaBox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaPyramid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaCylinder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaCone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaTorus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaTetrahedron.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaSphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaIcosphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaUnion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaHull.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaRefine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaSimplifyCleanup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaMinkowski.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaIntersection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaDifference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaCast.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaPolygon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaPrism.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaRevolve.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaVolume.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaBbox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaTranslate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaRotate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaScale.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaMirror.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaTransform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaArea.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaNverts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaNfaces.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaCentroid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaImport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaRect.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaCircle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaEmpty2D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaEmpty3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaNgon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaExtrude.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaLoftRuled.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaCombine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaSection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaOffset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaProjectFlatten.cpp   # ★ #3534: z=0 への直投影
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaTube.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaColor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfatsAgent.cpp
  )
  # ---- .so 化 Phase 3c: srava_agent_mf バイナリは廃止 (単一 srava_agent + manifold.so に集約・
  #      docs §1.2)。manifold 幾何は下記 manifold.so から供給される。 ----

  # ---- .so 化 Phase 3b: manifold.so (幾何 + manifest)。manifold を bundle・pig/pts/tinyState は
  #      host 供給。ソースは srava_agent_mf の modules/manifold/c++ 部分と当面二重管理 (3c で統合)。----
  set(MF_KERNEL_SRC
    modules/manifold/c++/ptsmfWireCacheStreamWriterMesh.cpp
    modules/manifold/c++/ptsmfWireCacheStreamReaderMesh.cpp
    modules/manifold/c++/mfCacheCodec.cpp
    modules/manifold/c++/mfMesh.cpp
    modules/manifold/c++/mfCross.cpp
    modules/manifold/c++/mfaBox.cpp
    modules/manifold/c++/mfaPyramid.cpp
    modules/manifold/c++/mfaCylinder.cpp
    modules/manifold/c++/mfaCone.cpp
    modules/manifold/c++/mfaTorus.cpp
    modules/manifold/c++/mfaTetrahedron.cpp
    modules/manifold/c++/mfaSphere.cpp
    modules/manifold/c++/mfaIcosphere.cpp
    modules/manifold/c++/mfaUnion.cpp
    modules/manifold/c++/mfaHull.cpp
    modules/manifold/c++/mfaRefine.cpp
    modules/manifold/c++/mfaSimplifyCleanup.cpp
    modules/manifold/c++/mfaMinkowski.cpp
    modules/manifold/c++/mfaIntersection.cpp
    modules/manifold/c++/mfaDifference.cpp
    modules/manifold/c++/mfaExport.cpp
    modules/manifold/c++/mfaCast.cpp
    modules/manifold/c++/mfaPolygon.cpp
    modules/manifold/c++/mfaPrism.cpp
    modules/manifold/c++/mfaRevolve.cpp
    modules/manifold/c++/mfaVolume.cpp
    modules/manifold/c++/mfaBbox.cpp
    modules/manifold/c++/mfaTranslate.cpp
    modules/manifold/c++/mfaRotate.cpp
    modules/manifold/c++/mfaScale.cpp
    modules/manifold/c++/mfaMirror.cpp
    modules/manifold/c++/mfaTransform.cpp
    modules/manifold/c++/mfaArea.cpp
    modules/manifold/c++/mfaNverts.cpp
    modules/manifold/c++/mfaNfaces.cpp
    modules/manifold/c++/mfaCentroid.cpp
    modules/manifold/c++/mfaImport.cpp
    modules/manifold/c++/mfaRect.cpp
    modules/manifold/c++/mfaCircle.cpp
    modules/manifold/c++/mfaEmpty2D.cpp
    modules/manifold/c++/mfaEmpty3D.cpp
    modules/manifold/c++/mfaNgon.cpp
    modules/manifold/c++/mfaExtrude.cpp
    modules/manifold/c++/mfaLoftRuled.cpp
    modules/manifold/c++/mfaCombine.cpp
    modules/manifold/c++/mfaSection.cpp
    modules/manifold/c++/mfaOffset.cpp
    modules/manifold/c++/mfaProjectFlatten.cpp
    modules/manifold/c++/mfaTube.cpp
    modules/manifold/c++/mfaColor.cpp
    modules/manifold/c++/mfatsAgent.cpp
  )
  # 外部依存 Manifold は **FetchContent + 静的リンク** (規約 B・docs §7.3)。実行時の共有依存は増えない。
  # ★ #3434 (2026-08-22・案 B'): manifold の**幾何クラスを共有ライブラリへ**。
  #   理由: openvdb_mf.so が **本物の mfMesh** を受け取るため。各 .so がコピーを持つと
  #   in-proc で d_cast が失敗する (vdMesh で踏んだのと同じバグ)。
  #   ★ 境界は明快: mfa*.cpp = op (モジュール側) / それ以外 = 幾何クラスと codec (lib 側)。
  set(MF_GEOM_SRC
    modules/manifold/c++/ptsmfWireCacheStreamWriterMesh.cpp
    modules/manifold/c++/ptsmfWireCacheStreamReaderMesh.cpp
    modules/manifold/c++/mfCacheCodec.cpp
    modules/manifold/c++/mfMesh.cpp
    modules/manifold/c++/mfCross.cpp)
  add_library(srava_mf SHARED ${MF_GEOM_SRC})
  set_target_properties(srava_mf PROPERTIES OUTPUT_NAME "srava_mf" INSTALL_RPATH "$ORIGIN")
  srava_export_all(srava_mf)   # ★ Windows: 明示エクスポート
  target_link_libraries(srava_mf PUBLIC pig manifold::manifold)
  target_compile_definitions(srava_mf PRIVATE SRAVA_VALUE_ONLY)
  target_include_directories(srava_mf PUBLIC ${SRAVA_GEN})
  add_dependencies(srava_mf pigts_codegen mf_ts_codegen gen_srava_parser)
  install(TARGETS srava_mf LIBRARY DESTINATION lib RUNTIME DESTINATION bin)

  # ---- ★ #3498: Manifold の中断機構がライブラリ層で効くことの単体回帰。
  #   occt_break / openvdb_break の姉妹。manifold は中断の口が Cancel() (こちらから撃つ) なので、
  #   **pigBreak の押し出し口 (pigBreakHook) が別スレッドから撃てること**そのものが検査対象。
  #   ⚠ 「評価点を compute() へ移しても総仕事量は増えない」もここで見ている (項目 4)。
  add_executable(test_manifold_break test/manifold_break_test.cpp)
  target_link_libraries(test_manifold_break PRIVATE srava_mf manifold::manifold)
  target_include_directories(test_manifold_break PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/h)
  add_test(NAME manifold_break COMMAND test_manifold_break)
  set_tests_properties(manifold_break PROPERTIES
    PASS_REGULAR_EXPRESSION "MF-BREAK-OK"
    FAIL_REGULAR_EXPRESSION "FAIL|MF-BREAK-FAIL|Segmentation|Aborted"
    TIMEOUT 300)

  # op だけを .so に残す (幾何クラスは srava_mf 側)
  list(REMOVE_ITEM MF_KERNEL_SRC ${MF_GEOM_SRC})

  srava_add_module(manifold_module
    OUTPUT   manifold
    SOURCES  ${MF_KERNEL_SRC} modules/manifold/manifest.cpp
    DEPENDS  pigts_codegen mf_ts_codegen gen_srava_parser
    LINK     srava_mf manifold::manifold
    DEFINES  SRAVA_VALUE_ONLY
    INCLUDES ${SRAVA_GEN}
    INSTALL)

  # manifold.so が dlopen でき、記述子 (make_agent 付き) が読めることを検証。
  add_test(NAME srava_module_manifold_so
    COMMAND srava_module_probe $<TARGET_FILE:manifold_module>)
  set_tests_properties(srava_module_manifold_so PROPERTIES
    PASS_REGULAR_EXPRESSION "MODULE_OK name=manifold .*make_agent=yes")

  # ★ rev4 Phase A: manifold.so の manifest 静的初期化が型軸レジストリへ実装型を登録できること
  # (mf-mesh3d=MFM3 / mf-cross2d=MFC2)。
  add_test(NAME srava_type_manifold
    COMMAND srava_module_probe $<TARGET_FILE:manifold_module>)
  set_tests_properties(srava_type_manifold PROPERTIES
    PASS_REGULAR_EXPRESSION "TYPES mf-mesh3d,mf-cross2d")

  # ---- .so 化 Phase 3c-2: manifold の in-proc 実行を **静的リンクから dlopen へ**。planner は
  #      manifold を静的リンクせず、起動時に manifold.so を探索路からロードする (srava_main の
  #      pigModuleLoader::load_search_path)。mfatsAgent の静的初期化 (agent/記述子/codec 登録) は
  #      .so ロード時に走り、pigfModuleAgent が SRAVA_INPROC + thread 可能カーネルのとき
  #      ptsMediatorInternal で planner 内 thread 実行する。旧来ここに列挙していた mf 幾何 +
  #      Manifold リンクは撤去 (二重管理の解消)。
  #      ★ in-proc 実行体 (manifold.so 側) が使う **host インフラ** (ptsCalcBody / ref codec) は .so に
  #        bundle されず planner が供給する必要がある。★2026-08-10: これらは srava 定義直後に **無条件で**
  #        リンクするようにしたので、ここでの追加は不要になった (manifold OFF でも d2/d3 が dlopen 可能)。

  # ---- in-process 評価器の回帰テスト (#3406 4.3)。
  #   他の 176 テストは既定カーネル=CGAL なので、SRAVA_INPROC=1 を付けても agent_kernel_name() が
  #   thNULL を返して全部 External に落ちる = ptsMediatorInternal は一度も実行されない。
  #   ここだけ DEFAULT_OUTPUT=manifold を明示して Internal 経路を必ず踏ませ、External と同じ答えに
  #   なることを見る(詳細は test/srava_inproc.sh の冒頭コメント)。
  add_test(NAME srava_inproc_value
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_inproc.sh $<TARGET_FILE:srava> value)
  set_tests_properties(srava_inproc_value PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-inproc-val"
    PASS_REGULAR_EXPRESSION "INPROC-VALUE-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|ERROR"
    TIMEOUT 30)
  add_test(NAME srava_inproc_mesh
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_inproc.sh $<TARGET_FILE:srava> mesh)
  set_tests_properties(srava_inproc_mesh PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-inproc-mesh"
    PASS_REGULAR_EXPRESSION "INPROC-MESH-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|ERROR"
    TIMEOUT 30)
endif()

# ==================================================================
# ★ #3433 P1: nef.so — CGAL Nef_polyhedron_3 (SNC) を型 nf-mesh3d として供給するモジュール。
#   - **新規依存ゼロ**: Nef は CGAL 同梱 (cgal.so も offset/minkowski で既に使っている)。
#     よって SRAVA_MODULE_CGAL に従属し、CGAL が無いビルドでは作らない。
#   - priority=5 で既定カーネルにはならない (cgal=20 / manifold=10)。使うときは
#     module("nef.so",{priority:99}) で明示的に上げる。
#   - cache 形式は cg の "MESH" と同一フレーミング (厳密境界) なので、MESH → nf の昇格読みが
#     同じ decode 経路で成立する (modules/nef/h/nf/c++/nfMesh.h 冒頭の設計メモ参照)。
# ==================================================================
if(SRAVA_MODULE_NEF AND SRAVA_MODULE_CGAL)
  tinystate_codegen(nf_ts_codegen
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/ptsnfWireCacheStreamWriterMesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/ptsnfWireCacheStreamReaderMesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaBox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaPyramid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaCylinder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaCone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaTorus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaTetrahedron.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaPrism.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaIcosphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaImport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaEmpty3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaTube.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaSphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaUnion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaIntersection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaDifference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaComplement.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaMinkowski.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaHull.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaOffset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaConvexDecomposition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaNparts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaPart.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaUnify.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaSolidify.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaVolume.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaBbox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaCentroid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaArea.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaValid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaNverts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaNfaces.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaCast.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaTranslate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaRotate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaScale.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaMirror.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfaTransform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nftsAgent.cpp
  )
  set(NF_KERNEL_SRC
    modules/nef/c++/ptsnfWireCacheStreamWriterMesh.cpp
    modules/nef/c++/ptsnfWireCacheStreamReaderMesh.cpp
    modules/nef/c++/nfCacheCodec.cpp
    modules/nef/c++/nfMesh.cpp
    modules/nef/c++/nfaBox.cpp
    modules/nef/c++/nfaPyramid.cpp
    modules/nef/c++/nfaCylinder.cpp
    modules/nef/c++/nfaCone.cpp
    modules/nef/c++/nfaTorus.cpp
    modules/nef/c++/nfaTetrahedron.cpp
    modules/nef/c++/nfaPrism.cpp
    modules/nef/c++/nfaIcosphere.cpp
    modules/nef/c++/nfaImport.cpp
    modules/nef/c++/nfaEmpty3D.cpp
    modules/nef/c++/nfaTube.cpp
    modules/nef/c++/nfaSphere.cpp
    modules/nef/c++/nfaUnion.cpp
    modules/nef/c++/nfaIntersection.cpp
    modules/nef/c++/nfaDifference.cpp
    modules/nef/c++/nfaComplement.cpp
    modules/nef/c++/nfaMinkowski.cpp
    modules/nef/c++/nfaHull.cpp
    modules/nef/c++/nfaOffset.cpp
    modules/nef/c++/nfaConvexDecomposition.cpp
    modules/nef/c++/nfaNparts.cpp
    modules/nef/c++/nfaPart.cpp
    modules/nef/c++/nfaUnify.cpp
    modules/nef/c++/nfaSolidify.cpp
    modules/nef/c++/nfaVolume.cpp
    modules/nef/c++/nfaBbox.cpp
    modules/nef/c++/nfaCentroid.cpp
    modules/nef/c++/nfaArea.cpp
    modules/nef/c++/nfaValid.cpp
    modules/nef/c++/nfaNverts.cpp
    modules/nef/c++/nfaNfaces.cpp
    modules/nef/c++/nfaExport.cpp
    modules/nef/c++/nfaCast.cpp
    modules/nef/c++/nfaTranslate.cpp
    modules/nef/c++/nfaRotate.cpp
    modules/nef/c++/nfaScale.cpp
    modules/nef/c++/nfaMirror.cpp
    modules/nef/c++/nfaTransform.cpp
    modules/nef/c++/nftsAgent.cpp
  )
  # ★ #3433: 同一ソースから **2 モジュール**をビルドする (ひさ方針)。型名・4CC・module 名・
  #   hash_salt を分けてあるので同居できる (pigTypeRegistry は型名↔タグ 1:1 が不変条件)。
  #     nef_snc.so    : cache は常に SNC        型 nf-mesh3d  / 4CC NEF3
  #     nef_hybrid.so : 有界立体は厳密境界      型 nfb-mesh3d / 4CC NEFB
  #   どちらを使うかは module("nef_snc.so","on") / module("nef_hybrid.so","off") で切り替える
  #   (#3439 で off ≡ 未ロードになったので、off にしたモジュールの型は見えない)。
  # ★ 建てる変種は option で決まる (既定は hybrid のみ)。
  set(_nef_variants)
  if(SRAVA_MODULE_NEF_SNC)
    list(APPEND _nef_variants snc)
  endif()
  if(SRAVA_MODULE_NEF_HYBRID)
    list(APPEND _nef_variants hybrid)
  endif()
  # ★★ #3559 (2026-09-19・ひさ裁定): **幾何クラスは libsrava_cg に置く** — 1 本だけ。
  #   ---- なぜ移したか ----
  #   #3499 では「変種ごとに 1 本」(libsrava_nf_snc / libsrava_nf_hybrid) だった。4CC・型名・
  #   形式方針がコンパイル時マクロで焼き込まれていたので、1 本にするには変種を実行時の属性へ
  #   作り替える必要があったため。⇒ #3559 でそれをやった (nfMesh / nfMeshSnc の 2 型)。
  #   ⚠ 変種ごとに幾何ライブラリを建てると、**上流 (CGAL) の実体もその本数だけ増える**。
  #     CGAL は header-only なので可変大域 (get_default_random()::default_random 等) が
  #     各 .so に emit される。ELF は @u@ (STB_GNU_UNIQUE) をローダが 1 個に畳むので実害が
  #     出ないが、**PE には畳む機構が無い** (box で実測: 局所シンボル d で 3 個)。
  #   ⇒ 引き換えに #3440 の線 (cgal.so / manifold.so を CGAL Nef 非依存に保つ) を畳んだ。
  #     上流が corefinement と Nef をはっきり分けられない以上やむを得ない、というひさ判断。
  #   ★ nef_snc.so / nef_hybrid.so と橋 (nef_cg.so / nef_mf.so) は **同じ nfNefMesh の実体**を
  #     共有する必要がある (各 .so がコピーを持つと in-proc で d_cast が失敗する)。
  #   ⚠ 共有ライブラリ側は **HIDDEN にできない** (モジュールから見えないと意味がない)。
  #     モジュール .so の HIDDEN は従来どおり — 衝突するのは記述子など**モジュール側**の
  #     シンボルなので、そちらは引き続き隠す。幾何クラスは nfMesh.h で明示的に default 可視性。
  #   ⚠ **-D は付けない**。幾何ライブラリ側の TU は 2 型ともコンパイルする (変種で分岐しない)。
  set(NF_GEOM_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/ptsnfWireCacheStreamWriterMesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/ptsnfWireCacheStreamReaderMesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfMesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/c++/nfWire.cpp)
  # ★ #3559: 橋 (nef ⇄ cgal の変換) も同居させる。両側の幾何クラスが同じライブラリに在るので、
  #   別 .so (旧 libsrava_nfcg) を建てる理由が消えた。詳細 = modules/nef_cg/h/nfc/c++/nfcBridge.h
  if(SRAVA_MODULE_NEF_SNC)
    list(APPEND NF_GEOM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef_cg/c++/nfcBridge.cpp)
  endif()
  target_sources(srava_cg PRIVATE ${NF_GEOM_SRC})
  target_include_directories(srava_cg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef_cg/h)
  add_dependencies(srava_cg nf_ts_codegen)
  # ★ nfCacheCodec.cpp (descriptor.codecs の表) は **モジュール側に残る** — 中身が変種ごとに
  #   違う (自分の型名 1 本) ため。WIRE の定義と reader/writer の生成子だけが nfWire.cpp。
  set(NF_OPS_SRC ${NF_KERNEL_SRC})
  list(REMOVE_ITEM NF_OPS_SRC
    modules/nef/c++/ptsnfWireCacheStreamWriterMesh.cpp
    modules/nef/c++/ptsnfWireCacheStreamReaderMesh.cpp
    modules/nef/c++/nfMesh.cpp)

  foreach(_v ${_nef_variants})
    if(_v STREQUAL snc)
      set(_def NF_WIRE_SNC)
    else()
      set(_def NF_WIRE_HYBRID)
    endif()
    # 外部依存 CGAL は **システムから検出して共有リンク** (規約 C・docs §7.3)。
    # ★ #3559: 幾何クラスは srava_cg から借りる (自分では持たない)。
    srava_add_module(nef_${_v}
      OUTPUT   nef_${_v}
      SOURCES  ${NF_OPS_SRC} modules/nef/manifest.cpp
      DEPENDS  pigts_codegen nf_ts_codegen gen_srava_parser
      LINK     srava_cg CGAL::CGAL
      DEFINES  SRAVA_VALUE_ONLY ${_def}
      INCLUDES ${SRAVA_GEN}
      # ★ HIDDEN: 2 変種は**同一ソース**なので、既定の可視性だと nftsAgent_descriptor などの
      #   グローバルシンボル 200 本超が衝突する。ローダは RTLD_GLOBAL で dlopen するため、
      #   後から読まれた .so の manifest が **先に読まれた方の記述子**を返し、1 つしか
      #   登録されない (実際に踏んだ)。公開点 srava_module() だけ export する (プラグインの定石)。
      HIDDEN
      INSTALL)

    add_test(NAME srava_module_nef_${_v}_so COMMAND srava_module_probe $<TARGET_FILE:nef_${_v}>)
    set_tests_properties(srava_module_nef_${_v}_so PROPERTIES
      PASS_REGULAR_EXPRESSION "MODULE_OK name=nef_${_v} .*make_agent=yes")
  endforeach()
  # 型申告 (型名は変種ごとに違う = 同居できる根拠)。
  if(SRAVA_MODULE_NEF_SNC)
    add_test(NAME srava_type_nef_snc    COMMAND srava_module_probe $<TARGET_FILE:nef_snc>)
    set_tests_properties(srava_type_nef_snc PROPERTIES PASS_REGULAR_EXPRESSION "TYPES nf-mesh3d")
  endif()
  if(SRAVA_MODULE_NEF_HYBRID)
    add_test(NAME srava_type_nef_hybrid COMMAND srava_module_probe $<TARGET_FILE:nef_hybrid>)
    set_tests_properties(srava_type_nef_hybrid PROPERTIES PASS_REGULAR_EXPRESSION "TYPES nfb-mesh3d")
  endif()

  # ==================================================================
  # ★ #3499: **nef_snc ⇄ 他カーネルの境界モジュール** (nef_cg.so / nef_mf.so)。
  #   nef_snc は「常に SNC だけ」を書く (Nef 本来の表現) ので、cgal.so / manifold.so は
  #   その cache を読めない — SNC のパースには CGAL Nef が要り、どちらもそれに依存しない
  #   方針を持っているため (#3440 / GPL 非汚染)。#3478 は nef_snc 側に厳密境界を付録として
  #   併記させて解いたが、**encode ごとに to_mesh() が走る**代償が大きかった。
  #   ⇒ 変換だけを別 .so に切り出し、**cast が呼ばれたときだけ**払う (occt_mf と同じ構図)。
  #   ★ 幾何クラスは libsrava_cg / libsrava_mf にあるので、ここには実体を
  #     持たない (持つと in-proc で d_cast が失敗する)。
  #   ★ hybrid には橋を建てない — あちらは普通の立体を厳密境界で書くので直接読める。
  if(SRAVA_MODULE_NEF_SNC)
    tinystate_codegen(nfc_ts_codegen
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef_cg/c++/nfcCast.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef_cg/c++/nfctsAgent.cpp)
    # ★★ #3559: **橋の変換は libsrava_cg に同居する** (上の NF_GEOM_SRC へ足してある)。
    #   #3545 段 4 の時点では、変換が要る nfMesh と cgMesh3D がそれぞれ別の幾何ライブラリに
    #   居たので「どちらにも置けない ⇒ 橋が自分の .so (libsrava_nfcg) を持つ」形だった。
    #   #3559 で 2 つが同じ libsrava_cg に入ったので、その .so は要らなくなった。
    #   ★ 橋の op TU (nfcCast.cpp) が CGAL を 1 枚も引かない形は **そのまま**
    #     (検査 = test/srava_op_cgal_free.sh)。
    srava_add_module(nef_cg_module
      OUTPUT   nef_cg
      SOURCES  modules/nef_cg/c++/nfcCacheCodec.cpp
               modules/nef_cg/c++/nfcCast.cpp
               modules/nef_cg/c++/nfctsAgent.cpp
               modules/nef_cg/manifest.cpp
      DEPENDS  pigts_codegen nf_ts_codegen nfc_ts_codegen gen_srava_parser
      # ★ #3545 段 4: 変換は幾何ライブラリ (srava_cg) が持つ。⚠ CGAL::CGAL を **外した** —
      #   op はもう CGAL を呼ばないので、リンク行に残すと「引いてよい」という誤った申告になる。
      LINK     srava_cg
      DEFINES  SRAVA_VALUE_ONLY NF_WIRE_SNC
      INCLUDES ${SRAVA_GEN}
               ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/h
               ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/h
               ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef_cg/h
      INSTALL)
    add_test(NAME srava_module_nef_cg_so COMMAND srava_module_probe $<TARGET_FILE:nef_cg_module>)
    set_tests_properties(srava_module_nef_cg_so PROPERTIES
      PASS_REGULAR_EXPRESSION "MODULE_OK name=nef_cg .*make_agent=yes")

    # ★ manifold が無いビルドでは mf 側の橋だけ落ちる (cg 側は建つ)。
    if(SRAVA_MODULE_MANIFOLD)
      tinystate_codegen(nfm_ts_codegen
        ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef_mf/c++/nfmCast.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef_mf/c++/nfmtsAgent.cpp)
      srava_add_module(nef_mf_module
        OUTPUT   nef_mf
        SOURCES  modules/nef_mf/c++/nfmCacheCodec.cpp
                 modules/nef_mf/c++/nfmCast.cpp
                 modules/nef_mf/c++/nfmtsAgent.cpp
                 modules/nef_mf/manifest.cpp
        DEPENDS  pigts_codegen nf_ts_codegen mf_ts_codegen nfm_ts_codegen gen_srava_parser
        # ★ #3559: nfMeshSnc は srava_cg に在る (旧 srava_nf_snc)。
        LINK     srava_cg srava_mf CGAL::CGAL
        DEFINES  SRAVA_VALUE_ONLY NF_WIRE_SNC
        INCLUDES ${SRAVA_GEN}
                 ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef/h
                 ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/h
                 ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/h
                 ${CMAKE_CURRENT_SOURCE_DIR}/modules/nef_mf/h
        INSTALL)
      add_test(NAME srava_module_nef_mf_so COMMAND srava_module_probe $<TARGET_FILE:nef_mf_module>)
      set_tests_properties(srava_module_nef_mf_so PROPERTIES
        PASS_REGULAR_EXPRESSION "MODULE_OK name=nef_mf .*make_agent=yes")
    endif()
  endif()

  # ---- nef の振る舞い回帰 (test/srava_nef.sh・各モードの意図はスクリプト冒頭に記載)。
  #   ★**両変種で同じ 6 モードを回す**。挙動が変わるのは nf → mf の直接変換だけで、そこは
  #     スクリプトが変種を見て両方の正しさを固定する (hybrid=有界なら通る / snc=常に不可)。
  #   agree     : Nef(SNC) と cgal(corefinement) の厳密解が一致する
  #   cross     : cg の "MESH" を nf へ昇格読みできる
  #   unbounded : 非有界が cache を渡って生き残る + 非有界の volume は明示エラー
  #   downgrade : nf → cg。表現できる立体は変換し、非有界は明示エラー
  #   mfcross   : mf ⇄ nf (昇格読み・混成ブール・nf→mf の変種別の成否)
  #   chain     : ★Nef 型を維持したまま op 連鎖する (他形式への往復が 0 件)
  #   minkowski : ★Minkowski 和 (#3440・offset のプリミティブ)。箱⊕箱=箱 (体積 27) /
  #               cg 入力の all-foreign routing / 非有界は明示エラー
  #   offset    : ★3D offset (#3440 の 2 で cgal.so から移設)。膨張/収縮/中空箱/非有界エラー
  #   cavity    : ★内部空洞つき立体が cache を渡っても壊れない (hybrid 境界形式のバグ回帰)
  #   nonmani   : ★2-多様体でない有界の値も cg / mf へ出せる (2026-09-06)。空洞を落とさない
  #               (to_mesh が全シェルを回す回帰) / 非有界は理由つきで不可 (負の対照)
  #   convex    : ★凸分解 (#3441)。nparts で数え part(d,i) で片を取り出す (体積の合計が元と一致) /
  #               内壁で cg 降格不可 / 範囲外は明示エラー
  #   unify     : ★内壁除去 (#3442・正則化)。単一は不変 / 空洞は保つ / 分解の内壁を消せる
  #   selfx     : ★自己交差メッシュは明示エラー・agent が落ちないこと (CGAL assertion のガード)
  #   solidify  : ★壊れた境界からのソリッド再構成 (#3445)。自己交差 tube 52.02 → 48.61 /
  #               健全な立体は不変 / 空洞が埋まらない (深さ合成の回帰)
  # ★ ggcross は geogram モジュールが要る (既定 OFF)。ビルドしたときだけ登録する。
  set(_NEF_MODES agree cross unbounded downgrade mfcross chain minkowski offset subdiv cavity convex unify selfx solidify nonmani)
  if(SRAVA_MODULE_GEOGRAM)
    list(APPEND _NEF_MODES ggcross)
  endif()
  foreach(_v ${_nef_variants})
    foreach(_m ${_NEF_MODES})
      add_test(NAME srava_nef_${_v}_${_m}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_nef.sh $<TARGET_FILE:srava> ${_m} ${_v})
      # ⚠ SRAVA_HANG_SECS — subdiv が **機で 12 倍違う** (Linux 3.6s / mac 43.6s ・ 2026-09-22 実測)。
      #   既定 10 秒だと mac で毎回撮られ、**緑のまま捕獲だけ溜まる** (mac で 222 件溜まっていた)。
      set_tests_properties(srava_nef_${_v}_${_m} PROPERTIES
        ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/nefcache-${_v}-${_m};SRAVA_HANG_SECS=120"
        PASS_REGULAR_EXPRESSION "NEF-[A-Z]+-OK"
        FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
        TIMEOUT 300)
    endforeach()
  endforeach()
endif()

# ==================================================================
# ★ #3435 P3: geogram (Bruno Lévy・BSD-3) — 「厳密のまま CGAL より速い」を §6 の実データにする。
#   外部依存の取り込みは **規約 B** (FetchContent + 静的リンク・docs/srava_module_design.md §7.3):
#     - distro パッケージが存在しない → システム検出 (規約 C) は採れない
#     - 上流を tag で pin できて改修の予定も無い → vendoring (規約 A) までは要らない
#   ★ EXCLUDE_FROM_ALL 必須 (規約 B): これが無いと geogram 側の install() が srava の install に
#     相乗りして、ユーザが自前で入れた geogram を壊しうる。
#   ★★ ライセンス: geogram 本体は BSD-3 だが、**同梱の TetGen (AGPL) と Triangle (非商用限定) は
#     既定 ON** なので、必ず OFF にする。srava は GPLv3 なので AGPL を引き込むと配布条件が変わる。
# ==================================================================
if(SRAVA_MODULE_GEOGRAM)
  include(FetchContent)
  set(GEOGRAM_LIB_ONLY            ON  CACHE BOOL "" FORCE)   # 例題/ビューアを作らない
  set(GEOGRAM_WITH_GRAPHICS       OFF CACHE BOOL "" FORCE)   # OpenGL/GLFW を引かない
  set(GEOGRAM_WITH_LUA            OFF CACHE BOOL "" FORCE)
  set(GEOGRAM_WITH_HLBFGS         OFF CACHE BOOL "" FORCE)
  set(GEOGRAM_WITH_TETGEN         OFF CACHE BOOL "" FORCE)   # ★AGPL
  set(GEOGRAM_WITH_TRIANGLE       OFF CACHE BOOL "" FORCE)   # ★非商用限定ライセンス
  set(GEOGRAM_WITH_LEGACY_NUMERICS OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS           OFF CACHE BOOL "" FORCE)   # 静的リンク (規約 B)
  # ★★ #3535 (ひさ判断 2026-09-15): geogram は **共有ライブラリ**として建てる。
  #   ⚠⚠ 以前は静的 (.a) にしていたが、静的アーカイブは *参照されたオブジェクトだけ* を引くので
  #     libsrava_gg と geogram.so が **それぞれ自分の分を抱え込み**、GEO:: の file-scope な
  #     可変状態 (CmdLine の変数表・Process のスレッド数) が **.so ごとに別物**になっていた。
  #     ⇒ 初期化したコピーと使うコピーが食い違って SIGSEGV (2026-09-14・Linux 実測)。
  #   ★ 共有にすれば **実体がプロセスに 1 つ**になり、この種の絡まりが *構造的に起きえない*。
  #     可視性をいじる必要も「借りる」配線も要らない — 上流は @c GEOGRAM_API で公開点を
  #     自分で注釈しており (GEO_DYNAMIC_LIBS 時に visibility("default") になる)、
  #     **-dynamic 版が geogram 自身の既定**でもある。
  #   ⇒ 規約 B (FetchContent + 静的) ではなく docs §7.3 末尾の
  #     「モジュールが私物の共有ライブラリを持ち込む場合は $PREFIX/lib へ install する」に従う。
  #   ⚠ 代償: 配布物が 1 本増える。install 規則を書き忘れると
  #     「ビルドツリーでは動くが install したら agent が起動しない」になる (§7.3 の警告どおり)。
  # ★★ MinGW は上流の dynamic 版が使えない (2026-09-16 に box で実測・#3535 の MinGW 分)。
  #   ① `Win-vs-dynamic-generic/config.cmake` は `if(CMAKE_CL_64)` で分岐する。これは **MSVC 専用
  #      変数**なので MinGW では未定義 = 偽 ⇒ **32bit ブランチ**へ落ち、さらに MSVC 用の
  #      `Windows-vs.cmake` を読む。「未検証」ではなく **当てると壊れる**。
  #   ② 上流に **Windows/gcc 向けの dynamic 版が無い** (dynamic 版は Darwin-* / Linux64-* と
  #      Win-vs-dynamic-generic の 9 つだけ。Win64-gcc と MinGW-x86_64-w64 は静的のみ)。
  #   ③ ⚠⚠ 仮に dynamic で建てても `GEOGRAM_API` が効かない。geogram の `api/defs.h` は
  #        `GEO_DYNAMIC_LIBS` 下で `_MSC_VER` なら `__declspec(dllexport)`、`__GNUC__` なら
  #        `__attribute__((visibility("default")))` = **ELF の作法**。**MinGW は GNUC かつ PE**
  #        なので後者に落ち、**dllexport が一切付かない** (`GEO_IMPORT` も空)。
  #        ⇒ 上流は「MSVC か、さもなくば ELF」前提で、**MinGW = GNUC + PE が想定外**。
  #        ★ 対照: 同じ木の OpenVDB `Platform.h` は `#ifdef _WIN32` を**先に**見るので MinGW でも正しい。
  #          **コンパイラ (`__GNUC__`) はバイナリ形式を判定できない。**
  #   ⇒ MinGW では **静的のまま**にする。共有化 (#3535) の効能 (実体を 1 つに寄せる) は
  #     MinGW では別手段で達成する (#3545 の形 = 上流を include する .so を 1 つに限る)。
  set(_srava_gg_dynamic ON)
  if(APPLE)
    set(VORPALINE_PLATFORM "Darwin-clang-dynamic"  CACHE STRING "" FORCE)
  elseif(MSVC)
    set(VORPALINE_PLATFORM "Win-vs-dynamic-generic" CACHE STRING "" FORCE)   # 本来の用途はここだけ
  elseif(WIN32)
    set(VORPALINE_PLATFORM "Win64-gcc"             CACHE STRING "" FORCE)   # ★ MinGW: 静的のみ
    set(_srava_gg_dynamic OFF)
  else()
    set(VORPALINE_PLATFORM "Linux64-gcc-dynamic"   CACHE STRING "" FORCE)
  endif()
  set(VORPALINE_BUILD_DYNAMIC     ${_srava_gg_dynamic} CACHE BOOL "" FORCE)
  # ★ 静的 .a を **モジュール (.so)** へリンクするので PIC が要る (非 PIC だとリロケーションエラー)。
  #   geogram は dynamic 版の分岐でしか POSITION_INDEPENDENT_CODE を立てないので、こちらで立てる
  #   (pipeprox の静的ライブラリと同じ扱い)。MakeAvailable の前に置くと配下の全ターゲットに効く。
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  FetchContent_Declare(geogram
    GIT_REPOSITORY https://github.com/BrunoLevy/geogram.git
    GIT_TAG v1.10.0
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(geogram)

  # ★ Cygwin 対応 (2026-08-27): geogram は Cygwin を OS として認識しない。2 箇所とも
  #   「Cygwin = POSIX API を持つが Linux ではなく、_WIN32 でもない」の取りこぼしで、
  #   上流が既に持っている除外リストに Cygwin を足すだけで通る。
  #     ① basic/common.h — OS 判定が __linux__ / _WIN32 / __APPLE__ / __EMSCRIPTEN__ しか
  #        見ておらず、どれにも当たらず #error "Unsupported operating system" になる。
  #        Cygwin は POSIX 側なので __linux__ の枝に載せる。
  #     ② basic/process_unix.cpp — feenableexcept/fedisableexcept は glibc 拡張で
  #        Cygwin(newlib) に無い。上流は同じ理由で MUSL を除外済みなので、そこへ足す。
  #   ⚠ 上流の木を書き換えるので **冪等な sed** にしてある (適用後はパターンに一致しない)。
  #   ⚠ この 2 つが無いと geogram のビルドが通らないため、OFF にする以外の道は無い。
  if(CYGWIN)
    execute_process(COMMAND sed -i
      "s/^#if defined(__linux__)$/#if defined(__linux__) || defined(__CYGWIN__)/"
      "${geogram_SOURCE_DIR}/src/lib/geogram/basic/common.h")
    execute_process(COMMAND sed -i
      "s/^#if defined(GEO_OS_APPLE) || defined(GEO_OS_EMSCRIPTEN) || defined(__MUSL__)$/#if defined(GEO_OS_APPLE) || defined(GEO_OS_EMSCRIPTEN) || defined(__MUSL__) || defined(__CYGWIN__)/"
      "${geogram_SOURCE_DIR}/src/lib/geogram/basic/process_unix.cpp")
    message(STATUS "geogram: Cygwin 対応パッチを適用した (OS 判定 + FPE 制御)")
  endif()

  # ★★ #3535: **install 後も @rpath で解決させる** (mac)。
  #   ⚠⚠ geogram 自身の CMakeLists が @c set(CMAKE_INSTALL_NAME_DIR "${CMAKE_INSTALL_PREFIX}/lib")
  #     をしているため、install するとき dylib の install name が **絶対パス**
  #     (/usr/local/lib/libgeogram.…dylib) に書き換えられる。ビルドツリーでは @rpath なので通り、
  #     **install したときだけ** dlopen が "Library not loaded" で落ちる。
  #     ⇒ ctest srava_install_tree が実際にこれを捕まえた (2026-09-15)。§7.3 が警告している
  #       「ビルドツリーでは動くが install したら agent が起動しない」そのもの。
  #   ★ @rpath にしておけば、srava_gg の INSTALL_RPATH ($ORIGIN) と モジュールの
  #     $ORIGIN/../.. がどちらも $PREFIX/lib を指すので解決する。
  if(APPLE AND TARGET geogram)
    set_target_properties(geogram PROPERTIES INSTALL_NAME_DIR "@rpath")
  endif()

  # ★ #3535①の「default 可視性で建てる」手当ては **撤去した** — 共有ライブラリにしたので
  #   上流の GEOGRAM_API (GEO_DYNAMIC_LIBS 時に visibility("default")) がそのまま効く。
  #   ⚠ ここで -fvisibility=default を上書きすると *公開点以外まで export* してしまい、
  #     上流が意図した境界を壊す。⇒ 何もしないのが正しい。

  # ★★ geogram は Linux で **GEOGRAM_WITH_TBB=OFF でも無条件に `tbb` をリンクする**
  #   (src/lib/geogram/CMakeLists.txt の "quick and dirty fix, for using parallel STL")。
  #   TBB が入っていない機械では `cannot find -ltbb` でリンクが落ちる。
  #   → **上流のソースは書き換えず**、ターゲットのリンク依存から素の `tbb` を外し、
  #     見つかったときは **実パス**へ差し替える (TBB が無ければ外したままにする)。
  #   (#3419 で Manifold PAR=ON をやるときに TBB を入れる判断をしたら、その時は自動的に残る。)
  #
  # ★ 2026-08-26 (MinGW 実機で判明): **実パスへの差し替えが要る**。MSYS2 の TBB は
  #   `libtbb12.dll.a` だけを配り `libtbb.dll.a` を持たないので、
  #     ① `find_library(NAMES tbb)` が空振りして -ltbb を外す → geogram の parallel STL
  #        (`__pstl::__tbb_backend`) が参照する TBB シンボルが **undefined reference** になる
  #     ② 名前を足して見つけても、素の `tbb` は `-ltbb` に展開されるので結局解決できない
  #   Debian は `libtbb.so` があるので ①② とも表面化しなかった。実パスなら両方に効く。
  find_library(SRAVA_TBB_LIB NAMES tbb tbb12)
  foreach(_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(_gl geogram ${_prop})
    if(_gl)
      list(REMOVE_ITEM _gl tbb)
      if(SRAVA_TBB_LIB)
        list(APPEND _gl ${SRAVA_TBB_LIB})
      endif()
      set_target_properties(geogram PROPERTIES ${_prop} "${_gl}")
    endif()
  endforeach()
  if(SRAVA_TBB_LIB)
    message(STATUS "geogram: parallel STL 用の TBB を実パスで張りました (${SRAVA_TBB_LIB})")
  else()
    message(STATUS "geogram: TBB が無いので parallel STL 用の -ltbb を外しました (機能には影響しません)")
  endif()

  # ★ geogram は **OpenMP を使う** (GEO::Process のスレッド管理が GOMP_* を参照する) のに、
  #   自分のターゲットに OpenMP を張っていない。静的 .a をモジュールへリンクすると
  #   `undefined symbol: GOMP_critical_end` で **dlopen 時に**落ちる (実際に踏んだ)。
  #   → こちらで OpenMP を PUBLIC で足し、モジュールへ伝播させる。
  find_package(OpenMP)
  if(OpenMP_CXX_FOUND)
    # ★ **plain 署名**で書くこと。geogram 自身が plain (`target_link_libraries(geogram tbb)`) を
    #   使っているので、PUBLIC/PRIVATE を付けると
    #   "All uses of target_link_libraries with a target must be either all-keyword or all-plain"
    #   で configure が落ちる。plain は「リンクもし、利用側へも伝播する」= 欲しい挙動。
    target_link_libraries(geogram OpenMP::OpenMP_CXX)
  endif()

  # ---- geogram.so (幾何 + manifest)。geogram を静的 bundle・pig/pts/tinyState は host 供給。----
  tinystate_codegen(gg_ts_codegen
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ptsggWireCacheStreamWriterMesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ptsggWireCacheStreamReaderMesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaBox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaPyramid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaCylinder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaCone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaTorus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaTetrahedron.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaPrism.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaIcosphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaImport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaEmpty3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaTube.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaSphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaUnion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaHull.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaIntersection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaDifference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaSolidify.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaVolume.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaDistanceAt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaEstimateNormals.cpp   # ★ #3528
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaCast.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaTranslate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaRotate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaScale.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaMirror.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggaTransform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/c++/ggtsAgent.cpp
  )
  # ★ #3434 (2026-08-22・案 B'): geogram の**幾何クラスを共有ライブラリへ** (mf/cg と同じ理由)。
  set(GG_GEOM_SRC
    modules/geogram/c++/ptsggWireCacheStreamWriterMesh.cpp
    modules/geogram/c++/ptsggWireCacheStreamReaderMesh.cpp
    modules/geogram/c++/ggCacheCodec.cpp
    modules/geogram/c++/ggMesh.cpp)
  add_library(srava_gg SHARED ${GG_GEOM_SRC})
  set_target_properties(srava_gg PROPERTIES OUTPUT_NAME "srava_gg" INSTALL_RPATH "$ORIGIN")
  srava_export_all(srava_gg)   # ★ Windows: 明示エクスポート
  # ★ #3528: ggCacheCodec.cpp が &ptCloud::WIRE を並べる (点群型を借りる) ので srava_pt に依存する。
  # ★ #3535 (2026-09-15): geogram が **共有ライブラリ**なら素直に PUBLIC で渡してよい
  #   (実体がプロセスに 1 つなので、モジュールが直に引いてもコピーは増えない)。
  # ⚠⚠ 2026-09-16 (macMINI 指摘): **MinGW では静的に戻した** (上の ①②③) ので、
  #   この木には **共有の枝と静的の枝が同居している**。⇒ 配線も枝ごとに分ける。
  #   静的アーカイブは *参照されたオブジェクトだけ* を引くので、srava_gg と geogram モジュールが
  #   **それぞれ自分の分を抱え込む**形になりうる。そうなると GEO:: の file-scope な可変状態が
  #   .so ごとに別物になる (= #3535 4 節の SIGSEGV)。
  # ★★★ ⚠ **実測 (box・2026-09-16 夜) では 2 コピーは起きていなかった。** 直す動機は
  #   「壊れているから」ではなく **「壊れることを *許す* 構造だから」**。訂正しておく:
  #     libsrava_gg.dll  GEO:: 定義 2961  /  geogram.dll  1  /  openvdb_gg.dll  4
  #     可変大域 (CmdLine::initialize / Process::initialize) は **libsrava_gg.dll のみ**
  #   機序: モジュールのリンク行で **srava_gg の import lib が先に GEO:: を全部供給する**ので、
  #   libgeogram.a からは 1 つもメンバが引かれない (静的アーカイブは未解決があるときだけ引く)。
  #   ⚠⚠ だが 1 つで済んでいるのは **3 つの偶然が重なっているから**:
  #     ① srava_export_all の --export-all-symbols が GEO:: を 2961 個 export している
  #     ② モジュールの LINK の並びで **srava_gg が geogram より先**に来る ← ★暗黙・無警告
  #     ③ #3545 で op の GEO 参照が減り、未解決が残らない
  #   ⇒ ②は並びを入れ替えるだけで壊れ、**何の警告も出ない**。⇒ 静的のときは
  #   **#3535① の「借りる」形**にして、いま偶然成り立っていることを *構造* で保証する:
  #     ・geogram は srava_gg が **PRIVATE で** 抱える (モジュールへは渡さない)
  #     ・モジュールは srava_gg が export する GEO:: を **借りる** (下の LINK 行)
  #     ・可視性は default にする (ELF)。PE は srava_export_all の --export-all-symbols が担う
  #   ★ 検証済み (box・c05aab1 を素からフルビルド): リンク rc=0 (FAILED 0) ・ GEO:: の数は
  #     **借りる形でも 2961 / 1 / 4 で前と同一** ・ objdump -p geogram.dll の import 表に
  #     libsrava_gg.dll が居る ・ ninja -t commands に libgeogram.a が **0 件**。
  #     ⇒ 「定義が無い」「借り先が名指しで在る」「リンク行に .a が無い」の 3 経路が一致。
  #   ★ #3528: ggCacheCodec.cpp が &ptCloud::WIRE を並べる (点群型を借りる) ので srava_pt に依存する。
  if(_srava_gg_dynamic)
    target_link_libraries(srava_gg PUBLIC pig srava_pt geogram)
  else()
    # ★ #3535① の形。⚠ include だけは PUBLIC で流す (モジュールがヘッダを読むため)。
    target_link_libraries(srava_gg PUBLIC pig srava_pt PRIVATE geogram)
    target_include_directories(srava_gg SYSTEM PUBLIC
      $<TARGET_PROPERTY:geogram,INTERFACE_INCLUDE_DIRECTORIES>)
    foreach(_ggvis geogram geogram_third_party)
      if(TARGET ${_ggvis})
        set_target_properties(${_ggvis} PROPERTIES
          CXX_VISIBILITY_PRESET default C_VISIBILITY_PRESET default
          VISIBILITY_INLINES_HIDDEN OFF)
      endif()
    endforeach()
    message(STATUS "geogram: 静的 ⇒ #3535① の「借りる」形で配線します (モジュールは直リンクしない)")
  endif()
  target_compile_definitions(srava_gg PRIVATE SRAVA_VALUE_ONLY)
  target_include_directories(srava_gg PUBLIC ${SRAVA_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/h)
  add_dependencies(srava_gg pigts_codegen gg_ts_codegen gen_srava_parser)
  install(TARGETS srava_gg LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
  # ★ モジュールが引くもの。共有なら geogram も直に引いてよい / 静的なら srava_gg から借りる。
  if(_srava_gg_dynamic)
    set(_srava_gg_modlink srava_gg geogram)
  else()
    set(_srava_gg_modlink srava_gg)
  endif()
  # ★★ #3535: **私物の共有ライブラリを $PREFIX/lib へ install する** (docs §7.3 末尾の規約)。
  #   ⚠ 書き忘れると「ビルドツリーでは動くが install したら agent が起動しない」になる。
  #   ★ srava_gg の INSTALL_RPATH ($ORIGIN) と モジュールの $ORIGIN/../.. がどちらもここを指す。
  #   ⚠ geogram は EXCLUDE_FROM_ALL (規約 B) なので、**こちらから明示的に install する**。
  #   ⚠ 静的の枝 (MinGW) では geogram は .a で、**配る必要が無い** (srava_gg が抱えている)。
  if(_srava_gg_dynamic)
    install(TARGETS geogram LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
  endif()

  srava_add_module(geogram_module
    OUTPUT   geogram
    SOURCES  modules/geogram/c++/ggaBox.cpp
             modules/geogram/c++/ggaPyramid.cpp
             modules/geogram/c++/ggaCylinder.cpp
             modules/geogram/c++/ggaCone.cpp
             modules/geogram/c++/ggaTorus.cpp
             modules/geogram/c++/ggaTetrahedron.cpp
             modules/geogram/c++/ggaPrism.cpp
             modules/geogram/c++/ggaIcosphere.cpp
             modules/geogram/c++/ggaImport.cpp
             modules/geogram/c++/ggaEmpty3D.cpp
             modules/geogram/c++/ggaTube.cpp
             modules/geogram/c++/ggaSphere.cpp
             modules/geogram/c++/ggaUnion.cpp
             modules/geogram/c++/ggaHull.cpp
             modules/geogram/c++/ggaIntersection.cpp
             modules/geogram/c++/ggaDifference.cpp
             modules/geogram/c++/ggaSolidify.cpp
             modules/geogram/c++/ggaVolume.cpp
             modules/geogram/c++/ggaDistanceAt.cpp
             modules/geogram/c++/ggaEstimateNormals.cpp
             modules/geogram/c++/ggaExport.cpp
             modules/geogram/c++/ggaCast.cpp
             modules/geogram/c++/ggaTranslate.cpp
    modules/geogram/c++/ggaRotate.cpp
    modules/geogram/c++/ggaScale.cpp
    modules/geogram/c++/ggaMirror.cpp
    modules/geogram/c++/ggaTransform.cpp
             modules/geogram/c++/ggtsAgent.cpp
             modules/geogram/manifest.cpp
    DEPENDS  pigts_codegen gg_ts_codegen gen_srava_parser
    # ★ #3535: geogram が **共有** なら、モジュールが直にリンクしてもコピーは増えない。
    # ⚠⚠ 2026-09-16: MinGW では静的に戻したので、**枝で分ける** (macMINI 指摘)。
    #   静的のまま直リンクすると 2 コピー目ができ、GEO:: の大域状態が割れる
    #   (#3535 4 節の SIGSEGV)。⇒ 静的のときは srava_gg だけを引いて **借りる**。
    LINK     ${_srava_gg_modlink}
    DEFINES  SRAVA_VALUE_ONLY
    INCLUDES ${SRAVA_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/h
    INSTALL)

  add_test(NAME srava_module_geogram_so COMMAND srava_module_probe $<TARGET_FILE:geogram_module>)
  set_tests_properties(srava_module_geogram_so PROPERTIES
    PASS_REGULAR_EXPRESSION "MODULE_OK name=geogram .*make_agent=yes")
  add_test(NAME srava_type_geogram COMMAND srava_module_probe $<TARGET_FILE:geogram_module>)
  set_tests_properties(srava_type_geogram PROPERTIES PASS_REGULAR_EXPRESSION "TYPES gg-mesh3d")

  # ---- geogram の振る舞い回帰 (test/srava_geogram.sh 冒頭に各モードの意図)。
  #   solidify は #3435 の受け入れ条件 (#3445) そのもの。manifold 必須 (tube を作らせるため)。
  set(_gg_modes arity fatal)   # ★ #3436 P4: n 項ブールの arity 掃引 / ★ 致命エラーが srava のエラーになる (どちらも geogram 単独で走る)
  if(SRAVA_MODULE_MANIFOLD)
    list(APPEND _gg_modes solidify mfcross warmroute)
  endif()
  # cgcross は cgal (厳密 "MESH" を書く側) が要る。manifold は不要。
  # ★ 2026-08-25 (eabd8d3) 以降、solidify(cg-mesh3d) は精度クラス保存のため nef へ振られる
  #   (geogram の sig から (cg-mesh3d) 行を削除) ので nef も要る。
  if(SRAVA_MODULE_CGAL AND SRAVA_MODULE_NEF)
    list(APPEND _gg_modes cgcross)
  endif()
  foreach(_m IN LISTS _gg_modes)
    add_test(NAME srava_geogram_${_m}
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_geogram.sh $<TARGET_FILE:srava> ${_m})
    set_tests_properties(srava_geogram_${_m} PROPERTIES
      ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/ggcache-${_m}"
      PASS_REGULAR_EXPRESSION "GEOGRAM-[A-Z]+-OK"
      FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
      TIMEOUT 300)
  endforeach()
endif()

# ==================================================================
# ★ #3438 P6: Cherchi ら "Interactive and Robust Mesh Booleans" (MIT・以下 IRMB)。
#   geogram (P3) と並べて「**厳密高速勢**」の 2 点比較にする。厳密の実現方法が違い、
#   IRMB は交点を明示座標で構成せず **indirect predicates** (「どの 3 平面の交わりか」の
#   間接表現) のまま厳密述語を評価する。
#   ★★ **素で n 項**: booleanPipeline が三角形ごとの label を取り arrangement を 1 回だけ作る
#     ので、#3436 P4 の「n 項 vs 二分木」の対照相手が geogram と 2 つになる。
#
#   ★ 取り込みは **規約 B の変種**: FetchContent で取得するが、上流の CMakeLists.txt は
#     **add_subdirectory しない** (SOURCE_SUBDIR に CMakeLists.txt の無いディレクトリを指す)。
#     理由 = 上流のそれは実行体を 5 本と **oneTBB を自前で建てる**ので、そのまま取り込むと
#     1 プロセスに TBB が 2 つ入る (規約 C 違反)。IRMB 自体は**ヘッダだけ**で、必要なのは
#     include パスと shewchuk.c 1 本だけなので、こちらでターゲットを組む方が素直。
#
#   ★ ライセンス (全て GPLv3 と両立):
#     IRMB / arrangements / Cinolib = MIT、Indirect_Predicates = LGPL-2.1、
#     abseil-cpp / parallel-hashmap / oneTBB = Apache-2.0、Eigen (Cinolib 同梱) = MPL-2.0。
#     ⚠ Cinolib の imgui/GL 系は使わない (ヘッダを include しないだけで足りる)。
#
#   ★ コンパイル条件 (述語が IEEE 754 の丸めに依存する):
#     -frounding-math (必須) / -ffp-contract=off (FMA の縮約で誤差なし変換が壊れるのを防ぐ) /
#     AVX2 は有れば使う (無ければ SSE2 = x86-64 の既定)。
# ==================================================================
if(SRAVA_MODULE_CHERCHI)
  include(FetchContent)
  # ★ SOURCE_SUBDIR に CMakeLists.txt の無いディレクトリを指すと、MakeAvailable は
  #   **取得だけして add_subdirectory しない** (CMake の文書化された挙動)。
  FetchContent_Declare(irmb
    GIT_REPOSITORY https://github.com/gcherchi/InteractiveAndRobustMeshBooleans.git
    # ★ pin は **main の HEAD** (2024-06-10・tag は無い)。
    #   ⚠⚠ この版には **out-of-bounds read** があり、退化した配置 (同じ形を軸に沿って平行移動する等)
    #     で **実行のたびに違う値**が返る。調査と再現手順は docs/cherchi-upstream-2026-08-27/。
    #   ⚠ 新しい枝 dev-mf (2025-07-02) は **試したが使えない** — ビルドが通らない
    #     (Indirect_Predicates の fork にインクルードガードが無い / -mavx2 と Boost.Multiprecision
    #      の always_inline が衝突) うえ、回避して建てても値が誤り、**CGAL 依存**になっている。
    #     移るとしたら別モジュールとして起こす話 (nef_snc / nef_hybrid と同じ形)。
    GIT_TAG        7bd6c2697695f555f9a15d7e154e084d841316f0
    GIT_SUBMODULES_RECURSE ON
    SOURCE_SUBDIR  code   # ← CMakeLists.txt が無いディレクトリ = configure させない
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(irmb)

  set(IRMB_DIR  ${irmb_SOURCE_DIR})
  set(IRMB_CINO ${IRMB_DIR}/arrangements/external/Cinolib)
  set(IRMB_INCLUDES
    ${IRMB_DIR}
    ${IRMB_DIR}/code
    ${IRMB_DIR}/arrangements/code
    ${IRMB_DIR}/arrangements/external/Indirect_Predicates/include
    ${IRMB_DIR}/arrangements/external/abseil-cpp
    ${IRMB_DIR}/arrangements/external/parallel-hashmap
    ${IRMB_CINO}/include
    ${IRMB_CINO}/external/eigen                  # ★ Cinolib が Eigen を同梱している (別途要らない)
    ${IRMB_CINO}/external/shewchuk_predicates)

  # ★ TBB は **規約 C** (system)。OpenVDB / OCCT と同じ 1 インスタンスを共有する。
  find_package(TBB REQUIRED CONFIG)

  include(CheckCXXCompilerFlag)
  # ⚠ この検査は「そのフラグでコンパイルが通るか」しか見ない。arm64 の Apple clang は
  #   -mavx2 をエラーにせず「未使用引数」の警告で流すので、**AVX の無い機械でも真になる**。
  #   よって結果をそのまま使わず、アーキテクチャと出力形式で絞る。
  check_cxx_compiler_flag("-mavx2" SRAVA_CH_HAS_AVX2)
  set(IRMB_FLAGS -frounding-math -ffp-contract=off)
  # ⚠ Windows (PE) では付けてはいけない: x64 ABI のスタック整列が 16B なのに AVX2 は 32B を
  #   要求するため、cherchi が**実行時に落ちる** (2026-08-28 に MinGW 実機で確認。
  #   volume(box(2,2,2)) すら "agent closed unexpectedly" になり、外すと正しく 8 を返す)。
  #   ⚠⚠ 2026-09-25: 条件が `NOT WIN32` だけだと **Cygwin が漏れる** (CMake は Cygwin で
  #     WIN32=0)。⇒ Cygwin も **PE を吐く**ので同じ ABI 不整合 (16B 対 32B) に当たる。
  #     上のコメントが「出力形式で絞る」と言っているのに、判定が OS 側になっていた。
  #     (いまは SRAVA_MODULE_CHERCHI が Cygwin で OFF なので不発だが、有効化した日に効く)
  if(SRAVA_CH_HAS_AVX2 AND NOT WIN32 AND NOT CYGWIN
     AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    list(APPEND IRMB_FLAGS -mavx2)
  endif()

  # ---- cherchi.so (幾何 + manifest)。IRMB はヘッダのみ・pig/pts/tinyState は host 供給。----
  tinystate_codegen(ch_ts_codegen
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/ptschWireCacheStreamWriterMesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/ptschWireCacheStreamReaderMesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaBox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaSphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaPyramid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaCylinder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaCone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaTorus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaTetrahedron.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaPrism.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaIcosphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaImport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaEmpty3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaTube.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaUnion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaIntersection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaDifference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaVolume.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaCast.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaTranslate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaRotate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaScale.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaMirror.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chaTransform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/c++/chtsAgent.cpp
  )
  # ★ 幾何クラスは共有ライブラリへ (mf/cg/gg と同じ理由 = reader/writer と op が同じ実体を見る)。
  #   ⚠ shewchuk.c は **K&R C** なので C としてコンパイルさせる (C++ では通らない)。
  set(CH_GEOM_SRC
    modules/cherchi/c++/ptschWireCacheStreamWriterMesh.cpp
    modules/cherchi/c++/ptschWireCacheStreamReaderMesh.cpp
    modules/cherchi/c++/chCacheCodec.cpp
    modules/cherchi/c++/chMesh.cpp
    ${IRMB_CINO}/external/shewchuk_predicates/shewchuk.c)
  add_library(srava_ch SHARED ${CH_GEOM_SRC})
  set_target_properties(srava_ch PROPERTIES OUTPUT_NAME "srava_ch" INSTALL_RPATH "$ORIGIN")
  target_link_libraries(srava_ch PUBLIC pig PRIVATE TBB::tbb)
  target_compile_definitions(srava_ch PRIVATE SRAVA_VALUE_ONLY TBB_PARALLEL=1 CINOLIB_USES_SHEWCHUK_PREDICATES)
  target_compile_options(srava_ch PRIVATE ${IRMB_FLAGS})
  target_include_directories(srava_ch
    PUBLIC  ${SRAVA_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/h
    PRIVATE ${IRMB_INCLUDES})
  add_dependencies(srava_ch pigts_codegen ch_ts_codegen gen_srava_parser)
  install(TARGETS srava_ch LIBRARY DESTINATION lib RUNTIME DESTINATION bin)

  srava_add_module(cherchi_module
    OUTPUT   cherchi
    SOURCES  modules/cherchi/c++/chaBox.cpp
             modules/cherchi/c++/chaSphere.cpp
             modules/cherchi/c++/chaPyramid.cpp
             modules/cherchi/c++/chaCylinder.cpp
             modules/cherchi/c++/chaCone.cpp
             modules/cherchi/c++/chaTorus.cpp
             modules/cherchi/c++/chaTetrahedron.cpp
             modules/cherchi/c++/chaPrism.cpp
             modules/cherchi/c++/chaIcosphere.cpp
             modules/cherchi/c++/chaImport.cpp
             modules/cherchi/c++/chaEmpty3D.cpp
             modules/cherchi/c++/chaTube.cpp
             modules/cherchi/c++/chaUnion.cpp
             modules/cherchi/c++/chaIntersection.cpp
             modules/cherchi/c++/chaDifference.cpp
             modules/cherchi/c++/chaVolume.cpp
             modules/cherchi/c++/chaExport.cpp
             modules/cherchi/c++/chaCast.cpp
             modules/cherchi/c++/chaTranslate.cpp
    modules/cherchi/c++/chaRotate.cpp
    modules/cherchi/c++/chaScale.cpp
    modules/cherchi/c++/chaMirror.cpp
    modules/cherchi/c++/chaTransform.cpp
             modules/cherchi/c++/chtsAgent.cpp
             modules/cherchi/manifest.cpp
    DEPENDS  pigts_codegen ch_ts_codegen gen_srava_parser
    LINK     srava_ch
    DEFINES  SRAVA_VALUE_ONLY
    INCLUDES ${SRAVA_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/modules/cherchi/h
    INSTALL)

  add_test(NAME srava_module_cherchi_so COMMAND srava_module_probe $<TARGET_FILE:cherchi_module>)
  set_tests_properties(srava_module_cherchi_so PROPERTIES
    PASS_REGULAR_EXPRESSION "MODULE_OK name=cherchi .*make_agent=yes")
  add_test(NAME srava_type_cherchi COMMAND srava_module_probe $<TARGET_FILE:cherchi_module>)
  set_tests_properties(srava_type_cherchi PROPERTIES PASS_REGULAR_EXPRESSION "TYPES ch-mesh3d")

  # ---- cherchi の振る舞い回帰 (test/srava_cherchi.sh 冒頭に各モードの意図)。
  set(_ch_modes bool arity contact guard threads)  # ★ 値 / n 項の掃引 / 退化配置 2 種 / op 内並列の絞り (#3481)
  if(SRAVA_MODULE_MANIFOLD)
    list(APPEND _ch_modes mfcross)   # mf-mesh3d との混成 (同じ 4CC をそのまま読む)
  endif()
  if(SRAVA_MODULE_CGAL)
    list(APPEND _ch_modes cgcross)   # cgal の厳密 "MESH" からの昇格読み (cast)
  endif()
  foreach(_m IN LISTS _ch_modes)
    add_test(NAME srava_cherchi_${_m}
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_cherchi.sh $<TARGET_FILE:srava> ${_m})
    set_tests_properties(srava_cherchi_${_m} PROPERTIES
      ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/chcache-${_m}"
      PASS_REGULAR_EXPRESSION "CHERCHI-[A-Z]+-OK"
      FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
      TIMEOUT 300)
  endforeach()
endif()

# ==================================================================
# ★ #3434 P2: OpenVDB (AcademySoftwareFoundation・Apache-2.0) — **第 3 の表現クラス**。
#   三角形メッシュでも B-rep でもない「格子点にスカラ値を持つ」表現 (SDF)。表面は値 0 の
#   等値面として暗黙に定義され、ブールは **点ごとの min/max だけ**なので **位相の場合分けが
#   存在しない** = 自己交差・非多様体・汚い入力でも必ず答えが出る。
#
#   外部依存の取り込みは **規約 B** (FetchContent・docs/srava_module_design.md §7.3)。
#   ★ EXCLUDE_FROM_ALL 必須: 無いと上流の install() が srava の install に相乗りする。
#
#   ★★ 依存の絞り込み (実地確認 2026-08-19・OpenVDB v12.1.1):
#     - TBB          … **必須で切れない**。core の CMakeLists が find_package(TBB REQUIRED)。
#                       トップレベルの USE_TBB option は "Use TBB in building the **NanoVDB**
#                       library" で core には効かない。core の 65 ファイルが tbb/ を include する。
#     - Boost        … OPENVDB_USE_DELAYED_LOADING=OFF で **外せる** (delayed loading だけが
#                       Boost::iostreams を要求する)。grid を遅延ロードしないだけで機能は変わらない。
#     - Blosc        … USE_BLOSC=OFF で外す。切っても .vdb の **活性マスク圧縮・half-float・
#                       zip(zlib)** は残るので、まず無しで測ってから足すか決める。
#     - zlib         … システムのを使う (1.2.7 以上が要求・Debian 13 は 1.3.1)。
#   必要な tool は全部 core 側にある (Composite.h = csgUnion/Intersection/Difference /
#   LevelSetRebuild.h / MeshToVolume.h / VolumeToMesh.h / LevelSetFilter.h)。NanoVDB は要らない。
# ==================================================================
if(SRAVA_MODULE_OPENVDB)
  include(FetchContent)

  # ---- TBB: ★**規約 C** (システムから検出)。ビルド全体で 1 インスタンス ----
  # TBB は **スケジューラの状態を持つランタイム**なので、1 プロセスに 2 つ在ってはいけない。
  # 静的リンクして複数の .so に埋めると各 .so が自分の TBB を持つことになる
  # (nef_snc / nef_hybrid で踏んだ「同一ソースの 2 .so はシンボル衝突 (RTLD_GLOBAL)」の裏返しで、
  #  HIDDEN にすれば衝突は消える代わりに各自が自分の TBB を持つ)。
  #
  # ★ 2026-08-19: 当初は FetchContent で共有ビルドして $PREFIX/lib へ install していたが、
  #   **OCCT (#3437) も TBB を要求する**と分かって整理し直した。OCCT の
  #   INTERFACE_LINK_LIBRARIES に素の `tbb;tbbmalloc` が入っており、OCCT 自体が規約 C
  #   (システムの libocct-*) なので必然的に **system TBB** を使う。FetchContent 版と併存させると
  #   まさに「1 プロセスに TBB が 2 つ」になるため、**TBB 自体を規約 C へ寄せた**。
  #   distro が配っていて (Debian libtbb-dev)、かつ共有状態を持つランタイム = まさに 1 つで
  #   あるべきもの、という点で規約 C の条件に合う。自前で install する .so も無くなる。
  # ★ 規約 C の約束どおり、この if() の**中**で find_package する (OFF なら走らない)。
  # ★ geogram (静的で正しい・OpenMP はシステムの libgomp を共有) とは扱いが違う理由もこれ。
  #
  # ★ CGAL が CMAKE_MODULE_PATH に足した **CGAL 同梱の FindTBB.cmake** が MODULE モードで
  #   先に拾われる。あれは oneTBB 2021+ で消えた tbb/tbb_stddef.h を探すので当てにならない
  #   → **CONFIG モードを明示**してシステムの TBBConfig.cmake を直接引く。
  find_package(TBB REQUIRED CONFIG)
  # ---- OpenVDB core: **静的**でよい (vd の .so だけが使う) ----
  # 静的にしても OpenVDB のグローバル状態 (型レジストリ・openvdb::initialize) は
  # openvdb.so の中に 1 つだけなので問題にならない。TBB と扱いが違うのはそのため。
  set(OPENVDB_BUILD_CORE           ON  CACHE BOOL "" FORCE)
  # ★★ #3535 (ひさ判断 2026-09-15): OpenVDB も **共有ライブラリ**にする。geogram と同じ理由 —
  #   静的アーカイブだと libsrava_vd と openvdb.so / openvdb_mf / openvdb_cg / openvdb_gg が
  #   それぞれ自分の分を抱え込み、**同じものが何コピーも**入る。モジュールが増えるたびに
  #   その絡まりが増える。⇒ 共有にすれば実体はプロセスに 1 つで、*構造的に起きえない*。
  #   ⚠ 現状 OpenVDB の型レジストリは libsrava_vd にしか無く状態は割れていないが、
  #     それは「たまたま op が openvdb を直に触っていないから」であって構造の保証ではない。
  #   ⇒ 規約 B ではなく docs §7.3 末尾の「私物の共有ライブラリは $PREFIX/lib へ install」に従う。
  set(OPENVDB_CORE_SHARED          ON  CACHE BOOL "" FORCE)
  set(OPENVDB_CORE_STATIC          OFF CACHE BOOL "" FORCE)
  set(OPENVDB_BUILD_BINARIES       OFF CACHE BOOL "" FORCE)   # 既定 ON (vdb_print) なので明示 OFF
  set(OPENVDB_BUILD_UNITTESTS      OFF CACHE BOOL "" FORCE)
  set(OPENVDB_BUILD_DOCS           OFF CACHE BOOL "" FORCE)
  set(OPENVDB_BUILD_NANOVDB        OFF CACHE BOOL "" FORCE)
  set(OPENVDB_BUILD_AX             OFF CACHE BOOL "" FORCE)
  set(OPENVDB_ENABLE_UNINSTALL     OFF CACHE BOOL "" FORCE)   # 上流が uninstall ターゲットを足すのを防ぐ
  set(OPENVDB_INSTALL_CMAKE_MODULES OFF CACHE BOOL "" FORCE)
  set(OPENVDB_USE_DELAYED_LOADING  OFF CACHE BOOL "" FORCE)   # ★これで Boost 依存が消える
  set(USE_EXPLICIT_INSTANTIATION   OFF CACHE BOOL "" FORCE)   # ★ビルド時間とバイナリサイズを大幅に削る
  set(USE_CCACHE                   OFF CACHE BOOL "" FORCE)   # CMAKE_CXX_COMPILER_LAUNCHER を勝手に触らせない
  # ★ 既定 "Auto" は jemalloc を探し、無ければ tbbmalloc へ落ちる (警告つき)。どちらも
  #   要らない (性能推奨であって必須ではない・tbbmalloc は TBB の別ターゲットで、
  #   「TBB は 1 インスタンス」の規則を余計に複雑にする) ので明示的に None。
  set(CONCURRENT_MALLOC            "None" CACHE STRING "" FORCE)
  set(USE_BLOSC                    OFF CACHE BOOL "" FORCE)
  set(USE_ZLIB                     ON  CACHE BOOL "" FORCE)   # システムの zlib (.vdb の zip 圧縮)
  set(USE_NANOVDB                  OFF CACHE BOOL "" FORCE)
  set(USE_AX                       OFF CACHE BOOL "" FORCE)
  set(USE_EXR                      OFF CACHE BOOL "" FORCE)
  set(USE_LOG4CPLUS                OFF CACHE BOOL "" FORCE)
  set(USE_IMATH_HALF               OFF CACHE BOOL "" FORCE)   # OpenVDB 同梱の half を使う
  # 静的 .a をモジュール (.so) へリンクするので PIC が要る (geogram と同じ理由)。
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  FetchContent_Declare(openvdb
    GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openvdb.git
    GIT_TAG        v12.1.1
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(openvdb)

  # ---- openvdb.so (幾何 + manifest)。OpenVDB は静的 bundle・TBB は共有・pig/pts/tinyState は host 供給。----
  tinystate_codegen(vd_ts_codegen
    # ⚠⚠ **登録は 2 か所**要る — ここ (codegen) と srava_add_module の SOURCES。
    #   codegen 側を落とすと **ビルドは通ってロードで undefined symbol** になる
    #   (外側クラスのコンストラクタが生成されないため)。2026-09-22 に #3579 で踏んだ。
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaPtIntersection.cpp   # ★ #3580
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaPtDifference.cpp     # ★ #3580
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/ptsvdWireCacheStreamWriterGrid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/ptsvdWireCacheStreamReaderGrid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaVolume.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaBbox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaCentroid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaArea.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaValid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaSphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaTranslate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaDistanceAt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaRotate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaScale.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaMirror.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaTransform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaBox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaPyramid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaCylinder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaCone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaTorus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaTetrahedron.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaPrism.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaIcosphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaImport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaEmpty3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaTube.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaVoxels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaUnion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaIntersection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaDifference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaRenormalize.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdaOffset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/c++/vdtsAgent.cpp
  )
  # ★ #3434 (2026-08-22): openvdb を 3 分割する準備 — **幾何クラスを共有ライブラリへ**。
  #   理由: openvdb_mf.so / openvdb_cg.so は **補完的で同時に load される**ので、
  #   それぞれが vdGrid のコピーを持つと **in-proc で d_cast が失敗する**
  #   (今回 vdMesh で踏んだのとまったく同じバグが vdGrid で再発する)。
  #   → クラスの実体を 1 箇所に集約する。pig が共有ライブラリである理由と同じ。
  add_library(srava_vd SHARED
    modules/openvdb/c++/ptsvdWireCacheStreamWriterGrid.cpp
    modules/openvdb/c++/ptsvdWireCacheStreamReaderGrid.cpp
    modules/openvdb/c++/vdCacheCodec.cpp
    modules/openvdb/c++/vdGrid.cpp)
  set_target_properties(srava_vd PROPERTIES
    OUTPUT_NAME "srava_vd"
    INSTALL_RPATH "$ORIGIN")
  srava_export_all(srava_vd)   # ★ Windows: 明示エクスポート
  # ★ #3535: 上流の ALIAS @c openvdb を使う (共有/静的のどちらを建てても当たる)。
  # ★ #3580: srava_pt — vdCacheCodec.cpp の provides が &ptCloud::WIRE を参照する
  #   (点群を受けて返す op のため)。⚠ モジュール側の LINK だけでは足りない —
  #   provides の表は **共有ライブラリ側**に在るので、ここに無いと
  #   libsrava_vd.so: undefined reference to `ptCloud::WIRE' でリンクが落ちる。
  #   ★ srava_gu (geomutils) が同じ理由で同じ形にしてある。
  target_link_libraries(srava_vd PUBLIC pig openvdb srava_pt)
  target_compile_definitions(srava_vd PRIVATE SRAVA_VALUE_ONLY)
  target_include_directories(srava_vd PUBLIC
    ${SRAVA_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/h
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/h)   # ★ #3580: ptCloud (値の器を借りる)

  add_dependencies(srava_vd pigts_codegen vd_ts_codegen gen_srava_parser)
  install(TARGETS srava_vd LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
  # ★★ #3535: 私物の共有ライブラリを $PREFIX/lib へ (docs §7.3 末尾)。
  #   ⚠ mac は install 時に install name が絶対パスへ書き換わる罠がある (geogram で実際に踏んだ)
  #     ⇒ @rpath に固定する。srava_vd の $ORIGIN と モジュールの $ORIGIN/../.. が
  #     どちらも $PREFIX/lib を指すので解決する。
  if(TARGET openvdb_shared)
    if(APPLE)
      set_target_properties(openvdb_shared PROPERTIES INSTALL_NAME_DIR "@rpath")
    endif()
    # ★★ PE (MinGW) は install name も RPATH も持たない。ローダが見るのは
    #   **exe の隣と PATH** だけで、**読み込む .dll の隣は見ない**。
    #   ⚠ 上流の CMake は libopenvdb.dll を ${CMAKE_INSTALL_PREFIX}/bin (= build/installtest/bin)
    #     へ出すので、build/ のモジュールからは見えず **dlopen が 0xC0000135
    #     (STATUS_DLL_NOT_FOUND) で落ちる** — 2026-09-16 に box で openvdb 系 19 本が道連れになった。
    #   ⇒ ビルドツリーでは srava.exe / モジュールと**同じ dir** へ出す。
    #   ⚠ 2026-09-25: `if(WIN32)` だけだと **Cygwin が漏れる**。Cygwin も PE (cygopenvdb.dll) を
    #     吐くので、同じ dir に置かないと dlopen が落ちる。(いまは OPENVDB が Cygwin で OFF)
    if(WIN32 OR CYGWIN)
      set_target_properties(openvdb_shared PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    endif()
    # ⚠ PE では .dll は LIBRARY でなく **RUNTIME**。LIBRARY だけ書くと Windows で
    #   **install から丸ごと漏れる** (import lib だけ入って実体が無い)。
    install(TARGETS openvdb_shared LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
  endif()

  # ---- ★ #3498: OpenVDB の中断機構がライブラリ層で効くことの単体回帰。
  #   occt_break と同じ理由でここに要る (planner 経由の SIGINT は #3417 の kill が先に効く)。
  #   ★ 「止まらない」側 (csgUnion) も一緒に固定してある — 上流が中断点を足したら赤くなり、
  #     vdBreak.h の表と #3498 の記述を直す合図になる。
  add_executable(test_openvdb_break test/openvdb_break_test.cpp)
  target_link_libraries(test_openvdb_break PRIVATE srava_vd)
  add_test(NAME openvdb_break COMMAND test_openvdb_break)
  set_tests_properties(openvdb_break PROPERTIES
    PASS_REGULAR_EXPRESSION "VDB-BREAK-OK"
    FAIL_REGULAR_EXPRESSION "FAIL|VDB-BREAK-FAIL|Segmentation|Aborted"
    TIMEOUT 300)

  # ★ #3580: 点群と距離場の積と差。判定器は距離場の符号 (geomutils 版と規約は同じ)。
  add_test(NAME srava_ptsplit_openvdb
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_ptsplit_openvdb.sh $<TARGET_FILE:srava>)
  set_tests_properties(srava_ptsplit_openvdb PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/ptsplitvdcache"
    PASS_REGULAR_EXPRESSION "PTSPLITVD-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|PTSPLITVD_FAIL|Segmentation|Aborted|core dumped"
    TIMEOUT 300)

  srava_add_module(openvdb_module
    OUTPUT   openvdb
    SOURCES  modules/openvdb/c++/vdaPtIntersection.cpp   # ★ #3580
             modules/openvdb/c++/vdaPtDifference.cpp     # ★ #3580
             modules/openvdb/c++/vdaVolume.cpp
             modules/openvdb/c++/vdaBbox.cpp
             modules/openvdb/c++/vdaCentroid.cpp
             modules/openvdb/c++/vdaArea.cpp
             modules/openvdb/c++/vdaValid.cpp
             modules/openvdb/c++/vdaSphere.cpp
             modules/openvdb/c++/vdaTranslate.cpp
             modules/openvdb/c++/vdaDistanceAt.cpp
             modules/openvdb/c++/vdaRotate.cpp
             modules/openvdb/c++/vdaScale.cpp
             modules/openvdb/c++/vdaMirror.cpp
             modules/openvdb/c++/vdaTransform.cpp
             modules/openvdb/c++/vdaBox.cpp
             modules/openvdb/c++/vdaPyramid.cpp
             modules/openvdb/c++/vdaCylinder.cpp
             modules/openvdb/c++/vdaCone.cpp
             modules/openvdb/c++/vdaTorus.cpp
             modules/openvdb/c++/vdaTetrahedron.cpp
             modules/openvdb/c++/vdaPrism.cpp
             modules/openvdb/c++/vdaIcosphere.cpp
             modules/openvdb/c++/vdaImport.cpp
             modules/openvdb/c++/vdaEmpty3D.cpp
             modules/openvdb/c++/vdaTube.cpp
             modules/openvdb/c++/vdaVoxels.cpp
             modules/openvdb/c++/vdaUnion.cpp
             modules/openvdb/c++/vdaIntersection.cpp
             modules/openvdb/c++/vdaDifference.cpp
             modules/openvdb/c++/vdaRenormalize.cpp
             modules/openvdb/c++/vdaOffset.cpp
             modules/openvdb/c++/vdtsAgent.cpp
             modules/openvdb/manifest.cpp
    DEPENDS  pigts_codegen vd_ts_codegen gen_srava_parser
    LINK     srava_vd openvdb srava_pt
    DEFINES  SRAVA_VALUE_ONLY
    INCLUDES ${SRAVA_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/h
             ${CMAKE_CURRENT_SOURCE_DIR}/modules/points/h
    INSTALL)

  # ★ #3434 (2026-08-22): **openvdb ⇄ manifold の境界モジュール**。
  #   voxelize / isosurface だけを持ち、**両側の本物のクラス** (mfMesh / vdGrid) を使う。
  #   幾何クラスは libsrava_vd / libsrava_mf にあるので、ここには実体を持たない
  #   (持つと in-proc で d_cast が失敗する)。ライセンス: Apache-2.0 のみ (GPL は混ざらない)。
  if(SRAVA_MODULE_MANIFOLD)
    tinystate_codegen(vm_ts_codegen
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_mf/c++/vmaVoxelize.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_mf/c++/vmaIsosurface.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_mf/c++/vmtsAgent.cpp)
    srava_add_module(openvdb_mf_module
      OUTPUT   openvdb_mf
      SOURCES  modules/openvdb_mf/c++/vmCacheCodec.cpp
               modules/openvdb_mf/c++/vmaVoxelize.cpp
               modules/openvdb_mf/c++/vmaIsosurface.cpp
               modules/openvdb_mf/c++/vmtsAgent.cpp
               modules/openvdb_mf/manifest.cpp
      DEPENDS  pigts_codegen vd_ts_codegen mf_ts_codegen vm_ts_codegen gen_srava_parser
      LINK     srava_vd srava_mf
      DEFINES  SRAVA_VALUE_ONLY
      INCLUDES ${SRAVA_GEN}
               ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/h
               ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_mf/h
      INSTALL)
  endif()

  # ⚠⚠ #3434 (2026-08-22): **openvdb ⇄ cgal の境界モジュール — CGAL をリンクするので GPL**。
  #   openvdb.so / openvdb_mf.so は Apache-2.0 のみ。**このモジュールだけがライセンスを変える**ので、
  #   option 名にも GPL を入れてある (ビルドする人が必ず目にする場所に置く)。
  # ★ option(SRAVA_MODULE_OPENVDB_CG_GPL) の宣言は冒頭の集約ブロックへ移した (2026-09-06)。
  #   ここに置くと 2159 行の add_test がクリーン初回で登録されない (理由は宣言側のコメント)。
  if(SRAVA_MODULE_CGAL AND SRAVA_MODULE_OPENVDB_CG_GPL)
    message(STATUS "openvdb_cg: ★ CGAL をリンクします = このモジュールは GPL です")
    # ★ #3468: export_vox (旧 cgal の cgaVoxelize) をここへ移設。HDF5 が無いビルドでは
    #   ソースごと外す (op も #ifdef SRAVA_HAVE_HDF5 で消える)。
    # ⚠ tinystate_codegen はジェネレータ式を受け取れない (add_custom_command の DEPENDS で評価される)
    #   ので、条件付きソースは普通の変数で組む。
    set(VC_TS_SRC
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_cg/c++/vcaVoxelize.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_cg/c++/vcaIsosurface.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_cg/c++/vctsAgent.cpp)
    set(VC_MOD_SRC
      modules/openvdb_cg/c++/vcCacheCodec.cpp
      modules/openvdb_cg/c++/vcaVoxelize.cpp
      modules/openvdb_cg/c++/vcaIsosurface.cpp
      modules/openvdb_cg/c++/vctsAgent.cpp
      modules/openvdb_cg/manifest.cpp)
    if(HDF5_FOUND)
      list(APPEND VC_TS_SRC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_cg/c++/vcaExportVox.cpp)
      list(APPEND VC_MOD_SRC modules/openvdb_cg/c++/vcaExportVox.cpp)
    endif()
    tinystate_codegen(vc_ts_codegen ${VC_TS_SRC})
    # ★★ #3545 段 4: **厳密幾何 (SoS 述語つき voxelize) を橋の共有ライブラリへ**。
    #   ⚠ これは *橋固有のロジック* なので libsrava_cg には入れない (ひさ判断 2026-09-15:
    #     「openvdb とも共有しているわけですしね。やめておきましょう」)。⇒ 橋が自分の .so を持つ。
    #   ★ これで op TU (vcaExportVox.cpp) が CGAL を 1 枚も引かなくなる。
    #   ⚠ **HIDDEN にしない** (幾何ライブラリと同じ扱い)。op のモジュールだけが HIDDEN。
    #   ⚠ HDF5 が無いビルドでは export_vox ごと消えるが、このライブラリは HDF5 に依存しない
    #     (厳密幾何だけ) ので、常に建ててよい。
    add_library(srava_vdcg SHARED
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_cg/c++/vdcgExact.cpp)
    set_target_properties(srava_vdcg PROPERTIES OUTPUT_NAME "srava_vdcg" INSTALL_RPATH "$ORIGIN")
    srava_export_all(srava_vdcg)   # ★ Windows: 明示エクスポート
    target_link_libraries(srava_vdcg PUBLIC pig srava_cg CGAL::CGAL)
    target_compile_definitions(srava_vdcg PRIVATE SRAVA_VALUE_ONLY)
    target_include_directories(srava_vdcg PUBLIC
      ${SRAVA_GEN}
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/h
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_cg/h)
    add_dependencies(srava_vdcg pigts_codegen gen_srava_parser)
    install(TARGETS srava_vdcg LIBRARY DESTINATION lib RUNTIME DESTINATION bin)

    srava_add_module(openvdb_cg_module
      OUTPUT   openvdb_cg
      SOURCES  ${VC_MOD_SRC}
      DEPENDS  pigts_codegen vd_ts_codegen vc_ts_codegen gen_srava_parser
      # ★ #3545 段 4: 厳密幾何は srava_vdcg が持つ (op はもう CGAL を呼ばない)。
      LINK     srava_vd srava_cg srava_vdcg
      DEFINES  SRAVA_VALUE_ONLY $<$<BOOL:${HDF5_FOUND}>:SRAVA_HAVE_HDF5>
      INCLUDES ${SRAVA_GEN}
               ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/h
               ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_cg/h
      INSTALL)
    if(HDF5_FOUND)
      target_include_directories(openvdb_cg_module PRIVATE ${HDF5_C_INCLUDE_DIRS} ${HDF5_INCLUDE_DIRS})
      target_link_libraries(openvdb_cg_module PRIVATE ${HDF5_C_LIBRARIES} ${HDF5_LIBRARIES})
    endif()
  endif()

  # ★ #3434 (2026-08-22): **openvdb ⇄ geogram の境界モジュール** (Apache-2.0 + BSD-3・GPL なし)。
  if(SRAVA_MODULE_GEOGRAM)
    tinystate_codegen(vg_ts_codegen
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_gg/c++/vgaVoxelize.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_gg/c++/vgaIsosurface.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_gg/c++/vgtsAgent.cpp)
    srava_add_module(openvdb_gg_module
      OUTPUT   openvdb_gg
      SOURCES  modules/openvdb_gg/c++/vgCacheCodec.cpp
               modules/openvdb_gg/c++/vgaVoxelize.cpp
               modules/openvdb_gg/c++/vgaIsosurface.cpp
               modules/openvdb_gg/c++/vgtsAgent.cpp
               modules/openvdb_gg/manifest.cpp
      DEPENDS  pigts_codegen vd_ts_codegen gg_ts_codegen vg_ts_codegen gen_srava_parser
      LINK     srava_vd srava_gg
      DEFINES  SRAVA_VALUE_ONLY
      INCLUDES ${SRAVA_GEN}
               ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb/h
               ${CMAKE_CURRENT_SOURCE_DIR}/modules/geogram/h
               ${CMAKE_CURRENT_SOURCE_DIR}/modules/openvdb_gg/h
      INSTALL)
  endif()

  add_test(NAME srava_module_openvdb_so COMMAND srava_module_probe $<TARGET_FILE:openvdb_module>)
  set_tests_properties(srava_module_openvdb_so PROPERTIES
    PASS_REGULAR_EXPRESSION "MODULE_OK name=openvdb .*make_agent=yes")
  add_test(NAME srava_type_openvdb COMMAND srava_module_probe $<TARGET_FILE:openvdb_module>)
  set_tests_properties(srava_type_openvdb PROPERTIES
    # ★ 2026-08-22 (#3434): openvdb.so は **vd-grid3d だけ**を申告する。
    #   mf-mesh3d は openvdb_mf.so が (本物の mfMesh で) 扱う。
    PASS_REGULAR_EXPRESSION "TYPES vd-grid3d")

  # ---- openvdb の振る舞い回帰 (test/srava_openvdb.sh 冒頭に各モードの意図)。
  #   ★ cavity (#3489) は openvdb.so だけで完結する (自前のプリミティブとブールで殻を作る)
  #     ので、cgal / manifold の有無に関わらず登録する。
  add_test(NAME srava_openvdb_cavity
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_openvdb.sh $<TARGET_FILE:srava> cavity)
  set_tests_properties(srava_openvdb_cavity PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/vdcache-cavity"
    PASS_REGULAR_EXPRESSION "OPENVDB-[A-Z]+-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)

  #   inputs は cgal ("MESH" の昇格読み) と manifold (入力メッシュ) の両方を要る。
  if(SRAVA_MODULE_CGAL AND SRAVA_MODULE_MANIFOLD)
    #   ★ cavity_vox (#3491) は voxelize / renormalize を見るので manifold の入力が要る。
    foreach(_m inputs converge bools roundtrip dxmix renorm offset steiner cavity_vox)
      add_test(NAME srava_openvdb_${_m}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_openvdb.sh $<TARGET_FILE:srava> ${_m})
      # ⚠ 番犬の予算 — 一族 9 本のうち **offset / steiner だけ重い**
      #   (mac 7.18s / 7.41s ・ Linux 0.79s / 1.05s ⇒ 最も遅い機 x3 = 30s)。
      #   残り 7 本は mac でも軽いので既定 10 秒のまま。
      set(_vd_hang "")
      if(_m STREQUAL "offset" OR _m STREQUAL "steiner")
        set(_vd_hang ";SRAVA_HANG_SECS=30")
      endif()
      set_tests_properties(srava_openvdb_${_m} PROPERTIES
        ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/vdcache-${_m}${_vd_hang}"
        PASS_REGULAR_EXPRESSION "OPENVDB-[A-Z]+-OK"
        FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
        TIMEOUT 300)
    endforeach()
  endif()
endif()

# ==================================================================
# ★ #3437 P5: Open CASCADE Technology (OCCT・LGPL-2.1 + 例外) — **第 4 の表現クラス**。
#   三角形メッシュでも Nef でも距離場でもなく、**B-rep** (位相 + 解析曲面/NURBS)。
#   ★円筒の側面は **1 面**であって三角形の集合ではない。分割数という概念が無い。
#
#   外部依存の取り込みは **規約 C** (find_package でシステムから・docs §7.3)。
#   OpenVDB が規約 B だったのと対照的で、判断の分かれ目は **distro が配っているか**:
#     OpenVDB … distro パッケージが無い          → B (FetchContent) しか採れなかった
#     OCCT    … Debian が libocct-* を配っている  → C が第一候補 (自前ビルドは非常に重い)
#   ★ 規約 C の約束「無い環境ではモジュールごと外れる」を守るため、find_package は
#     **この if() の中**で呼ぶ (OFF なら find_package すら走らない)。
#     ctest srava_build_nodeps が全モジュール OFF 構成を常時検証している。
#
#   ★★ ライセンス (実地確認 2026-08-19・Debian 7.8.1+dfsg1-3 の copyright を読んだ):
#     本体 = LGPL-2.1 + OpenCASCADE 例外 / 周辺に Expat・Unicode・BSD-2-clause /
#     Bison 生成物 1 ファイルが GPL-3+-with-Bison-exception。
#     **AGPL も非商用限定も無い** (geogram の TetGen/Triangle のような地雷は無し)。
#     LGPL-2.1 は §3 本文で「GPL v2 **またはそれ以降**へ読み替えてよい」と明文で許すので
#     GPLv3 の srava と組める。GMP/MPFR (LGPLv3+) が CGAL 経由で入っている前例と同じ扱い。
# ==================================================================
if(SRAVA_MODULE_OCCT)
  find_package(OpenCASCADE REQUIRED)
  message(STATUS "OCCT ${OpenCASCADE_VERSION} (${OpenCASCADE_INCLUDE_DIR})")

  # ★★ 2026-09-07: 任意機能の依存を **ここで一度だけ**検査する。
  #   COMPONENTS 無しの find_package は、その OCCT が持つモジュールの imported target を
  #   全部定義する ⇒ **TARGET の有無がそのまま「その機能が使えるか」**になる。
  if(SRAVA_OCCT_TEXT AND NOT TARGET TKV3d)
    message(FATAL_ERROR
      "SRAVA_OCCT_TEXT=ON だが この OCCT (${OpenCASCADE_VERSION}) に Visualization (TKV3d) が無い。\n"
      "  text op は StdPrs_BRepFont (libTKV3d・内部で FreeType) を使う。\n"
      "  OCCT 側を -DBUILD_MODULE_Visualization=ON -DUSE_FREETYPE=ON で建て直すか、\n"
      "  text op を落として良いなら -DSRAVA_OCCT_TEXT=OFF を **明示**すること。")
  endif()
  if(SRAVA_OCCT_STEP AND NOT TARGET TKDESTEP)
    message(FATAL_ERROR
      "SRAVA_OCCT_STEP=ON だが この OCCT (${OpenCASCADE_VERSION}) に DataExchange (TKDESTEP) が無い。\n"
      "  step/stp の import/export はそこにしか無い。\n"
      "  OCCT 側を -DBUILD_MODULE_DataExchange=ON で建て直すか、\n"
      "  brep だけで良いなら -DSRAVA_OCCT_STEP=OFF を **明示**すること。")
  endif()
  # ★ #3513: IGES は **TKDEIGES** (STEP の TKDESTEP とは別 toolkit)。⇒ 検査もフラグも別に持つ。
  #   OCCT 7.8 で DataExchange が toolkit ごとに割られたので、STEP だけ入っていて IGES が
  #   無いという構成が現に作れる。
  if(SRAVA_OCCT_IGES AND NOT TARGET TKDEIGES)
    message(FATAL_ERROR
      "SRAVA_OCCT_IGES=ON だが この OCCT (${OpenCASCADE_VERSION}) に IGES (TKDEIGES) が無い。\n"
      "  iges/igs の import/export はそこにしか無い。\n"
      "  OCCT 側を -DBUILD_MODULE_DataExchange=ON で建て直すか、\n"
      "  IGES を落として良いなら -DSRAVA_OCCT_IGES=OFF を **明示**すること。")
  endif()
  message(STATUS "OCCT: text op = ${SRAVA_OCCT_TEXT} / STEP 入出力 = ${SRAVA_OCCT_STEP} / IGES 入出力 = ${SRAVA_OCCT_IGES}")

  # ★ 機能ごとのソース・toolkit・マクロ。ここだけで出し入れが完結するようにしておく。
  set(OC_TEXT_SRC)       # codegen 用 (絶対パス)
  set(OC_TEXT_SRC_REL)   # srava_add_module 用 (相対パス)
  set(OC_TEXT_LIBS)
  set(OC_STEP_LIBS)   # ★ STEP / IGES の toolkit をここに積む (#3513)
  set(OC_FEATURE_DEFS)
  if(SRAVA_OCCT_TEXT)
    set(OC_TEXT_SRC     ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaText.cpp)
    set(OC_TEXT_SRC_REL modules/occt/c++/ocaText.cpp)
    set(OC_TEXT_LIBS    TKV3d)
    list(APPEND OC_FEATURE_DEFS SRAVA_OCCT_TEXT)
  endif()
  if(SRAVA_OCCT_STEP)
    set(OC_STEP_LIBS TKDESTEP TKXSBase)
    list(APPEND OC_FEATURE_DEFS SRAVA_OCCT_STEP)
  endif()
  if(SRAVA_OCCT_IGES)
    list(APPEND OC_STEP_LIBS TKDEIGES TKXSBase)   # ★ #3513 (TKXSBase は STEP と共用・重複は無害)
    list(APPEND OC_FEATURE_DEFS SRAVA_OCCT_IGES)
  endif()

  # ★ STEP 入出力 (TKDESTEP) を足すと、OCCT の CMake 設定が推移的に Visualization 側の
  #   リンク要求まで引いてきて **-lfontconfig** が現れる (OpenCASCADEVisualizationTargets.cmake が
  #   INTERFACE_LINK_LIBRARIES に素の名前 "fontconfig" を書いているため)。
  #   Debian には実体 libfontconfig.so.1 はあるが、**開発シンボリックリンク libfontconfig.so は
  #   -dev パッケージ側**なので、それが無い環境ではリンクが落ちる。
  #   ★ 実体を指す imported target を **同じ名前で先に定義**しておけば、CMake は素の名前を
  #     ターゲットとして解決するので -lfontconfig にならない。-dev の有無に依存しなくなる。
  #   (srava が可視化そのものを使うことは無い — text op が StdPrs_BRepFont を借りるだけなので、
  #    実体さえ繋がれば十分。SRAVA_OCCT_TEXT=OFF でも STEP 経由で同じ要求が出るため常に張る。)
  if(NOT TARGET fontconfig)
    find_library(SRAVA_FONTCONFIG_LIB NAMES fontconfig libfontconfig.so.1)
    if(SRAVA_FONTCONFIG_LIB)
      add_library(fontconfig UNKNOWN IMPORTED)
      set_target_properties(fontconfig PROPERTIES IMPORTED_LOCATION "${SRAVA_FONTCONFIG_LIB}")
      message(STATUS "OCCT: fontconfig を ${SRAVA_FONTCONFIG_LIB} へ束ねた (-dev 非依存)")
    endif()
  endif()

  tinystate_codegen(oc_ts_codegen
    # ⚠⚠ **登録は 2 か所** — ここ (codegen) と srava_add_module の SOURCES。
    #   codegen 側を落とすと **ビルドは通ってロードで undefined symbol**。
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaPtIntersection.cpp   # ★ #3581
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaPtDifference.cpp     # ★ #3581
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ptsocWireCacheStreamWriterShape.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ptsocWireCacheStreamReaderShape.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaBox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaPyramid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaCone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaTetrahedron.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaIcosphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaRect.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaNgon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaPolygon.cpp
    # ★ #3532: 格子から自由曲面 (A2 制御点 / A1 通過点 / 制御網の出し入れ)
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaSurfaceControl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaSurfaceThrough.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaPoles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaSetPoles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaCast.cpp          # ★ #3544
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaHlr.cpp           # ★ #3544 段 2
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaNedges.cpp        # ★ #3544 段 2
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaCircle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaEmpty2D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaEmpty3D.cpp
    # ★ #3461: 共通コア op (変換 + nverts)。boxa は ocaBox が兼ねる。
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaTranslate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaDistanceAt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaSection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaRotate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaScale.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaMirror.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaTransform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaNverts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaSphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaUnion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaIntersection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaDifference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaOffset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaOffsetThicken.cpp   # ★ #3547 ②
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaVert.cpp   # ★ #3547 ④
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaVerts.cpp   # ★ #3547 ④
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaFaceVerts.cpp   # ★ #3547 ④
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaVolume.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaBbox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaCentroid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaValid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaCylinder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaTorus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaTube.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaTubeRuled.cpp
    ${OC_TEXT_SRC}   # ★ SRAVA_OCCT_TEXT=OFF なら空 (ocaText.h も生成されない)
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaExtrude.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaRevolve.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaPrism.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaArea.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaFillet.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaChamfer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaImport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaNfaces.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaFace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaFaceAt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaSurfaceType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaProject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaProjectFlatten.cpp   # ★ #3534: z=0 への直投影
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaUnifyFaces.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaLoft.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocaLoftRuled.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/octsAgent.cpp
  )
  # ★ akira-project #3452: **幾何クラスを共有ライブラリへ出す**。
  #   occt.so と occt_mf.so (境界モジュール) が **同じ ocShape の実体**を共有する必要がある
  #   (各 .so がコピーを持つと in-proc で d_cast が失敗する = openvdb で踏んだのと同じバグ)。
  #   libsrava_vd / libsrava_mf / libsrava_gg と同じ構図。
  set(OC_GEOM_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ptsocWireCacheStreamWriterShape.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ptsocWireCacheStreamReaderShape.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocCacheCodec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocShape.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/c++/ocDrawing.cpp)   # ★ #3544 段 3: DXF / SVG
  add_library(srava_oc SHARED ${OC_GEOM_SRC})
  set_target_properties(srava_oc PROPERTIES OUTPUT_NAME "srava_oc" INSTALL_RPATH "$ORIGIN")
  srava_export_all(srava_oc)   # ★ Windows: 明示エクスポート (関数側のコメント参照)
  # ★★ 2026-09-07: ここは **幾何クラスだけ**の共有ライブラリ (ocShape / codec / wire)。
  #   ⚠ 以前は TKV3d を PUBLIC で付けていたが、OC_GEOM_SRC の 4 本は Visualization を
  #     1 行も触っていない (実際 --as-needed が libsrava_oc.so から落としていた)。
  #     TKV3d を要るのは text op = **モジュール側**なので、そちらへ移した。
  #   STEP は ocShape::write_to / read_file が本当に呼ぶので、ここに要る (任意)。
  # ★ #3547 ④: ocCacheCodec.cpp が &ptCloud::WIRE を並べる (点群型を借りる) ので srava_pt に依存する。
  #   ⚠ 借りているのは *幾何の機能* ではなく **値の器** — cgal (#3528) と同じ形。
  target_link_libraries(srava_oc PUBLIC pig srava_pt
    TKernel TKMath TKBRep TKPrim TKBO TKOffset TKTopAlgo TKMesh TKG3d TKGeomBase TKGeomAlgo TKShHealing
    TKFillet ${OC_STEP_LIBS})
  target_compile_definitions(srava_oc PRIVATE SRAVA_VALUE_ONLY ${OC_FEATURE_DEFS})
  target_include_directories(srava_oc PUBLIC ${SRAVA_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/h)
  add_dependencies(srava_oc pigts_codegen oc_ts_codegen gen_srava_parser)
  install(TARGETS srava_oc LIBRARY DESTINATION lib RUNTIME DESTINATION bin)

  # ★ #3581: 点群と B-rep の積と差 + **3 モジュール一致**。
  # ⚠ 一致の検定は manifold / openvdb / geomutils も要る (どれも既定 ON だが、
  #   構成を絞ったビルドでは落ちるので条件を書く)。
  if(SRAVA_MODULE_MANIFOLD AND SRAVA_MODULE_OPENVDB)
    add_test(NAME srava_ptsplit_occt
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_ptsplit_occt.sh $<TARGET_FILE:srava>)
    set_tests_properties(srava_ptsplit_occt PROPERTIES
      ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/ptsplitoccache"
      PASS_REGULAR_EXPRESSION "PTSPLITOC-OK"
      FAIL_REGULAR_EXPRESSION "FAIL:|PTSPLITOC_FAIL|Segmentation|Aborted|core dumped"
      TIMEOUT 300)
  endif()

  srava_add_module(occt_module
    OUTPUT   occt
    SOURCES  modules/occt/c++/ocaPtIntersection.cpp   # ★ #3581
             modules/occt/c++/ocaPtDifference.cpp     # ★ #3581
             modules/occt/c++/ocaBox.cpp
             modules/occt/c++/ocaPyramid.cpp
             modules/occt/c++/ocaCone.cpp
             modules/occt/c++/ocaTetrahedron.cpp
             modules/occt/c++/ocaIcosphere.cpp
             modules/occt/c++/ocaRect.cpp
             modules/occt/c++/ocaNgon.cpp
             modules/occt/c++/ocaPolygon.cpp
             modules/occt/c++/ocaSurfaceControl.cpp   # ★ #3532
             modules/occt/c++/ocaSurfaceThrough.cpp   # ★ #3532
             modules/occt/c++/ocaPoles.cpp            # ★ #3532
             modules/occt/c++/ocaSetPoles.cpp         # ★ #3532
             modules/occt/c++/ocaCast.cpp             # ★ #3544
             modules/occt/c++/ocaHlr.cpp              # ★ #3544 段 2 (陰線処理)
             modules/occt/c++/ocaNedges.cpp           # ★ #3544 段 2
             modules/occt/c++/ocaCircle.cpp
             modules/occt/c++/ocaEmpty2D.cpp
             modules/occt/c++/ocaEmpty3D.cpp
             modules/occt/c++/ocaSphere.cpp
             modules/occt/c++/ocaUnion.cpp
             modules/occt/c++/ocaIntersection.cpp
             modules/occt/c++/ocaDifference.cpp
             modules/occt/c++/ocaOffset.cpp
             modules/occt/c++/ocaOffsetThicken.cpp   # ★ #3547 ②
             modules/occt/c++/ocaVert.cpp   # ★ #3547 ④
             modules/occt/c++/ocaVerts.cpp   # ★ #3547 ④
             modules/occt/c++/ocaFaceVerts.cpp   # ★ #3547 ④
             modules/occt/c++/ocaVolume.cpp
    modules/occt/c++/ocaBbox.cpp
    modules/occt/c++/ocaCentroid.cpp
    modules/occt/c++/ocaValid.cpp
             modules/occt/c++/ocaCylinder.cpp
             modules/occt/c++/ocaTorus.cpp
             modules/occt/c++/ocaTube.cpp
             modules/occt/c++/ocaTubeRuled.cpp
             ${OC_TEXT_SRC_REL}
             modules/occt/c++/ocaExtrude.cpp
             modules/occt/c++/ocaRevolve.cpp
             modules/occt/c++/ocaPrism.cpp
             modules/occt/c++/ocaArea.cpp
             modules/occt/c++/ocaFillet.cpp
             modules/occt/c++/ocaChamfer.cpp
             modules/occt/c++/ocaExport.cpp
             modules/occt/c++/ocaImport.cpp
             modules/occt/c++/ocaNfaces.cpp
             modules/occt/c++/ocaFace.cpp
             modules/occt/c++/ocaFaceAt.cpp
             modules/occt/c++/ocaSurfaceType.cpp
             modules/occt/c++/ocaProject.cpp
             modules/occt/c++/ocaProjectFlatten.cpp
             modules/occt/c++/ocaUnifyFaces.cpp
             modules/occt/c++/ocaLoft.cpp
             modules/occt/c++/ocaLoftRuled.cpp
             modules/occt/c++/ocaNverts.cpp
             modules/occt/c++/ocaTranslate.cpp
             modules/occt/c++/ocaDistanceAt.cpp
             modules/occt/c++/ocaSection.cpp
             modules/occt/c++/ocaRotate.cpp
             modules/occt/c++/ocaScale.cpp
             modules/occt/c++/ocaMirror.cpp
             modules/occt/c++/ocaTransform.cpp
             modules/occt/c++/octsAgent.cpp
             modules/occt/manifest.cpp
    DEPENDS  pigts_codegen oc_ts_codegen gen_srava_parser
    LINK     srava_oc
             # ★ #3547 ④: verts が **点群型 (pt-cloud3d)** を作るので srava_pt に依存する。
             #   ⚠ 借りているのは *幾何の機能* ではなく **値の器** — 点群は外部ライブラリを
             #     持たない中立の型 (#3528) なのでモジュール境界の約束①に触れない
             #     (cgal / geogram / geomutils と同じ形)。
             srava_pt
             TKernel TKMath TKBRep TKPrim TKBO TKOffset TKTopAlgo TKMesh TKG3d TKGeomBase TKGeomAlgo TKShHealing
             TKFillet          # fillet / chamfer (#3437)
             # ★★ #3544 段 2: 陰線処理 (HLRBRep_Algo)。⚠ **option にしない** — 構成で op の
             #   有無が割れると本数が機体ごとに変わり、数字を並べるたびに但し書きが要る
             #   (NEF_SNC で実際にそうなった)。TKHLR は OCCT の ModelingAlgorithms の
             #   標準構成に入っているので、無ければリンクで **大きな音を立てて**落ちるのが正しい。
             TKHLR             # hlr (#3544)
             ${OC_STEP_LIBS}   # ★ STEP 入出力 (#3437) — SRAVA_OCCT_STEP=ON のときだけ
             ${OC_TEXT_LIBS}   # ★ text の StdPrs_BRepFont (#3471) — SRAVA_OCCT_TEXT=ON のときだけ
    DEFINES  ${OC_FEATURE_DEFS}
    INCLUDES ${SRAVA_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/h
    INSTALL)

  # ★ akira-project #3452: **occt ⇄ manifold の境界モジュール**。
  #   triangulate だけを持ち、**両側の本物のクラス** (ocShape / mfMesh) を使う。
  #   幾何クラスは libsrava_oc / libsrava_mf にあるので、ここには実体を持たない
  #   (持つと in-proc で d_cast が失敗する)。ライセンス: LGPL-2.1+例外 + Apache-2.0 (GPL 非混入)。
  #   ★ **occt.so 自身は manifold に依存しないまま** — この if() の中だけが manifold を要る。
  if(SRAVA_MODULE_MANIFOLD)
    tinystate_codegen(ocm_ts_codegen
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt_mf/c++/ocmTriangulate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt_mf/c++/ocmPolygonize.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt_mf/c++/ocmtsAgent.cpp)
    srava_add_module(occt_mf_module
      OUTPUT   occt_mf
      SOURCES  modules/occt_mf/c++/ocmCacheCodec.cpp
               modules/occt_mf/c++/ocmTriangulate.cpp
             modules/occt_mf/c++/ocmPolygonize.cpp
               modules/occt_mf/c++/ocmtsAgent.cpp
               modules/occt_mf/manifest.cpp
      DEPENDS  pigts_codegen oc_ts_codegen mf_ts_codegen ocm_ts_codegen gen_srava_parser
      LINK     srava_oc srava_mf
               TKernel TKMath TKBRep TKMesh TKG3d TKTopAlgo   # BRepMesh + Poly_Triangulation
      DEFINES  SRAVA_VALUE_ONLY
      INCLUDES ${SRAVA_GEN}
               ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt/h
               ${CMAKE_CURRENT_SOURCE_DIR}/modules/occt_mf/h
      INSTALL)

    add_test(NAME srava_module_occt_mf_so COMMAND srava_module_probe $<TARGET_FILE:occt_mf_module>)
    set_tests_properties(srava_module_occt_mf_so PROPERTIES
      PASS_REGULAR_EXPRESSION "MODULE_OK name=occt_mf .*make_agent=yes")
  endif()

  # ---- ★ #3498: OCCT の中断機構がライブラリ層で効くことの単体回帰。
  #   planner 経由の SIGINT (srava_teardown_sigint) では **配線が無くても通ってしまう** —
  #   occt は EXEC_PROCESS 専用なので #3417 の DM_CONT_KILL が agent ごと殺すため。
  #   ここでは srava_oc を直接叩き、*算法が自分で戻ってくる*ことを見る。
  #   ⚠ 時間を見る検査なので閾値は「素の所要に対する比」。TIMEOUT は素の所要の数倍を見込む。
  add_executable(test_occt_break test/occt_break_test.cpp)
  target_link_libraries(test_occt_break PRIVATE srava_oc)
  find_package(Threads REQUIRED)
  target_link_libraries(test_occt_break PRIVATE Threads::Threads)
  add_test(NAME occt_break COMMAND test_occt_break)
  set_tests_properties(occt_break PROPERTIES
    PASS_REGULAR_EXPRESSION "OCCT-BREAK-OK"
    FAIL_REGULAR_EXPRESSION "FAIL|OCCT-BREAK-FAIL|Segmentation|Aborted"
    TIMEOUT 300)

  add_test(NAME srava_module_occt_so COMMAND srava_module_probe $<TARGET_FILE:occt_module>)
  set_tests_properties(srava_module_occt_so PROPERTIES
    PASS_REGULAR_EXPRESSION "MODULE_OK name=occt .*make_agent=yes")
  add_test(NAME srava_type_occt COMMAND srava_module_probe $<TARGET_FILE:occt_module>)
  set_tests_properties(srava_type_occt PROPERTIES
    # ★ #3452: occt.so は **oc-brep3d だけ**を申告する。
    #   mf-mesh3d は occt_mf.so が (本物の mfMesh で) 扱う。
    PASS_REGULAR_EXPRESSION "TYPES oc-brep3d")

  # ---- OCCT の振る舞い回帰 (test/srava_occt.sh 冒頭に各モードの意図)。
  #   tri は出口のメッシュを下流 (manifold) で測るので SRAVA_MODULE_MANIFOLD が前提。
  set(_oc_modes exact steiner prim edit fatal contact)   # ★ fatal = OCCT の例外が srava のエラーになる (2026-08-26)
  #   ★ 2026-09-07: step モードは STEP 入出力を持つビルドだけ。
  if(SRAVA_OCCT_STEP)
    list(APPEND _oc_modes step)
  endif()
  #   ★ #3513: iges モードは **STEP と IGES の両方**を持つビルドだけ (球を 3 形式へ出して
  #     閉形式と突き合わせるので、step / iges / brep が揃っている必要がある)。
  if(SRAVA_OCCT_STEP AND SRAVA_OCCT_IGES)
    list(APPEND _oc_modes iges)
  endif()
  #   ★ contact = #3490 (2026-09-06)。境界だけで接する立体の融合が空洞を埋めないこと。
  if(SRAVA_MODULE_MANIFOLD)
    list(APPEND _oc_modes tri)
  endif()
  # ★ lossy = #3501 (2026-09-07)。BOPAlgo が黙って材料を落とす融合が明示エラーになること
  #   (かつ、正しい結果を誤検出で潰していないこと)。判定は「occt >= cgal」という
  #   カーネル間の不変条件なので **cgal が要る** (occt の球は厳密・cgal は内接多面体)。
  if(SRAVA_MODULE_CGAL)
    list(APPEND _oc_modes lossy)
  endif()
  foreach(_m IN LISTS _oc_modes)
    # ⚠ lossy だけ重い — **mac 実測 64.4 秒 > 予算 60 秒** (2026-09-22) で毎回撮られていた
    #   (Linux は 15.6 秒なので、こちらでは越えない = **機で足りる/足りないが変わる**例)。
    #   ★ loop 全体を伸ばすと軽い mode の捕獲が 3 倍遅れるので、mode ごとに分ける。
    set(_oc_hang 60)
    if(_m STREQUAL "lossy")
      set(_oc_hang 180)
    endif()
    add_test(NAME srava_occt_${_m}
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_occt.sh $<TARGET_FILE:srava> ${_m})
    set_tests_properties(srava_occt_${_m} PROPERTIES
      ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/occache-${_m};SRAVA_HANG_SECS=${_oc_hang}"
      PASS_REGULAR_EXPRESSION "OCCT-[A-Z]+-OK"
      FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
      TIMEOUT 300)
  endforeach()
  # ==================================================================
  # ★★ #3518 の 2: **面を取り出す** (face / face_at)。occt 固有 — メッシュ系の 2D は
  #   平面に縛られているので「円柱の側面を 2D 領域として持つ」ことができない。
  #   ⚠ 期待値に **巡回順そのものを焼き付けない** (面積を並べ替えてから比べる)。
  #     OCCT の内部順は版で変わりうるので、焼き付けると 7.9.3 で落ちる検査になる。
  # ==================================================================
  # ★★ #3532: 格子から自由曲面 — surface_through (通過点) / surface_control (制御点)。
  #   ★ 2 つは **同じ綴り**を受け取り点の意味だけが違うので、意味の違いそのものを検査する。
  add_test(NAME srava_occt_surface
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_occt_surface.sh
            $<TARGET_FILE:srava> occt.so 1e-6)
  set_tests_properties(srava_occt_surface PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/ocsurfcache;SRAVA_HANG_SECS=60"
    PASS_REGULAR_EXPRESSION "OCCT-SURFACE-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
  add_test(NAME srava_occt_face
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_occt_face.sh
            $<TARGET_FILE:srava> occt.so 1e-9)
  # ⚠ 番犬の予算 — **最も遅い機**の実測 x3 (2026-09-22)。mac(arm64/OCCT 7.9.3) 49.92s ・ Linux(x86_64/7.8.1) 20.68s ⇒ 180s。
  set_tests_properties(srava_occt_face PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/ocfacecache;SRAVA_HANG_SECS=180"
    PASS_REGULAR_EXPRESSION "OCCT-FACE-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
  # ★★★ #3544 段 2: **陰線処理 (HLR)**。閉形式だけで固定する (絵は見られない)。
  #   ★ 球が「ちょうど 1 本」になることが要 — 0 なら輪郭を落として球が消えており、
  #     2 なら継ぎ目 (seam) を図面に描いている。1 つの数が 2 つの誤りを捕まえる。
  add_test(NAME srava_occt_hlr
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_occt_hlr.sh
            $<TARGET_FILE:srava> occt.so)
  set_tests_properties(srava_occt_hlr PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/ochlrcache;SRAVA_HANG_SECS=60"
    PASS_REGULAR_EXPRESSION "OCCT-HLR-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
  # ★★★ #3544 段 3: **2D 図面の入出力** (DXF / SVG)。
  #   ★ 受け入れ条件は「円を書いて読み戻すと円のまま」。⚠ それだけでは「1 本でした」が
  #     何を意味するか決まらないので、**同じ円を折れ線で書いた対照** (cgal の書き手 = 64 本)
  #     を並べる。⇒ cgal.so を 3 引数で渡す (無ければ対照だけ飛ばして名前を言う)。
  if(SRAVA_MODULE_CGAL)
    set(OC_DRAW_CG "cgal.so")
  else()
    set(OC_DRAW_CG "")
  endif()
  # ★ ⑨ (occt → cgal の道) には occt_mf / manifold も要る。無ければ⑨だけ飛ばして名前を言う。
  if(SRAVA_MODULE_MANIFOLD)
    set(OC_DRAW_MF "occt_mf.so" "manifold.so")
  else()
    set(OC_DRAW_MF "")
  endif()
  add_test(NAME srava_occt_drawing
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_occt_drawing.sh
            $<TARGET_FILE:srava> occt.so ${OC_DRAW_CG} ${OC_DRAW_MF})
  set_tests_properties(srava_occt_drawing PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/ocdrawcache;SRAVA_HANG_SECS=60"
    PASS_REGULAR_EXPRESSION "OCCT-DRAWING-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
  # ★★ #3518 の 5: **掃引 (extrude / revolve) の検査**。符号つき体積が 0 になる掃引を
  #   明示エラーにする。⚠ 曲面と無関係に **元から在った穴** (平面の面を面内方向へ
  #   押し出しても 0 になる)。正しい掃引を潰していないことも同じ検査で見る。
  add_test(NAME srava_occt_sweep
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_occt_sweep.sh
            $<TARGET_FILE:srava> occt.so 1e-9)
  # ⚠ 番犬の予算 — **最も遅い機**の実測 x3 (2026-09-22)。mac(arm64/OCCT 7.9.3) 5.47s ・ Linux(x86_64/7.8.1) 2.49s ⇒ 20s。
  set_tests_properties(srava_occt_sweep PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/ocsweepcache;SRAVA_HANG_SECS=20"
    PASS_REGULAR_EXPRESSION "OCCT-SWEEP-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
  # ★★ #3511: **loft / loft_ruled**。断面の列を通る立体。検証は閉形式 (円柱・円錐台・角柱)。
  #   ★ 「なめらか」と「線織」が **本当に違う** ことまで検査する (同じなら区別が空振り)。
  add_test(NAME srava_occt_loft
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_occt_loft.sh
            $<TARGET_FILE:srava> occt.so 1e-9)
  # ⚠ 番犬の予算 — **最も遅い機**の実測 x3 (2026-09-22)。mac(arm64/OCCT 7.9.3) 15.44s ・ Linux(x86_64/7.8.1) 6.96s ⇒ 60s。
  set_tests_properties(srava_occt_loft PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/ocloftcache;SRAVA_HANG_SECS=60"
    PASS_REGULAR_EXPRESSION "OCCT-LOFT-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endif()

# ==================================================================
# ★ #3432 P0-b: **カーネル一致テストの雛形**。
#   カーネルを増やすたびに「同じモデルを複数カーネルで評価して突き合わせる」テストを手で
#   書いていた (sphere_kernel_agree / tube_kernel_agree / nef の agree モードが各々別の書き方)。
#   → 下の表に **1 行足すだけ**で新カーネルの一致検証が付くようにする。基準は常に cgal (厳密)。
#
#   srava_add_kernel_agree(<module.so> MODELS <model>:<許容相対誤差> ...)
#     許容誤差 0 = **完全一致**を要求 (共通生成器を共有している leaf の回帰)。
#     モデルの実体と誤差の考え方は test/srava_kernel_agree.sh 冒頭を参照。
# ==================================================================
function(srava_add_kernel_agree SO)
  cmake_parse_arguments(SKA "" "" "MODELS" ${ARGN})
  string(REPLACE ".so" "" _kn "${SO}")
  foreach(_spec IN LISTS SKA_MODELS)
    string(REPLACE ":" ";" _pair "${_spec}")
    list(GET _pair 0 _model)
    list(GET _pair 1 _tol)
    add_test(NAME srava_agree_${_kn}_${_model}
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_kernel_agree.sh
              $<TARGET_FILE:srava> ${_model} ${SO} ${_tol})
    set_tests_properties(srava_agree_${_kn}_${_model} PROPERTIES
      LABELS "agree"
      ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/agree-${_kn}-${_model}"
      PASS_REGULAR_EXPRESSION "AGREE"
      FAIL_REGULAR_EXPRESSION "MISMATCH|FAIL:|Segmentation|Aborted|core dumped|ERROR|parse error"
      TIMEOUT 120)
  endforeach()
endfunction()

# ---- カーネル一致の表 (新カーネルはここに 1 行足す) ----
#   基準 = cgal なので SRAVA_MODULE_CGAL が要件。各カーネルは自分の option の中で登録する。
if(SRAVA_MODULE_CGAL)
  if(SRAVA_MODULE_MANIFOLD)
    # 生成器を共有する leaf (common/geodesic.h) は完全一致・ブールと掃引は double 積算なので相対誤差。
    # ★ #3474 の基本立体 (common/solids.h) は **頂点が bit 一致**するが、体積の積み方が違うので
    #   最終桁だけ動く (cgal=厳密有理数を積んで最後に 1 回丸める / manifold=double で積む)。
    #   ⚠ box/sphere/icosphere が 0 で通っているのは **たまたま**なので、新しい立体を 0 で
    #     縛ると寸法を変えた瞬間に落ちる脆いテストになる (geogram の項と同じ理由)。
    srava_add_kernel_agree(manifold.so
      MODELS box:0 sphere:0 icosphere:0
             pyramid:1e-12 cylinder:1e-12 cone:1e-12 torus:1e-12 tetrahedron:1e-12
             prism:1e-12 importstl:1e-12
             # ★ 2D は 3D より緩い。manifold の CrossSection (Clipper2) は 3D の Manifold より
             #   精度が粗く、同じ図形でも下位桁が動く。
             #   ⚠ rect2d / polygon2d は寸法が整数なのでたまたま完全一致するが、そこで
             #     0 や 1e-12 を要求すると寸法を変えた瞬間に落ちる脆いテストになる
             #     (geogram の項と同じ理由)。
             rect2d:1e-8 ngon2d:1e-8 polygon2d:1e-8
             box_cut:1e-12 prism_cut:1e-9
             union:1e-8 difference:1e-8 intersection:1e-8
             tube3d:1e-8 tube2d:1e-8
             # ★ #3511: 凸包。hull3d / hull2d は閉形式で 9 / 3 ちょうど・hull_convex は
             #   icosphere2 と同じ値になる (凸なものの凸包はそれ自身)。
             hull3d:1e-12 hull_convex:1e-12 hull2d:1e-8)
  endif()
  if(SRAVA_MODULE_GEOGRAM)
    # geogram は厳密述語/厳密構成だが **結果の頂点は double** に落ちる (EPECK のように有理数を
    # 持ち回らない) ので相対誤差で見る。
    # ★ leaf の sphere も **完全一致にはならない**: 頂点は共通生成器 (geodesic.h) で cgal と
    #   bit 一致するが、体積の計算が違う (cgal=厳密有理数を積んで最後に 1 回丸める /
    #   geogram=発散定理を double で積む)。実測では下位桁だけが動く (manifold は cgal と
    #   一致したのでカーネルごとに違う = 「同じ頂点でも体積の積み方で下位桁は動く」)。
    #   ★ box も 0 にしてはいけない: 寸法によってはたまたま一致するが、別の寸法では
    #     最終桁がずれる (実測で確認済み)。
    #     「たまたま一致する寸法」で 0 を要求すると、寸法を変えた瞬間に落ちる脆いテストになる。
    srava_add_kernel_agree(geogram.so
      MODELS box:1e-12 sphere:1e-12 box_cut:1e-12
             pyramid:1e-12 cylinder:1e-12 cone:1e-12 torus:1e-12 tetrahedron:1e-12
             prism:1e-12 icosphere2:1e-12 importstl:1e-12 tube3d:1e-12
             union:1e-9 difference:1e-9 intersection:1e-9
             # ★ #3511: 凸包 (geogram は 2D 型を持たないので hull2d は入れない)。
             hull3d:1e-12 hull_convex:1e-12)
  endif()
  if(SRAVA_MODULE_OCCT)
    # ★ #3461: occt は **box だけ**を表に入れる。完全一致 (tol=0) する。
    #   ⚠ sphere / union / difference / intersection は **入れられない**。occt の球は
    #   *厳密な球面 1 枚* なのに対し、メッシュ系は *内接多面体* なので、体積が構造的に違う
    #   (occt の球は 4/3·π·r³ ちょうど / メッシュ系は内接多面体なので必ず小さい)。
    #   これは精度の問題ではないので、許容誤差を緩めて通すのは**偽のテスト**になる。
    #   seg を上げれば内接多面体が球に近づくだけで、一致はしない。
    #   ⇒ box (平面 6 枚・両表現で厳密) だけが「同じものを比べている」と言える。
    #   ★ #3474: **平面多面体だけ**を足せる。pyramid / tetrahedron は occt でも平面 Face の
    #   集まりなので同じ立体を比べられる (prism と同じ理由)。cylinder / cone / torus は occt では
    #   *解析曲面* なので内接多面体と構造的に体積が違う — sphere と同じ理由で入れない
    #   ⚠ 許容誤差は 0 にできない: 同じ多面体でも occt は B-rep から GProp で積むので
    #     最終桁が動く。
    #   ★ 2026-09-05: prism_cut を足した。occt の prism だけ **原点中心**で cgal / manifold の
    #   prism (z=0〜h) と食い違っていたのを揃えた回帰 (ひさ指示)。素の volume は平行移動で
    #   変わらないので検出できず、**原点中心の薄い箱と交差させて位置を値に出す**。
    #   ★ #3474 続き: icosphere は **測地多面体** (平面三角形の集まり) なので occt でも
    #   厳密に同じ立体になる — sphere と違って表に入れられる。prism も平面 n+2 枚。
    #   ⚠ importstl は入れない: occt が読めるのは STEP / BREP で、STL は読めない。
    #   ⚠ cylinder / cone / torus も入れない: occt は解析曲面で厳密なので、内接多面体で作る
    #     メッシュ系とは体積が **構造的に**違う (精度の問題ではないので誤差を緩めても意味が無い)。
    srava_add_kernel_agree(occt.so
      MODELS box:0 box_cut:1e-12 pyramid:1e-12 tetrahedron:1e-12
             prism:1e-12 icosphere2:1e-12 prism_cut:1e-9
             rect2d:1e-12 ngon2d:1e-12 polygon2d:1e-12)
  endif()
  if(SRAVA_MODULE_CHERCHI)
    # cherchi は厳密述語だが **結果の頂点は double** に落ちる (geogram と同じ精度クラス) ので
    # 相対誤差で見る。leaf を 0 にしない理由も geogram と同じ (体積の積み方が違うと下位桁が動く)。
    # ★ **intersection は表に入れない**。共通モデルが `box(2,2,2) &&& sphere(1.2,24)` = 球の中心が
    #   箱の角にあり、八面体種の球の頂点が **箱の面の上にちょうど載る** (測度 0 の接触) 配置で、
    #   cherchi はここで **開いた曲面**を作る (modules/cherchi/c++/chMesh.cpp の境界辺検査が弾く)。
    #   ⚠ 検査を入れる前はこのテストは**通っていた** — 落ちた面が原点を通る平面上にあり、
    #     発散定理の寄与が 0 だったので **体積だけは合っていた**ため。値が合うことと結果が
    #     正しいことは別、という実例なので、ここは通さずに落とす方を選ぶ。
    #   op 自体の検証は srava_cherchi_bool (一般の位置の箱で union/intersection/difference の
    #   値を厳密に確かめる) が担う。
    srava_add_kernel_agree(cherchi.so
      MODELS box:1e-12 sphere:1e-12 box_cut:1e-12
             pyramid:1e-12 cylinder:1e-12 cone:1e-12 torus:1e-12 tetrahedron:1e-12
             prism:1e-12 icosphere2:1e-12 importstl:1e-12 tube3d:1e-12
             union:1e-9 difference:1e-9)
  endif()
  if(SRAVA_MODULE_NEF)
    # 厳密同士。leaf は完全一致まで通る。ブールは分割規約が違うので最終丸めの下位桁だけ許す。
    # ★ 2026-09-05: nef にも tube を入れた (掃引の本体は common/tube.h が持つのでカーネル側の
    #   実装は Sink 接続だけで済む) ので tube3d を表に足した。
    if(SRAVA_MODULE_NEF_SNC)
      srava_add_kernel_agree(nef_snc.so
        MODELS box:0 sphere:0 box_cut:1e-12
               pyramid:0 cylinder:0 cone:0 torus:0 tetrahedron:0
               prism:0 icosphere2:0 importstl:1e-12 tube3d:1e-12
               union:1e-12 difference:1e-12 intersection:1e-12
               # ★ #3511: 凸包 (nef は 2D 型を持たないので hull2d は入れない)。
               hull3d:1e-12 hull_convex:1e-12)
    endif()
    if(SRAVA_MODULE_NEF_HYBRID)
      srava_add_kernel_agree(nef_hybrid.so
        MODELS box:0 sphere:0 box_cut:1e-12
               pyramid:0 cylinder:0 cone:0 torus:0 tetrahedron:0
               prism:0 icosphere2:0 importstl:1e-12 tube3d:1e-12
               union:1e-12 difference:1e-12 intersection:1e-12
               hull3d:1e-12 hull_convex:1e-12)
    endif()
  endif()
endif()

# ==================================================================
# ★ #3511: hull (凸包) の **閉形式**回帰。kernel_agree は「cgal と一致するか」しか見ないので、
#   基準の cgal 自身が正しいことは別に固定する必要がある。ここは真値を直接書く。
#   ★ 2026-09-12: 退化検査 (flat) を **nef にも付けた**。#3516 で box の寸法検査が入り、
#     退化入力の作り方が「平らな STL の import」へ変わったので、nef でも踏めるようになった
#     (以前は box(1,1,0) 自体が SIGSEGV していて付けられなかった)。
#   ⚠ 2D 検査は 2D 型を持つ cgal / manifold だけ。
# ==================================================================
function(srava_add_hull _so _tol _extra)
  string(REPLACE ".so" "" _hn "${_so}")
  add_test(NAME srava_hull_${_hn}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_hull.sh
            $<TARGET_FILE:srava> ${_so} ${_tol} "${_extra}")
  set_tests_properties(srava_hull_${_hn} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/hullcache-${_hn}"
    PASS_REGULAR_EXPRESSION "HULL-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|HULL-FAIL|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_CGAL)
  srava_add_hull(cgal.so 1e-12 "2d,flat")
endif()
if(SRAVA_MODULE_MANIFOLD)
  srava_add_hull(manifold.so 1e-12 "2d,flat")
endif()
if(SRAVA_MODULE_GEOGRAM)
  # geogram は結果の頂点が double なので相対誤差を緩める (agree の項と同じ理由)。
  srava_add_hull(geogram.so 1e-9 "flat")
endif()
if(SRAVA_MODULE_NEF AND SRAVA_MODULE_NEF_SNC)
  srava_add_hull(nef_snc.so 1e-12 "flat")
endif()
if(SRAVA_MODULE_NEF AND SRAVA_MODULE_NEF_HYBRID)
  srava_add_hull(nef_hybrid.so 1e-12 "flat")
endif()

# ==================================================================
# ★ #3512: refine / remesh / simplify の **閉形式**回帰。値はすべて真値を直接書く。
#   ★ 一番効くのは「形を変えない」こと — remesh は鋭角エッジを保護しないと単位箱が
#     体積 0.999107 / 面積 5.98811 になり (角が削れる)、simplify は体積保存の制約を
#     解かない実装だと面数を落とすたびに体積が動く。どちらも **黙って形が変わる**ので、
#     体積/面積が 1 / 6 のままであることを固定する。
#   ★ "angle" を付けると **最小角の改善**まで見る (OFF を書き出して測る。1x1x0.02 の板で
#     1.15° → 14.87°)。srava に角度を測る op が無いのでテスト側で計算している。
#   ★ refine は cgal / manifold の 2 実装。**約束は同じ** (形は不変・全辺が len 以下) が
#     作り方が違うので **面数は一致しない** (箱 len=0.25 で cgal 432 / manifold 312)。
#     ⇒ 面数では固定せず「増えたか」「辺が len 以下か」で見る。
#   ⚠ remesh / simplify は cgal だけ。geogram / manifold のライブラリは **同じ約束にできない**
#     ことを #3512 で実測した (docs/srava_module_reference.md の各節に数値)。
# ==================================================================
function(srava_add_remesh _so _tol _extra)
  string(REPLACE ".so" "" _rn "${_so}")
  add_test(NAME srava_remesh_${_rn}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_remesh.sh
            $<TARGET_FILE:srava> ${_so} ${_tol} "${_extra}")
  set_tests_properties(srava_remesh_${_rn} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/rmcache-${_rn}"
    PASS_REGULAR_EXPRESSION "REMESH-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|REMESH-FAIL|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_CGAL)
  srava_add_remesh(cgal.so 1e-12 "rs,angle,edge")
endif()
if(SRAVA_MODULE_MANIFOLD)
  # ★ manifold は refine だけ (remesh / simplify は持たない = #3512 で実測して見送った)。
  #   ⚠ 許容は緩める — manifold は体積を double で積むので箱の 1 / 6 が最下位桁でずれる。
  srava_add_remesh(manifold.so 1e-9 "edge,cleanup")
endif()

# ==================================================================
# ★ #3516: 退化した / 負のプリミティブ寸法が **明示エラー**になることの回帰。
#   ★ 見ているのは検査そのものではなく **カーネル間で歯抜けにならないこと**。
#     実測 (2026-09-12) では「寸法は > 0」を box / sphere は 2/7 しか持っておらず、
#     その結果 nef は box(1,1,0) で SIGSEGV し、box(-1,1,1) は 4 カーネルで 4 通りの
#     違う値を黙って返していた。
#   ⚠ openvdb は入れない — 引数の並びが違う (末尾に dx)。もともと 7/7 の側。
#   ★ 2026-09-12 続き: 2D (ngon / circle / rect / polygon / extrude / revolve) と tube / transform /
#     scale も足した。2D 系列は 2D 型を持つ cgal / manifold / occt だけ ("2d" を渡す)。
#     ⚠ tube は **元から 7/7** (検査は共通ヘッダ src/h/common/tube.h にある)。歯抜けではないが、
#       共通ヘッダが退行したら全カーネルが同時に壊れるので、ここで固定しておく。
# ==================================================================
function(srava_add_degenerate _so _extra)
  string(REPLACE ".so" "" _dn "${_so}")
  add_test(NAME srava_degenerate_${_dn}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_degenerate.sh
            $<TARGET_FILE:srava> ${_so} "${_extra}")
  # ⚠ 番犬の予算 — **occt の本だけ重い** (mac 12.85s / Linux 5.64s ⇒ 最も遅い機 x3 = 45s)。
  set(_dg_hang 30)
  if(_dn STREQUAL "occt")
    set(_dg_hang 45)
  endif()
  set_tests_properties(srava_degenerate_${_dn} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/dgcache-${_dn};SRAVA_HANG_SECS=${_dg_hang}"
    PASS_REGULAR_EXPRESSION "DEGEN-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|DEGEN-FAIL|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_CGAL)
  srava_add_degenerate(cgal.so "2d")
endif()
if(SRAVA_MODULE_MANIFOLD)
  srava_add_degenerate(manifold.so "2d")
endif()
if(SRAVA_MODULE_GEOGRAM)
  srava_add_degenerate(geogram.so "")
endif()
if(SRAVA_MODULE_CHERCHI)
  srava_add_degenerate(cherchi.so "")
endif()
if(SRAVA_MODULE_OCCT)
  srava_add_degenerate(occt.so "2d")
endif()
if(SRAVA_MODULE_NEF AND SRAVA_MODULE_NEF_SNC)
  srava_add_degenerate(nef_snc.so "")
endif()
if(SRAVA_MODULE_NEF AND SRAVA_MODULE_NEF_HYBRID)
  srava_add_degenerate(nef_hybrid.so "")
endif()

# ==================================================================
# ★ #3511: minkowski の **閉形式**回帰。
#   ⚠ kernel_agree には入れられない — あちらは基準が常に cgal で、**cgal.so は minkowski を
#     持たない** (CGAL の minkowski_sum_3 は Nef_polyhedron_3 の上の関数なので持ち主は nef)。
#   ⇒ 真値 (箱⊕箱の厳密値 / Steiner の上界) と直接突き合わせる。
# ==================================================================
function(srava_add_minkowski _so _tol)
  string(REPLACE ".so" "" _mn "${_so}")
  add_test(NAME srava_minkowski_${_mn}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_minkowski.sh
            $<TARGET_FILE:srava> ${_so} ${_tol})
  # ⚠ 番犬の予算 — nef 系の 2 本が重い (最も遅い機 = mac の実測 x3)。
  #     nef_snc     mac 5.01s / Linux 1.19s ⇒ 20s
  #     nef_hybrid  mac 4.10s / Linux 1.11s ⇒ 15s
  #   manifold は mac でも軽いので既定 10 秒のまま。
  set(_mk_hang "")
  if(_mn STREQUAL "nef_snc")
    set(_mk_hang ";SRAVA_HANG_SECS=20")
  elseif(_mn STREQUAL "nef_hybrid")
    set(_mk_hang ";SRAVA_HANG_SECS=15")
  endif()
  set_tests_properties(srava_minkowski_${_mn} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/mkcache-${_mn}${_mk_hang}"
    PASS_REGULAR_EXPRESSION "MINKOWSKI-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|MINKOWSKI-FAIL|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_MANIFOLD)
  srava_add_minkowski(manifold.so 1e-9)
endif()
if(SRAVA_MODULE_NEF AND SRAVA_MODULE_NEF_SNC)
  srava_add_minkowski(nef_snc.so 1e-12)
endif()
if(SRAVA_MODULE_NEF AND SRAVA_MODULE_NEF_HYBRID)
  srava_add_minkowski(nef_hybrid.so 1e-12)
endif()

# ==================================================================
# ★ #3486: アフィン変換 4 op (rotate / scale / mirror / transform) の **位置と向き**。
#   ★★ 体積は剛体変換で不変なので kernel_agree (体積の突き合わせ) では **置き場所も向きも
#     検出できない** — #3474 で occt の box / prism の食い違いを 2 件見逃した理由がこれ。
#     ⇒ 変換後の立体の内部に小さな箱を置いて切り取り、位置を値に出す (box_cut と同じ手)。
#   ★ ここは全カーネルに **同じスクリプト**を当てる。7 本が同じ書き方を受け付け、同じ理由で
#     断ることを見張るのが目的 (解釈は src/h/common/affine.h に集約した)。
#   ⚠ openvdb だけ生成 op の末尾に dx が要る (第 4 引数)。許容誤差もボクセル近似なので緩い。
#   ⚠ 第 5 引数 "cg" = 向きを cgal 側の**符号付き**体積で検算する。geogram の volume() は
#     絶対値を返すので、これが無いと面が裏返っていても全検査を通ってしまう (実測で確認)。
#     occt (cg への cast 経路が無い) と openvdb (距離場なので面の向きが無い) は付けない。
# ==================================================================
function(srava_add_affine _name _so _tol _dx _cg)
  add_test(NAME srava_affine_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_affine.sh
            $<TARGET_FILE:srava> ${_so} ${_tol} "${_dx}" "${_cg}")
  # ⚠ 番犬の予算 — **occt の本だけ重い** (mac 8.99s / Linux 4.97s ⇒ 最も遅い機 x3 = 30s)。
  #   他の本は既定 10 秒のまま (srava_hangwatch.sh)。
  set(_aff_hang "")
  if(_name STREQUAL "occt")
    set(_aff_hang ";SRAVA_HANG_SECS=30")
  endif()
  set_tests_properties(srava_affine_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/affcache-${_name}${_aff_hang}"
    PASS_REGULAR_EXPRESSION "AFFINE-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_CGAL)
  # ★ cgal が居るときだけ ⑭ (cg へ cast して符号付き体積で向きを検算) を有効にする。
  set(_aff_cg "cg")
else()
  set(_aff_cg "")
endif()
if(SRAVA_MODULE_CGAL)
  srava_add_affine(cgal cgal.so 1e-9 "" "${_aff_cg}")
endif()
if(SRAVA_MODULE_MANIFOLD)
  srava_add_affine(manifold manifold.so 1e-9 "" "${_aff_cg}")
endif()
if(SRAVA_MODULE_OCCT)
  srava_add_affine(occt occt.so 1e-9 "" "")
endif()
if(SRAVA_MODULE_NEF AND SRAVA_MODULE_CGAL)
  if(SRAVA_MODULE_NEF_SNC)
    srava_add_affine(nef_snc nef_snc.so 1e-9 "" "${_aff_cg}")
  endif()
  if(SRAVA_MODULE_NEF_HYBRID)
    srava_add_affine(nef_hybrid nef_hybrid.so 1e-9 "" "${_aff_cg}")
  endif()
endif()
if(SRAVA_MODULE_GEOGRAM)
  srava_add_affine(geogram geogram.so 1e-9 "" "${_aff_cg}")
endif()
if(SRAVA_MODULE_CHERCHI)
  srava_add_affine(cherchi cherchi.so 1e-9 "" "${_aff_cg}")
endif()
if(SRAVA_MODULE_OPENVDB)
  # ボクセル (dx=0.05) なので許容誤差は 2%。生成 op の末尾に dx が要る。
  srava_add_affine(openvdb openvdb.so 2e-2 0.05 "")
endif()

# ==================================================================
# ★★ #3518: **2D (cross2d) のアフィン変換**。3D の回帰 (上) は 2D の経路を 1 行も通らない
#   ので、2D を持つ 3 カーネル (cgal / manifold / occt) に同じスクリプトを当てる。
#   ★ occt は #3518 で 2D を受けるようになった (cgal / manifold は元から持っていた = 歯抜け)。
#   ★★ 面積は剛体変換で不変なので、**extrude して内部に probe を置いて切り取る**
#     (srava_affine.sh と同じ手。area だけ見ていると何も動いていなくても通る)。
#   ⚠ 第 4 引数は **能力の宣言**: "" 置けない / "space" 置ける / "oc" space + occt 固有。
#     ★ #3526 で manifold と cgal の 2D が **枠 (平面)** を持ったので、3 カーネルとも "space"
#       以上になった ("" を使う行はもう無い)。項が空振りしないよう occt だけ "oc" のまま。
# ==================================================================
# ==================================================================
# ★★ #3533: **平面 → 枠 (O,U,V) の表を 1 箇所に保つ**柵。ソースを数える検査。
#   ⚠ 値の検査では書けない — bbox / centroid を world にしたので **局所座標は言語から観測
#     できない** (面積も周長も不変・DXF は法線から OCS を作り直す)。⇒ 表が食い違っても
#     値の検査は緑のまま通る。守りたいのは「枠を作ってよいのは 1 箇所・ほかは運ぶだけ」。
#   ★ 較正済み (2026-09-15): mfaSection の呼び出しを手書きの表に戻すと ②③ が赤くなる。
#   ⚠ モジュールのビルド有無に依らないのでいつでも回る (srava 実行体を使わない)。
# ==================================================================
add_test(NAME srava_frame_table
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_frame_table.sh ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(srava_frame_table PROPERTIES
  PASS_REGULAR_EXPRESSION "FRAMETABLE-OK"
  FAIL_REGULAR_EXPRESSION "FRAME_FAIL"
  TIMEOUT 120)

# ==================================================================
# ★★ #3533: **2D 領域の型が 2 つに割れた** (*-cross2d = z=0 の簡易表現 / *-face3d = 空間に
#   置かれた一般表現) ことの回帰。規約 4 つを 1 つずつ見る (詳細は test/srava_face3d.sh の冒頭)。
#   ★ 主眼は **bbox が経路に依らないこと** — x まわり 180 度 と 90 度 x 2 は world では同じ
#     図形なので、答えも同じでなければならない。#3533 以前は局所座標で答えていたので割れていた。
#   ★ 2026-09-15 に **負の対照を 2 本**取った (どちらも 3 件赤くなる):
#       (a) bbox を局所へ戻す        → ★ の行が [[0,-3],[2,0]] 対 [[0,0],[2,3]] に戻る
#       (b) 型をブロブに書かない     → face3d が読み直しで cross2d に戻る
#     ⚠ (b) は「cold と warm が一致」の検査では **捕まらない** (両方とも等しく壊れる)。
#   ⚠ occt は対象外 — oc-face3d しか無い (z=0 の簡易表現という状態が存在しない)。
# ==================================================================
function(srava_add_face3d _name _so _pfx)
  add_test(NAME srava_face3d_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_face3d.sh
            $<TARGET_FILE:srava> ${_so} ${_pfx})
  set_tests_properties(srava_face3d_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/f3dcache-${_name};SRAVA_HANG_SECS=30"
    PASS_REGULAR_EXPRESSION "FACE3D-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_CGAL)
  srava_add_face3d(cgal cgal.so cg)
endif()
if(SRAVA_MODULE_MANIFOLD)
  srava_add_face3d(manifold manifold.so mf)
endif()
# ★★ #3544 (2026-09-17): **occt も対象にした**。上の「occt は対象外」は #3533 の読みで、
#   陰線処理 (HLR) の出力が z=0 に乗る (実測) と分かって覆った。⇒ oc-cross2d を型として戻し、
#   この検定 (規約 4 つ + bbox/centroid の形) を **他カーネルと同じ 1 本**で見る。
#   ★ この検査は #3533 のときに *別のカーネル向けに* 書かれたものなので、
#     実装に合わせて書いた検査ではない = 受け入れ条件として強い。
if(SRAVA_MODULE_OCCT)
  srava_add_face3d(occt occt.so oc)
endif()

# ==================================================================
# ★★ #3534: **project_flatten** — 空間に置かれた 2D を z=0 へ落とす。
#   ★ 検査の芯は **経路非依存** — 取り下げた案 A (枠の局所座標をそのまま (x,y) にする) は
#     同じ world 図形でも式の書き方で結果が変わる。他の項は案 A でも全部通るので、
#     ⑤ が無いと 2 案を区別できない。
#   ⚠ occt は曲線の境界だけ近似になる (BRepAlgoAPI_Common が 平面 ∩ 円柱 を BSpline に
#     落とす・実測 7.2e-6) ので、第 3 引数で分ける。⚠ occt は **型が変わらない** (#3533)。
# ==================================================================
function(srava_add_project_flatten _name _so _kind)
  add_test(NAME srava_project_flatten_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_project_flatten.sh
            $<TARGET_FILE:srava> ${_so} "${_kind}")
  set_tests_properties(srava_project_flatten_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/pflatcache-${_name};SRAVA_HANG_SECS=30"
    PASS_REGULAR_EXPRESSION "PROJFLAT-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_CGAL)
  srava_add_project_flatten(cgal cgal.so "")
endif()
if(SRAVA_MODULE_MANIFOLD)
  srava_add_project_flatten(manifold manifold.so "")
endif()
if(SRAVA_MODULE_OCCT)
  srava_add_project_flatten(occt occt.so "oc")
endif()

function(srava_add_affine2d _name _so _tol _oc)
  add_test(NAME srava_affine2d_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_affine2d.sh
            $<TARGET_FILE:srava> ${_so} ${_tol} "${_oc}")
  # ⚠ 番犬の予算 — **occt の本だけ重い** (mac 24.67s / Linux 9.63s ⇒ 最も遅い機 x3 = 90s)。
  #   一族をまとめて伸ばすと軽い本の捕獲が遅れるので、この 1 本だけ分ける (2026-09-22)。
  set(_a2d_hang 30)
  if(_name STREQUAL "occt")
    set(_a2d_hang 90)
  endif()
  set_tests_properties(srava_affine2d_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/aff2dcache-${_name};SRAVA_HANG_SECS=${_a2d_hang}"
    PASS_REGULAR_EXPRESSION "AFFINE2D-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

# ==================================================================
# ★★ #3511: **loft_ruled** (メッシュ系)。断面の列を直線で結ぶ立体。
#   ★ occt の srava_occt_loft.sh とは別 — メッシュ系は **loft (なめらか) を持たない**ので
#     期待値の表がそもそも別。加えて **ねじれ (面内回転) の答えが occt と違う**ことを
#     検査に固定してある (ひさ判断: そのまま受ける・2026-09-13)。
#   ⚠ 許容は相対。manifold の 2D は cache から decode すると Clipper2 の格子に載るので、
#     同じ式でも断面の座標が下位桁で動く (loft の欠陥ではなく 2D 型の性質)。
#   ★ cgal は #3526 の枠を移してから (いまは 2D を面外へ置けないので断面を空間に置けない)。
# ==================================================================
function(srava_add_loft_ruled _name _so _tol)
  add_test(NAME srava_loft_ruled_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_loft_ruled.sh
            $<TARGET_FILE:srava> ${_so} ${_tol})
  set_tests_properties(srava_loft_ruled_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/loftcache-${_name};SRAVA_HANG_SECS=60"
    PASS_REGULAR_EXPRESSION "LOFT-RULED-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_MANIFOLD)
  srava_add_loft_ruled(manifold manifold.so 1e-7)
endif()

if(SRAVA_MODULE_CGAL)
  # ★ cgal の 2D は厳密なので manifold より 2 桁きつく見られる (2D が Clipper2 の格子に
  #   載らないため)。⇒ 同じスクリプトを厳しい許容で当てて、期待値が両カーネルで同じことを見る。
  srava_add_loft_ruled(cgal cgal.so 1e-9)
endif()

# ==================================================================
# ★★ #3529: **cache を経由しても同じ値になること** (同じ式が経路で違う値を返さない)。
#   manifold の 2D は decode が CrossSection(ps,NonZero) で作り直しており、構成子が Clipper2 の
#   Union を通るので座標が整数格子に載っていた ⇒ *その場の実体* と *decode した実体* で値が違う。
#   ⚠ これは性能でなく **正しさ**の問題 — content-addressed cache は「同じ入力なら同じ結果」を
#     前提に値を再利用する仕組みなので、hit と miss で答えが変わるのは約束そのものに触る。
#   ⚠⚠ 素朴に「同じ式を 2 回流して比べる」は **検定にならない** (2 回目は終端 op 自体が
#     cache hit するので同じ値が出て当たり前)。⇒ 焼くときの終端と比べるときの終端を **変える**。
#   ★ 2D だけでなく 3D も横断する (3D は無罪と実測済みだが、将来 manifold 側が変わったら赤くなる)。
#   ★ カーネルごとの 2D op の歯抜けは **飛ばして名前を出す** — 「型が無い」と「本当の歯抜け」を
#     混ぜると表が嘘になる (#3510 の教訓)。
# ==================================================================
function(srava_add_cache_path _name _so _has2d)
  add_test(NAME srava_cache_path_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_cache_path.sh
            $<TARGET_FILE:srava> ${_so} ${_has2d})
  set_tests_properties(srava_cache_path_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/cpathcache-${_name};SRAVA_HANG_SECS=60"
    PASS_REGULAR_EXPRESSION "CACHEPATH-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_MANIFOLD)
  srava_add_cache_path(manifold manifold.so 1)
endif()
if(SRAVA_MODULE_CGAL)
  srava_add_cache_path(cgal cgal.so 1)
endif()
if(SRAVA_MODULE_OCCT)
  srava_add_cache_path(occt occt.so 1)
endif()

# ==================================================================
# ★★ #3537: **valid は回転で変わらない** — 共有述語 self_intersects の誤検出の回帰。
#   @revolve@ で作った立体を回すと manifold の valid が 0 になっていた。誤検出していたのは
#   manifold でも cgal でもなく src/h/common/meshprops.h の *こちらの double 述語*。
#   ★ @revolve@ の円環の蓋には中心頂点が無いので「非隣接なのに同一平面」の三角形対を持つ。
#     @cylinder@ の蓋は扇で全部隣接扱いになるため、この穴を踏むのは revolve だけだった。
#   ⚠ 検査は **不変量の形**にしてある (角度の期待値を並べない) — 起票時の「厳密に表せる角度
#     なら通る」という見立てが再測で崩れており、角度を並べると別の角度で落ちる版が素通りする。
#   ★★ 反対側 (自己交差した立体は回しても valid=0) を必ず置く。片側だけだと
#     「述語が常に 0 を返す」版が満点で通ってしまう。
#   ★ cgal は **厳密述語**なので独立した対照になる (両方が同じ答えを出すことに意味がある)。
#     occt は自分の B-rep で判定するので、これも独立した対照。
#     ⚠ occt の tube は自己交差する背骨を掃引そのものが断るので、反対側は "-" (作れない)。
# ==================================================================
function(srava_add_selfintersect _name _so _selfx)
  add_test(NAME srava_selfintersect_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_selfintersect.sh
            $<TARGET_FILE:srava> ${_so} "${_selfx}")
  set_tests_properties(srava_selfintersect_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/selfxcache-${_name};SRAVA_HANG_SECS=60"
    PASS_REGULAR_EXPRESSION "SELFX-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_MANIFOLD)
  srava_add_selfintersect(manifold manifold.so 0)
endif()
if(SRAVA_MODULE_CGAL)
  srava_add_selfintersect(cgal cgal.so 0)
endif()
if(SRAVA_MODULE_OCCT)
  srava_add_selfintersect(occt occt.so "-")
endif()

# ==================================================================
# ★★ #3530: **segs (分割数) / n (辺数) の規約を op x カーネルで数える**。
#   起票時に数えたら **4 通りの規約が併存**していた (sphere 族 / revolve / ngon 族 /
#   circle が cgal と manifold で別) — @circle(1,0)@ が cgal で三角形・manifold で 32 角形
#   になり、**エラーも警告も無く**体積が 2.4 倍違う答えを返していた。
#   ⚠ これは op を 1 つずつ見ても見えない。表にして初めて出た ⇒ **表を検査にする**。
#   規約は src/h/common/segs.h に 1 本:
#     segs (近似の細かさ・既定値あり)  省略 と 0 は同じ / 1,2,負 は明示エラー / 3 以上 は その値
#     n    (形そのもの・既定値なし)    3 未満 (0 を含む) は明示エラー
#   ★ occt は segs を **無視する**が検査はする (第 5 引数 0) — しないと同じ式が cg/mf で
#     落ちて occt で通る、という新しい食い違いになる。
#   ★ openvdb は末尾の dx が必須なので segs を位置的に省略できない (第 6 引数で dx を渡す)。
#     ⇒ 「省略」の比較だけ行わない。0 = 未指定 の意味は同じ。
# ==================================================================
function(srava_add_segs _name _so _segs_ops _n_ops _honors _tail)
  add_test(NAME srava_segs_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_segs.sh
            $<TARGET_FILE:srava> ${_so} "${_segs_ops}" "${_n_ops}" ${_honors} "${_tail}")
  # ⚠ 番犬の予算 — **occt の本だけ重い** (mac 14.17s / Linux 5.40s ⇒ 最も遅い機 x3 = 45s)。
  set(_segs_hang 30)
  if(_name STREQUAL "occt")
    set(_segs_hang 45)
  endif()
  set_tests_properties(srava_segs_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/segscache-${_name};SRAVA_HANG_SECS=${_segs_hang}"
    PASS_REGULAR_EXPRESSION "SEGS-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_CGAL)
  srava_add_segs(cgal     cgal.so     "circle,sphere,cylinder,cone,torus,tube_ruled,revolve" "ngon,prism,pyramid" 1 "")
endif()
if(SRAVA_MODULE_MANIFOLD)
  srava_add_segs(manifold manifold.so "circle,sphere,cylinder,cone,torus,tube_ruled,revolve" "ngon,prism,pyramid" 1 "")
endif()
if(SRAVA_MODULE_OCCT)
  # ★★ #3570 段3: occt は segs を **そもそも取らない** (記述子から撤去した) ので none。
  #   以前は 0 (受けるが無視し、検査はする = #3530) だった。
  #   ★ #3593: **tube_ruled も並べる** — occt も持つようになったので、「渡すとエラー・省略すると値」
  #     をここで実地に見る。これが下の segs_coverage の除外の**根拠**になる (除外を申告だけで
  #     済ませない = 「取らないはず」を実際に確かめる本が在る状態にする)。
  srava_add_segs(occt     occt.so     "circle,sphere,cylinder,cone,torus,tube,tube_ruled,revolve" "ngon,prism,pyramid" none "")
endif()
if(SRAVA_MODULE_CHERCHI)
  srava_add_segs(cherchi  cherchi.so  "sphere,cylinder,cone,torus,tube_ruled" "prism,pyramid" 1 "")
endif()
if(SRAVA_MODULE_GEOGRAM)
  srava_add_segs(geogram  geogram.so  "sphere,cylinder,cone,torus,tube_ruled" "prism,pyramid" 1 "")
endif()
if(SRAVA_MODULE_NEF_SNC)
  srava_add_segs(nef_snc  nef_snc.so  "sphere,cylinder,cone,torus,tube_ruled" "prism,pyramid" 1 "")
endif()
if(SRAVA_MODULE_OPENVDB)
  # ★ openvdb は sphere(r,dx) — 第 2 引数が分割数でなくボクセルサイズなので segs op に入らない
  srava_add_segs(openvdb  openvdb.so  "cylinder,cone,torus,tube_ruled" "prism,pyramid" 1 ",0.05")
endif()

# ★★ #3530: **検査の歯抜けを機械で数える** (test/srava_segs_coverage.sh)。
#   共通検査を 1 本置いても、新しい op を足した人がそこを通し忘れたら元に戻る。
#   ⚠ 戻ったことは目視では見えない ⇒ 数える検査を常時走らせる (srava_hangwatch_coverage と同型)。
#   ★ モジュール構成に依らない (srava を起動しない) ので if() の外に置く。
add_test(NAME srava_segs_coverage
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_segs_coverage.sh
          ${CMAKE_CURRENT_SOURCE_DIR})
# ⚠ この本は **一括倍率 x10 では届かない** (2026-09-25 実測)。プロセス起動が支配的な本 (木を走査して
#   1 件ずつ srava を起こす) なので、Windows のプロセス生成の高さと並列の競合が両方乗る:
#       Linux 単独 0.54s ・ MinGW 単独 25.23s (機差 47 倍) ・ **MinGW -j4 走行 154.38s** (さらに 6.1 倍)
#   ⇒ 既定 10 秒 x10 = 100 秒では **走行中に鳴く**。予算は負荷込みの値で決める。
#   ⇒ 番犬 60 (Windows では x10 = 600 ・ box 実測 154.38 の 3 倍を満たす)
#   ⚠ TIMEOUT は **番犬より大きく**する (同値だと ctest が先に殺して捕獲が 0 枚になる) ⇒ 60 → 120
set_tests_properties(srava_segs_coverage PROPERTIES
  ENVIRONMENT "SRAVA_HANG_SECS=60"
  PASS_REGULAR_EXPRESSION "SEGSCOV-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:"
  TIMEOUT 120)

if(SRAVA_MODULE_CGAL)
  srava_add_affine2d(cgal cgal.so 1e-9 "space")   # ★ #3526: cgal も 2D を空間に置ける
endif()
if(SRAVA_MODULE_MANIFOLD)
  srava_add_affine2d(manifold manifold.so 1e-9 "space")
endif()
if(SRAVA_MODULE_OCCT)
  srava_add_affine2d(occt occt.so 1e-9 "oc")
endif()

# ==================================================================
# ★ #3488: stdlib の回転ヘルパ (vcross / rotmat_2v / mat34 / rotate_v)。
#   ★ 純粋な値計算なので **カーネル op にしない** (7 実装の維持と対応表が増える。行列は
#     合成できるが op は合成できない・点列にも効く・退化ケースを 1 箇所で書ける)。
#   ★ 検証は閉形式 (定義の等式 / 正規直交 / det=+1 / 退化 3 種)。⚠ mat34 は
#     「rotmat_* をメッシュへ当てる道」なので **メッシュに当てて位置で**確かめる
#     (体積は剛体変換で不変なので体積だけでは何も言えない)。
#   ⚠ cgal が要る (⑦⑧⑨ が mesh の transform を通るため)。
# ==================================================================
if(SRAVA_MODULE_CGAL)
  add_test(NAME srava_stdmath
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_stdmath.sh $<TARGET_FILE:srava>)
  set_tests_properties(srava_stdmath PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/stdmath-cache;SRAVA_LIB=${CMAKE_CURRENT_SOURCE_DIR}/lib"
    PASS_REGULAR_EXPRESSION "STDMATH-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endif()

# ==================================================================
# ★ #3487: 素性を訊く op (bbox / centroid / area / valid) の回帰。
#   ★ 検証は **閉形式** (箱と正六角柱)。カーネル同士を突き合わせても両方が同じように
#     間違っていたら気づけない (#3470 の occt tube と同じ理由) ので kernel_agree に入れない。
#   ★★ valid は共通定義 (① 空でない ∧ ② 閉じている ∧ ③ 自己交差が無い) を先に決めてから
#     配った。既存 2 本がその定義から外れていたのを直した回帰でもある:
#       cgal … 空メッシュに 1 を返していた (① が抜けていた)
#       manifold … 自己交差 tube に 1 を返していた (③ が抜けていた)
#   ⚠ 第 5 引数 = 自己交差 tube に対する valid の期待値。openvdb は距離場なので ②③ が
#     恒真で 1・occt は自己交差する背骨の掃引を断るので "-" (検査しない)。
# ==================================================================
function(srava_add_meshprops _name _so _tol _dx _tube)
  add_test(NAME srava_meshprops_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_meshprops.sh
            $<TARGET_FILE:srava> ${_so} ${_tol} "${_dx}" "${_tube}")
  set_tests_properties(srava_meshprops_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/mpcache-${_name}"
    PASS_REGULAR_EXPRESSION "MESHPROPS-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_CGAL)
  srava_add_meshprops(cgal cgal.so 1e-9 "" 0)
endif()
if(SRAVA_MODULE_MANIFOLD)
  srava_add_meshprops(manifold manifold.so 1e-9 "" 0)
endif()
if(SRAVA_MODULE_OCCT)
  srava_add_meshprops(occt occt.so 1e-9 "" "-")
endif()
if(SRAVA_MODULE_NEF AND SRAVA_MODULE_CGAL)
  if(SRAVA_MODULE_NEF_SNC)
    srava_add_meshprops(nef_snc nef_snc.so 1e-9 "" 0)
  endif()
  if(SRAVA_MODULE_NEF_HYBRID)
    srava_add_meshprops(nef_hybrid nef_hybrid.so 1e-9 "" 0)
  endif()
endif()
if(SRAVA_MODULE_GEOGRAM)
  srava_add_meshprops(geogram geogram.so 1e-9 "" 0)
endif()
if(SRAVA_MODULE_CHERCHI)
  srava_add_meshprops(cherchi cherchi.so 1e-9 "" 0)
endif()
if(SRAVA_MODULE_OPENVDB)
  # ボクセル (dx=0.05) の誤差は **絶対で dx 程度**。代表寸法 1 に対して 5% なので 6e-2。
  srava_add_meshprops(openvdb openvdb.so 6e-2 0.05 1)
endif()

# ==================================================================
# ★ #3514: **位相を直接数える** op (nshells / nparts / genus) の回帰。
#   ★ 期待値はすべて閉形式で手で書ける整数 (球 / 箱 / トーラス / 中空の箱 / 同心球の xor)。
#     カーネル同士の合議ではない (#3470 の教訓)。
#   ★★ 3 本は **違う問い**に答える。中空の箱 (シェル 2 枚・塊 1 個) と同心球の xor
#     (シェル N 枚・塊 N/2 個) を必ず通し、どれかが他方の別名になっていないことを固定する。
#   ⚠ nparts は **nef の nparts と同じ約束** (SNC の marked volume = 塊)。cgal / manifold /
#     geogram の 3 本が同じ期待値表を見ることで、変換を挟まずに同じ数を返すことを保つ。
#   ⚠ 起票時の検証案「xor の N 球 → 成分 N/2」は **掃引の模型 (線分上の N 球) では成り立たない**
#     — 破片が球面の交線で接していて 1 枚に繋がるため。テスト側の冒頭に実測つきで書いてある。
# ==================================================================
function(srava_add_topology _name _so _seg)
  add_test(NAME srava_topology_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_topology.sh
            $<TARGET_FILE:srava> ${_so} ${_seg})
  # ⚠ 番犬の予算 — cgal / geogram の 2 本が重い (manifold は mac 1.44s / Linux 1.82s で軽い)。
  #     cgal     mac 5.24s / Linux 3.97s ⇒ 遅い方 (mac)   x3 = 15.7s  ┐ どちらも 20s で足りる
  #     geogram  mac 3.04s / Linux 3.56s ⇒ 遅い方 (Linux) x3 = 10.7s  ┘
  #   ★★ geogram は **この木で唯一 Linux の方が遅い本** (他の 25 本はすべて mac が遅い)。
  #     ⇒ 機の倍率は一律の係数で代替できないどころか **向きも一定でない**。予算は必ず
  #       両機の実測を並べて **遅い方**から取ること (2026-09-22 ・ mac と往復して確定)。
  set(_topo_hang "")
  if(_name STREQUAL "cgal" OR _name STREQUAL "geogram")
    set(_topo_hang ";SRAVA_HANG_SECS=20")
  endif()
  set_tests_properties(srava_topology_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/topocache-${_name}${_topo_hang}"
    PASS_REGULAR_EXPRESSION "TOPOLOGY-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 600)
endfunction()

if(SRAVA_MODULE_CGAL)
  srava_add_topology(cgal cgal.so 64)
endif()
if(SRAVA_MODULE_MANIFOLD)
  srava_add_topology(manifold manifold.so 64)
endif()
if(SRAVA_MODULE_GEOGRAM)
  srava_add_topology(geogram geogram.so 64)
endif()

# ==================================================================
# ★★★ #3527 段 4: geomutils.so の **片の取り出し** (part / part_at / shell / shell_at)。
#   ★ 段 4 の本丸は **入れ子** — 「どの空洞がどの塊のものか」。数えるだけ (nparts / nshells)
#     なら符号で足りるが、*取り出す* には入れ子が要る。そこが #3514 で part が見送られた
#     分かれ目で、実体は meshprops.h の nesting() (3D・巻き数) と ringprops.h の nesting()。
#   ★★ 柱は **和の式が 2 つ在り別物であること**:
#       part  … Σ |volume(part)|  == |volume(m)|   (符号なし)
#       shell … Σ  volume(shell)  ==  volume(m)    (符号つき)
#   ★ 入力を作るカーネルを振って回す — gu は 3 者から同じ 4CC (MFM3) で受けるので、
#     *どの double 系カーネルが作った値でも同じ分解になる* ことがここで固定される。
#   ⚠ 2D の節は manifold のときだけ走る (geogram / cherchi は 2D 型を持たない = 対象外で
#     あって歯抜けではない)。テスト側がカーネル名の表で判断している。
# ==================================================================
function(srava_add_geomutils _name _so)
  add_test(NAME srava_geomutils_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_geomutils.sh
            $<TARGET_FILE:srava> ${_so})
  # ⚠ SRAVA_HANG_SECS — Linux 実測 5.2s で既定 10 秒に **余裕が x1.9 しかない** (2026-09-22)。
  set_tests_properties(srava_geomutils_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/gucache-${_name};SRAVA_HANG_SECS=30"
    PASS_REGULAR_EXPRESSION "GEOMUTILS_OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_MANIFOLD)
  srava_add_geomutils(manifold manifold.so)
endif()
if(SRAVA_MODULE_GEOGRAM)
  srava_add_geomutils(geogram geogram.so)
endif()
if(SRAVA_MODULE_CHERCHI)
  srava_add_geomutils(cherchi cherchi.so)
endif()

# ==================================================================
# ★★ #3527 段 5: geomutils.so の **頂点を読む** op (vert / verts / face_verts)。
#   ★ 柱は 3 つ: ① verts(m)[i] == vert(m,i) (同じ列を同じ順) ② face_verts が指す三角形から
#     組み立てた面積と符号つき体積が m と一致 ③ face3d は **world の 3 成分**。
#   ⚠ ②は **面の並べ替えには不変** (実測済)。「i 番目が本当に i 番目か」は名指す口が他に無いので
#     原理的に見られず、代わりに索引の再現性で受けている。テスト側の③にその旨を書いてある。
#   ★ 陽性対照 4 種を実測: 向きの反転 / verts の列ずれ / face3d を枠内へ戻す は捕まり、
#     面の並べ替えだけが捕まらない。
# ==================================================================
function(srava_add_geomutils_vert _name _so)
  add_test(NAME srava_geomutils_vert_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_geomutils_vert.sh
            $<TARGET_FILE:srava> ${_so})
  # ⚠ SRAVA_HANG_SECS — Linux 実測 8.5s で既定 10 秒に **余裕が x1.2 しかない** (2026-09-22)。
  set_tests_properties(srava_geomutils_vert_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/guvcache-${_name};SRAVA_HANG_SECS=30"
    PASS_REGULAR_EXPRESSION "GEOMUTILS_VERT_OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_MANIFOLD)
  srava_add_geomutils_vert(manifold manifold.so)
endif()
if(SRAVA_MODULE_GEOGRAM)
  srava_add_geomutils_vert(geogram geogram.so)
endif()
if(SRAVA_MODULE_CHERCHI)
  srava_add_geomutils_vert(cherchi cherchi.so)
endif()

# ==================================================================
# ★★ #3527 段 6: sig の **型名の綴り** を登録済みの型名と突き合わせる。
#   sig はただの文字列なので綴り間違いはコンパイルを通り、ルータがその行に一度もヒットしない
#   **死んだ行** が黙って残る (gg/gu ・ nf/nfb ・ vd-grid3d/vd-grid)。
#   ⚠⚠ 守備範囲は「その名前が *登録されているか*」だけ。実在する別の型への取り違え
#     (gg-mesh3d -> gu-mesh3d) は **捕まらない** — 実測済みで、テスト本体の頭に書いてある。
#   ★ 材料は `srava --module-info` (ソースではなく **実物**。型名はマクロで組まれるため)。
#     ビルドされていないモジュールの型は台帳 test/sig_typenames.txt で補う。
# ==================================================================
add_test(NAME srava_sig_typenames
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_sig_typenames.sh $<TARGET_FILE:srava>)
set_tests_properties(srava_sig_typenames PROPERTIES
  PASS_REGULAR_EXPRESSION "SIGTYPES_OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|UNKNOWN|Traceback"
  TIMEOUT 60)

# ==================================================================
# ★★ #3527 段 6: **cast が枠 (置き場所) を落とさない** ことの回帰。
#   #3533 の規約②「降格は cast のみ」を **cast 自身が黙って破っていた** — PLY2 は
#   regions のあとにガイド層の節を挟んで枠 ("MARF") を置くのに、mf / gu の
#   decode_cross_exact が regions で止まっており、cg-face3d を渡すと z=0 へ戻っていた。
#   (#3526 で mf→cg は直っていたが **cg→mf の向きが残っていた**。段 6 の cast 監査で発見)
#   ⚠ 「3 成分になったか」では足りない ⇒ **高さ違いの 2 枚が別の答え**になることまで見る。
#   ★ ガイド (line の開いた折れ線) は mf / gu に表現が無いので **断る** — 黙って捨てると
#     nverts が cgal と食い違う。
# ==================================================================
if(SRAVA_MODULE_CGAL AND SRAVA_MODULE_MANIFOLD)
  add_test(NAME srava_cast_frame
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_cast_frame.sh $<TARGET_FILE:srava>)
  set_tests_properties(srava_cast_frame PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/castframecache;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib"
    PASS_REGULAR_EXPRESSION "CASTFRAME_OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped|parse error"
    TIMEOUT 120)
endif()

# ==================================================================
# ★ #3514: distance_at(m,[x,y,z]) — **点との距離** の回帰。
#   ★★ 主役は **箱**。球は入れない (srava_meshprops.sh と同じ判断) — occt は解析球なので
#     |d-r| が厳密に出るが、メッシュ系は **内接多面体**なので構造的に小さい値になる
#     (半径 2・seg=128 で中心から 1.99805)。許容を緩めて両方通すのは偽のテストになる。
#     箱なら面が平面なので全カーネルで厳密に一致し、角・稜の外側まで手で書ける。
#   ★ 第 5 引数 "sphere" = 解析球の検定も行う (B-rep の occt だけ)。
#     ⚠⚠ これは occt の実装の罠の回帰でもある: TopoDS_Solid をそのまま
#     BRepExtrema_DistShapeShape に渡すと「中身の詰まった領域」として扱われ、
#     **内側の点との距離が 0** になる。Face の compound に対して測る必要がある。
#   ★ 第 5 引数 "band" = openvdb。距離が **場そのもの**なので探索が要らない代わりに、
#     狭帯域の外は background に飽和する ⇒ そこは明示エラーになることを固定する。
# ==================================================================
function(srava_add_distance_at _name _so _tol _dx _mode)
  add_test(NAME srava_distance_at_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_distance_at.sh
            $<TARGET_FILE:srava> ${_so} ${_tol} "${_dx}" "${_mode}")
  # ⚠ 番犬の予算 — **occt の本だけ重い** (mac 3.44s / Linux 1.41s ⇒ 最も遅い機 x3 = 15s)。
  set(_da_hang "")
  if(_name STREQUAL "occt")
    set(_da_hang ";SRAVA_HANG_SECS=15")
  endif()
  set_tests_properties(srava_distance_at_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/dacache-${_name}${_da_hang}"
    PASS_REGULAR_EXPRESSION "DISTANCEAT-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_CGAL)
  srava_add_distance_at(cgal cgal.so 1e-9 "" "")
endif()
if(SRAVA_MODULE_GEOGRAM)
  srava_add_distance_at(geogram geogram.so 1e-9 "" "")
endif()
if(SRAVA_MODULE_OCCT)
  srava_add_distance_at(occt occt.so 1e-9 "" sphere)
endif()
if(SRAVA_MODULE_OPENVDB)
  # ボクセル (dx=0.02) の誤差は絶対で dx 程度。代表寸法 0.03 に対して緩めに 5%。
  srava_add_distance_at(openvdb openvdb.so 5e-2 0.02 band)
endif()

# ==================================================================
# ★ #3514: section(m,P,N[,mode]) — 断面 の回帰。occt へ配線したのを機に **3 カーネル共通**にした。
#   ★★ 固定するものは 2 つ。① **3 要素配列の規約** (共面かどうかでどの要素が答えかが入れ替わる)
#     ② **面積だけでなく座標** — 2D 断面は切断平面の局所基底に置かれるので、基底の取り方が違うと
#     面積が合っていても bbox / extrude が食い違う。断面を extrude して 3D の bbox で固定する。
#   ★ 第 4 引数: "z" = Z 法線だけ (manifold は Z 以外を断る) / "all" = 6 軸 + 斜め /
#     "analytic" = さらに解析球 (B-rep の occt だけ — メッシュ系は内接多角形で構造的に違う値)。
# ==================================================================
function(srava_add_section _name _so _tol _mode)
  add_test(NAME srava_section_${_name}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_section.sh
            $<TARGET_FILE:srava> ${_so} ${_tol} ${_mode})
  # ⚠ 番犬の予算 — **occt の本だけ重い** (mac 10.28s / Linux 5.62s ⇒ 最も遅い機 x3 = 45s)。
  #   2026-09-22 に Linux 実測だけで 30s と書いたが、mac を並べたら x2.9 しか無かった。
  set(_sec_hang 30)
  if(_name STREQUAL "occt")
    set(_sec_hang 45)
  endif()
  set_tests_properties(srava_section_${_name} PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_BINARY_DIR}/seccache-${_name};SRAVA_HANG_SECS=${_sec_hang}"
    PASS_REGULAR_EXPRESSION "SECTION-OK"
    FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
    TIMEOUT 300)
endfunction()

if(SRAVA_MODULE_CGAL)
  srava_add_section(cgal cgal.so 1e-9 all)
endif()
if(SRAVA_MODULE_MANIFOLD)
  srava_add_section(manifold manifold.so 1e-9 z)
endif()
if(SRAVA_MODULE_OCCT)
  srava_add_section(occt occt.so 1e-9 analytic)
endif()

# ==================================================================
# ★ #3431 P0-a: **依存なし構成**の回帰 (完了条件② 「依存が無い環境でもビルドが通る」)。
#   SRAVA_MODULE_{CGAL,NEF,MANIFOLD,PIPEPROX} を全部 OFF にした別ビルドを作り、
#   ビルドが通ることと、残った d3 だけで実際に走ることを見る。option はずっと前からあったが
#   **OFF 構成を一度もビルドしていなかった**ので、ガードの外へ依存が漏れても誰も気づけなかった。
#   ★ CGAL も Manifold も引かないので**安い**。重ければ `ctest -LE nodeps`。
# ==================================================================
#   ★ 2026-09-11: **親と同じ tinyState を使わせる**。素の configure は /usr/local を引くので、
#     親を別 prefix の tinyState に向けている間 (例: #3509 の insNeq が /usr/local に未展開)
#     このテストだけが古い tinyState でビルドされ、見ているものが「依存なし構成が通るか」から
#     「/usr/local の tinyState が新しいか」へ静かにすり替わる。
add_test(NAME srava_build_nodeps
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_nodeps.sh
          ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/nodeps-build
          -DtinyState_DIR=${tinyState_DIR}
          -DTINYSTATE_TSCPP2=${TINYSTATE_TSCPP2})
# ★ 2026-08-27: **このテストは内部で全コアを使ってビルドする** (build_nodeps.sh が
#   `cmake --build -j$(nproc)`)。ところが ctest からは 1 スロットにしか見えないので、
#   `ctest -jN` で走らせると **N-1 個の他テストと全コアビルドが同時に動く** =
#   機体が 2 倍以上オーバーサブスクライブされる。
#   ⇒ MinGW 実機で、**単独なら数秒で通るテストが TIMEOUT を超える**という形で表面化した
#     (落ちる顔ぶれが回ごとに変わるのが目印)。個別に TIMEOUT を上げて回るのは対症療法なので、
#     **占有コア数を正しく申告する**。
#   ⇒ ctest はこのテストの実行中、他へ回すスロットを空けなくなる (実質的に単独実行)。
include(ProcessorCount)
ProcessorCount(SRAVA_NPROC)
if(SRAVA_NPROC EQUAL 0)
  set(SRAVA_NPROC 4)
endif()
set_tests_properties(srava_build_nodeps PROPERTIES
  LABELS "nodeps"
  PROCESSORS ${SRAVA_NPROC}
  PASS_REGULAR_EXPRESSION "NODEPS-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
  TIMEOUT 900)

# ------------------------------------------------------------------
# ★ #3431: srava_add_module() で定義した全モジュールを host の実行時依存として配線する。
#   ここで一括してやるのは、cgal.so のように **srava より前**で定義されるモジュールがあり、
#   その場では add_dependencies(srava ...) を書けない (第 1 引数は既存ターゲットである必要がある)
#   ため。個々のブロックで書き忘れる余地も無くなる。
# ------------------------------------------------------------------
srava_wire_modules_to_hosts()

# ==================================================================
# ★ #3431 P0-a: **install ツリー**の回帰。他の全テストはビルドツリーの .so と srava_agent を
#   env で名指しして走るので、「install した物がそれ自身で動くか」を誰も見ていなかった。
#   このテストだけは env を落として、install した prefix が自力で自分のモジュールと agent を
#   解決できること・外部依存を持つモジュールが in-proc と別プロセスの両方で動くことを見る。
#   詳細は test/srava_install.sh 冒頭。
#   ★ staging prefix はビルドツリーの下 ($BINARY_DIR/installtest) に作る。/usr/local には触らない。
#   ★ 実行時間の大半は .so のコピー (cgal.so 272MB + nef 174MB×2)。ラベル install を付けてあるので
#     `ctest -LE install` で外せる。
# ==================================================================
add_test(NAME srava_install_tree
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_install.sh
          ${CMAKE_BINARY_DIR} ${CMAKE_BINARY_DIR}/installtest)
# ⚠ 番犬の予算 — **最も遅い機**の実測 x3 (2026-09-22)。mac(arm64/OCCT 7.9.3) 9.28s ・ Linux(x86_64/7.8.1) 2.92s ⇒ 30s。
set_tests_properties(srava_install_tree PROPERTIES
  ENVIRONMENT "SRAVA_HANG_SECS=30"
  LABELS "install"
  PASS_REGULAR_EXPRESSION "INSTALL-OK"
  FAIL_REGULAR_EXPRESSION "FAIL:|Segmentation|Aborted|core dumped"
  TIMEOUT 600)

# 点群の transform 一族 と union (#3578)。意図は test/srava_points_transform.sh 冒頭。
#   ★ 本題は **面外へ出す変換だけが pt-cloud3d を返す**こと (行は #xy / #z の変種 + 基底)。
#   ⚠ 型の検定だけでは足りない — type_of は *routing の申告* を答えるので、計算本体だけが
#     別の次元を返しても緑になる。⇒ 値の検定 (vert / bbox) を必ず併せて置く。
#   ★ 番犬の予算 — mac 実測 **4.6 秒** (単独 ・ x3 = 14 秒) で既定 10 秒を越える ⇒ 明示する。
#     ⚠ -j6 のフル走行では 6.97 秒まで伸びる (検定 1 件ごとに srava を起こすため)。
#     ⚠ Linux 側は未測定。bench で測って「両機の遅い方 x3」へ詰めてよい (0df54df の原則)。
add_test(NAME srava_points_transform
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_points_transform.sh $<TARGET_FILE:srava>)
set_tests_properties(srava_points_transform PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/ptxcache;SRAVA_HANG_SECS=30"
  PASS_REGULAR_EXPRESSION "PTX_OK"
  FAIL_REGULAR_EXPRESSION "PTX_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly"
  TIMEOUT 120)

# ------------------------------------------------------------------
# ★ 2026-08-12: 全テストにモジュール探索路を固定注入する。
#   モジュールの探索路には install 先 ($PREFIX/lib/srava/modules) も含まれるため、注入しないと
#   ctest の結果が **その機械の install 状態に依存** する (ビルドツリーの .so を検証しているつもりが
#   install 済みの古い .so を読んでいた、という取り違えが実際に起きた)。
#   $SRAVA_MODULE_PATH は探索路の最後 = 最優先なので、これでビルドツリーのモジュールに固定される。
#   個々のテストが持つ ENVIRONMENT (SRAVA_AGENT 等) は APPEND なので壊さない。
# ------------------------------------------------------------------
get_property(_srava_all_tests DIRECTORY PROPERTY TESTS)
foreach(_t IN LISTS _srava_all_tests)
  set_property(TEST ${_t} APPEND PROPERTY
    ENVIRONMENT "SRAVA_MODULE_PATH=$<TARGET_FILE_DIR:srava>")
endforeach()

# ------------------------------------------------------------------
# ★★ #3522 / #3556 (2026-09-20・ひさ指示): **Windows は TIMEOUT を一括で伸ばす**。
#
#   278 箇所の TIMEOUT は全部 **Linux の実測**で決めたもので、OS 依存の仕掛けが無かった
#   (最頻値は 20 秒・129 箇所)。ところが同じ 546 本の所要は
#       Linux (simu01)  201 秒
#       Windows (box)  2100 秒     = **約 10 倍**
#   なので、Linux で 2 秒のテストが box では 20 秒に届き、**素で 20 秒の TIMEOUT に当たる**。
#
#   実際 2026-09-20 の box 走行で赤になった 3 本は、静かな機械で再走したら全部緑だった:
#       srava_vert          通常  5.5 秒 → Timeout 120 秒  (22 倍)
#       srava_occt_loft     通常 15   秒 → Timeout 300 秒  (20 倍)
#       srava_segs_coverage 通常 28   秒 → Timeout  60 秒
#   ⇒ **偽の座り込みはノイズでしかない**ので、Windows では閾値を実測どおり伸ばす。
#
#   ⚠ 伸ばすと **本物の座り込みを見つけるのも遅くなる** (300 秒 → 3000 秒)。それは承知のうえ。
#     本物を早く捕まえる役は番犬に任せる — 番犬は 2026-09-20 から
#     **CPU が進んでいないこと**を確かめてから撮るので、遅いだけの個体は撮らない。
#   ⚠ 個々の TIMEOUT には触らない (278 箇所を書き換えると、次に値を決め直すときの
#     根拠が散る)。**最後に一括で掛ける**。
#   ★ 倍率は変えられるようにしておく: -DSRAVA_TIMEOUT_SCALE=4 のように指定できる。
#
#   ⚠⚠ 【2026-09-25 訂正 2 件】
#     ① **`if(WIN32)` は Cygwin では偽**。CMake は Cygwin で CYGWIN=1 / WIN32=0 にするので、
#        Cygwin だけ倍率 1 のまま = **Linux の TIMEOUT で Windows 速度の機械を測っていた**。
#        MinGW 側の木には倍率が通り、Cygwin の木には通っていなかった。
#        ⇒ Cygwin では 4 本が TIMEOUT で赤 (commsort / nef_variant_guard /
#          hangwatch_coverage / segs_coverage)。いずれも **伸ばせば落ちない本**で、
#          倍率を通したあとは緑になる。
#        ⇒ Cygwin も **PE を吐く Windows 速度の機械**なので同じ扱いにする。
#        (機種ごとの所要は社内に置く — このコミットのメッセージと #3587)
#     ② **番犬の予算 (SRAVA_HANG_SECS) に倍率が通っていなかった。** TIMEOUT だけ 10 倍にしたので、
#        Windows では「ctest は殺さないが番犬は鳴く」帯ができ、**緑のまま捕獲が溜まる**。
#        box の実測で数えた (番犬つき 533 本 ・ 予算を書いた本 125 本):
#            倍率 x1  → 予算不足 **82 本** (既定 10s の 37 + 書いた本の 45)
#            倍率 x3  → 20 本 ・ x5 → 5 本 ・ **x10 → 1 本**
#        ⇒ 予算表の個別ミスではなく **倍率が片方だけ通っていなかった**。個々の値は触らず、
#          TIMEOUT と同じ倍率を **SRAVA_HANG_SCALE で渡し、番犬の側で掛ける**
#          (こうすると **予算を書いていない 467 本 = 既定 10 秒にも同時に効く**)。
#        ★ x10 でも残る 1 本 = srava_segs_coverage。個別に予算を書く (下の add_test 側で対応済み)。
#
#   ⚠⚠ **既存の build tree は古い倍率を持ち続ける。** `set(... CACHE ...)` は既存のキャッシュ値を
#     上書きしないので、この変更より前に configure した木では `SRAVA_TIMEOUT_SCALE` が 1 のまま。
#     ⇒ 上げるには **`cmake -U SRAVA_TIMEOUT_SCALE -U SRAVA_HANG_SCALE -S . -B <dir>`**
#       (実測: box の build-cyg は素の再 configure では 1 のままで、-U して初めて 10 になった。
#        新設の SRAVA_HANG_SCALE だけは キャッシュに無いので 10 になり、**片方だけ 10** という
#        紛らわしい状態になっていた)
#
#   ★ 倍率の根拠 (2026-09-25 ・ **同じ条件で並べて**決めた)。
#     ⚠ 「機械の速さの差」と「並列走行による膨らみ」は **別の要因**で、掛け算になる。
#       ⇒ 片方だけ見て決めると足りない。実測値は社内に置く (#3587 ・ このコミットのメッセージ)。
#     ★ 予算は **負荷込みの値**で決める — 番犬が鳴くのは実際の `ctest -j4` の最中だから。
#       (単独の値で敷くと、本番の走行で鳴きはじめる)
# ------------------------------------------------------------------
if(WIN32 OR CYGWIN)
  set(_srava_to_scale_default 10)
else()
  set(_srava_to_scale_default 1)
endif()
set(SRAVA_TIMEOUT_SCALE "${_srava_to_scale_default}" CACHE STRING
    "ctest の TIMEOUT に掛ける倍率 (Windows / Cygwin の既定 10)")
set(SRAVA_HANG_SCALE "${_srava_to_scale_default}" CACHE STRING
    "番犬の予算 (SRAVA_HANG_SECS) に掛ける倍率。TIMEOUT と揃える (Windows / Cygwin の既定 10)")

if(NOT SRAVA_TIMEOUT_SCALE EQUAL 1)
  set(_srava_to_n 0)
  get_property(_srava_to_tests DIRECTORY PROPERTY TESTS)
  foreach(_t IN LISTS _srava_to_tests)
    get_test_property(${_t} TIMEOUT _to)
    if(_to)
      math(EXPR _to_new "${_to} * ${SRAVA_TIMEOUT_SCALE}")
      set_tests_properties(${_t} PROPERTIES TIMEOUT ${_to_new})
      math(EXPR _srava_to_n "${_srava_to_n} + 1")
    endif()
  endforeach()
  message(STATUS
    "ctest TIMEOUT: ${_srava_to_n} 本を ${SRAVA_TIMEOUT_SCALE} 倍にした "
    "(Windows / Cygwin は Linux 用の値では足りないので、この機械向けの既定に合わせる。"
    "-DSRAVA_TIMEOUT_SCALE=<倍率> で変えられる)")
endif()

# ★★ 番犬の予算にも同じ倍率を通す (2026-09-25)。
#   ⚠ 個々の SRAVA_HANG_SECS は **書き換えない** (60 箇所の根拠が散るため・TIMEOUT と同じ方針)。
#     env を 1 本足して **番犬の側で掛ける** ⇒ 予算を書いていない本 (既定 10 秒) にも効く。
#   ⚠ SRAVA_HANG_SECS=0 (無効化) は掛けても 0 のまま = 無効のままになる。
if(NOT SRAVA_HANG_SCALE EQUAL 1)
  set(_srava_hs_n 0)
  get_property(_srava_hs_tests DIRECTORY PROPERTY TESTS)
  foreach(_t IN LISTS _srava_hs_tests)
    get_test_property(${_t} ENVIRONMENT _srava_hs_env)
    if(_srava_hs_env)
      set_tests_properties(${_t} PROPERTIES
        ENVIRONMENT "${_srava_hs_env};SRAVA_HANG_SCALE=${SRAVA_HANG_SCALE}")
    else()
      set_tests_properties(${_t} PROPERTIES
        ENVIRONMENT "SRAVA_HANG_SCALE=${SRAVA_HANG_SCALE}")
    endif()
    math(EXPR _srava_hs_n "${_srava_hs_n} + 1")
  endforeach()
  message(STATUS
    "番犬の予算: ${_srava_hs_n} 本へ SRAVA_HANG_SCALE=${SRAVA_HANG_SCALE} を渡した "
    "(TIMEOUT と同じ倍率。素の値は書き換えていない)")
endif()
