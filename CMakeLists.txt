cmake_minimum_required(VERSION 3.16)
project(srava LANGUAGES CXX C)   # C は lemon_cpp(パーサ生成器)のビルド用

# include "..." ライブラリの **コンパイル時既定探索先**($PREFIX/share/srava/lib)。
# レキサ(cgptsLemonParser)が $SRAVA_PATH の後にここを探す → インストール済み stdlib を環境変数なしで引ける。
# add_compile_definitions は以降に定義する全ターゲットに効く(冒頭に置く)。
add_compile_definitions(SRAVA_LIBDIR="${CMAKE_INSTALL_PREFIX}/share/srava/lib")

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
if(SRAVA_MODULE_CGAL)
  find_package(CGAL REQUIRED)
  # cgaVoxelize(export_vox)の vox.h5 書き出し用。無ければ export_vox だけ無効化(srava 本体はビルドされる)。
  # ⚠ Cygwin の libhdf5-devel は cmake config(/usr/cmake/hdf5-config.cmake)が壊れており
  #   find_package(HDF5) が FATAL で configure を落とす。voxelize は任意機能なので Cygwin では既定で無効化。
  #   有効化したい(config を直した)場合は -DSRAVA_ENABLE_HDF5=ON。
  # HDF5(cgaVoxelize)は cgal カーネル専用なので CGAL ブロック内で検出する。
  if(CYGWIN AND NOT SRAVA_ENABLE_HDF5)
    message(STATUS "Cygwin: HDF5(export_vox)は既定で無効(壊れた hdf5-config を回避・-DSRAVA_ENABLE_HDF5=ON で有効化)")
  else()
    find_package(HDF5 COMPONENTS C)
  endif()
  if(HDF5_FOUND)
    message(STATUS "HDF5 found → export_vox(voxel化→vox.h5)を有効化")
  else()
    message(STATUS "HDF5 not found → export_vox は無効（srava 本体は通常ビルド）")
  endif()
else()
  message(STATUS "SRAVA_MODULE_CGAL=OFF → CGAL 非依存ビルド (cgal.so・cgal 専用テストを除外・既定カーネルは次点へ)")
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

# ★ 2026-08-12: pig を **共有ライブラリ** にした (Windows モジュール対応の前提)。
#   従来: pig=STATIC + pts ランタイム(AGENT_HOST_SRC)を各 host が個別コンパイル → モジュール .so は
#         pig/pts/tinyState を **未解決のまま**残し host の -rdynamic + RTLD_GLOBAL で dlopen 時に解決。
#   これは ELF 固有の idiom で **PE/COFF (Windows) では成立しない** (DLL はリンク時未解決を許さない)。
#   → pig + AGENT_HOST_SRC(pts) + tinyState を 1 本の共有ライブラリに集約し、host もモジュールも
#     両方これにリンクする。モジュールの未定義シンボルが 0 になり Windows でも DLL 化できる。
#   実測 (nm -D -u manifold.so): モジュールが host に要求していたシンボル 292 本 =
#     pig 51 / pts 74 / ts2・tinyState 55 / libc・libstdc++ 72。→ 3 者すべてを共有ライブラリに入れる。
#   ⚠ tinyState は **PIC ビルドが必要** (非 PIC の .a は共有ライブラリに埋め込めない)。Redmine #3421。
add_library(pig SHARED
  src/classes/pig/c++/pigData.cpp
  src/classes/pig/c++/pigCacheCodec.cpp
  src/classes/pig/c++/pigModuleRegistry.cpp   # モジュール (カーネル/プラグイン) 一元レジストリ (K1〜K3 集約・Phase 1)
  src/classes/pig/c++/pigTypeRegistry.cpp     # rev4 型ディスパッチ (§9) の型軸レジストリ (Phase A)
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
target_link_libraries(pig PUBLIC Threads::Threads m ${CMAKE_DL_LIBS})
# 共有ライブラリなので install が要る (host / モジュールが実行時に参照する)。
install(TARGETS pig LIBRARY DESTINATION lib)

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
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsMediator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsMediatorExternal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsMediatorInternal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWirePipeTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/wpBigParent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/wpBigChild.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsAgentStub.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfAgent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfModuleAgent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfAgentTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsCalcBody.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaBox.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaPrism.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaPyramid.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaSphere.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaIcosphere.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaUnion.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaCombine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaIntersection.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaDifference.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaExport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaVoxelize.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaValid.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/cgal/c++/cgaRepair.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfApply.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfWhile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfArrayFold.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfMap.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfMapOp.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfGate.cpp
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
  src/classes/pig/c++/pigAgentRegistry.cpp
  src/classes/pig/c++/osglue.cpp
  src/classes/pig/c++/ptsWirePacket.cpp
  src/classes/pig/c++/ptsWirePipe.cpp
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
  src/classes/pig/c++/pigfAssign.cpp
  src/classes/pig/c++/pigfOps.cpp
  src/classes/pig/c++/pigfSequence.cpp
  src/classes/pig/c++/pigfIf.cpp
  src/classes/pig/c++/pigfApply.cpp
  src/classes/pig/c++/pigfWhile.cpp
  src/classes/pig/c++/pigfArrayFold.cpp
  src/classes/pig/c++/pigfMap.cpp
  src/classes/pig/c++/pigfMapOp.cpp
  src/classes/pig/c++/pigfGate.cpp
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
  modules/cgal/c++/cgaBox.cpp
  modules/cgal/c++/cgaPrism.cpp
  modules/cgal/c++/cgaPyramid.cpp
  modules/cgal/c++/cgaSphere.cpp
  modules/cgal/c++/cgaIcosphere.cpp
  modules/cgal/c++/cgaUnion.cpp
  modules/cgal/c++/cgaCombine.cpp
  modules/cgal/c++/cgaIntersection.cpp
  modules/cgal/c++/cgaDifference.cpp
  modules/cgal/c++/cgaExport.cpp
  $<$<BOOL:${HDF5_FOUND}>:modules/cgal/c++/cgaVoxelize.cpp>
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
  modules/cgal/c++/cgaValid.cpp
  modules/cgal/c++/cgaRepair.cpp
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
add_library(cgal MODULE ${CGAL_KERNEL_SRC} modules/cgal/manifest.cpp)
set_target_properties(cgal PROPERTIES PREFIX "" OUTPUT_NAME "cgal")   # → cgal.so
add_dependencies(cgal pigts_codegen gen_srava_parser)
target_include_directories(cgal PRIVATE ${SRAVA_GEN})
target_compile_definitions(cgal PRIVATE SRAVA_VALUE_ONLY $<$<BOOL:${HDF5_FOUND}>:SRAVA_HAVE_HDF5>)
install(TARGETS cgal LIBRARY DESTINATION lib/srava/modules)   # → $PREFIX/lib/srava/modules/cgal.so (ローダの SYSDIR)
target_link_libraries(cgal PRIVATE pig CGAL::CGAL)
set_target_properties(cgal PROPERTIES INSTALL_RPATH "${SRAVA_MODULE_RPATH}")   # lib/srava/modules → $PREFIX/lib
if(HDF5_FOUND)
  target_include_directories(cgal PRIVATE ${HDF5_C_INCLUDE_DIRS} ${HDF5_INCLUDE_DIRS})
  target_link_libraries(cgal PRIVATE ${HDF5_C_LIBRARIES} ${HDF5_LIBRARIES})
endif()
# srava_agent は実行時に cgal.so を dlopen する → 一緒にビルドされることを保証。
add_dependencies(srava_agent cgal)
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
if(SRAVA_MODULE_CGAL)
  add_dependencies(srava cgal)   # cgal: planner が起動時に dlopen (SRAVA_MODULE_CGAL=OFF では非依存)
endif()
target_include_directories(srava PRIVATE ${SRAVA_GEN})
target_link_libraries(srava PRIVATE pig Threads::Threads m)
# ★ .so 化 Phase 3c-2: planner は起動時にカーネル .so を dlopen し in-proc thread 実行する。
#   dlopen した .so の未解決 pig/pts シンボルを planner 本体から解決させるため -rdynamic で export。
set_target_properties(srava PROPERTIES ENABLE_EXPORTS ON)

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
tinystate_codegen(demo_ts_codegen
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/demo/dematsAgent.cpp)
add_library(demo_module MODULE
  modules/demo/dematsAgent.cpp
  modules/demo/demo_compute.cpp
  modules/demo/manifest.cpp)
set_target_properties(demo_module PROPERTIES PREFIX "" OUTPUT_NAME "demo")   # → demo.so
target_link_libraries(demo_module PRIVATE pig)   # libpig(共有) にリンク (2026-08-12)
set_target_properties(demo_module PROPERTIES INSTALL_RPATH "${SRAVA_MODULE_RPATH}")   # lib/srava/modules → $PREFIX/lib
add_dependencies(demo_module pigts_codegen demo_ts_codegen)
target_include_directories(demo_module PRIVATE modules/demo)
add_dependencies(srava demo_module)         # planner が起動時に dlopen (generic op 受理)
add_dependencies(srava_agent demo_module)   # srava_agent が process 実行時に dlopen (make_agent)

# demo.so が dlopen でき記述子 (make_agent 付き) が読めることを検証 (RTLD_NOW 全解決)。
add_test(NAME srava_module_demo_so
  COMMAND srava_module_probe $<TARGET_FILE:demo_module>)
set_tests_properties(srava_module_demo_so PROPERTIES
  PASS_REGULAR_EXPRESSION "MODULE_OK name=demo .*make_agent=yes")

# ★ 完成条件の実証: ホスト無改修で demo.so の新 op が使えるか (generic mk_call 層 + agent() opt-in)。
add_test(NAME srava_module_demo_ops
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> module_demo_ops)
set_tests_properties(srava_module_demo_ops PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-demo-ops"
  PASS_REGULAR_EXPRESSION "DEMO_OK"
  FAIL_REGULAR_EXPRESSION "DEMO_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly")

# ★ op-owner ルーティング実証: agent() priority opt-in なしで demo op が owner (demo) へ流れる。
add_test(NAME srava_module_demo_ops_noprio
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> module_demo_ops_noprio)
set_tests_properties(srava_module_demo_ops_noprio PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-demo-noprio"
  PASS_REGULAR_EXPRESSION "DEMO_OK"
  FAIL_REGULAR_EXPRESSION "DEMO_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op|closed unexpectedly")

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
add_library(d3_module MODULE
  modules/d3/d3Mesh.cpp
  modules/d3/d3CacheCodec.cpp
  modules/d3/ptsd3WireCacheStreamWriterMesh.cpp
  modules/d3/ptsd3WireCacheStreamReaderMesh.cpp
  modules/d3/d3aCube.cpp
  modules/d3/d3aMerge.cpp
  modules/d3/d3aNfaces.cpp
  modules/d3/d3aNverts.cpp
  modules/d3/d3atsAgent.cpp
  modules/d3/manifest.cpp)
set_target_properties(d3_module PROPERTIES PREFIX "" OUTPUT_NAME "d3")   # → d3.so
target_link_libraries(d3_module PRIVATE pig)   # libpig(共有) にリンク (2026-08-12)
set_target_properties(d3_module PROPERTIES INSTALL_RPATH "${SRAVA_MODULE_RPATH}")   # lib/srava/modules → $PREFIX/lib
add_dependencies(d3_module pigts_codegen d3_ts_codegen)
target_include_directories(d3_module PRIVATE modules/d3)
add_dependencies(srava d3_module)         # planner が起動時に dlopen (記述子/型登録)
add_dependencies(srava_agent d3_module)   # srava_agent が process 実行時に dlopen (make_agent)

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
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/d4/d4atsAgent.cpp)
add_library(d4_module MODULE
  modules/d4/d4Mesh.cpp
  modules/d4/d4CacheCodec.cpp
  modules/d4/ptsd4WireCacheStreamWriterMesh.cpp
  modules/d4/ptsd4WireCacheStreamReaderMesh.cpp
  modules/d4/d4aCube.cpp
  modules/d4/d4aMerge.cpp
  modules/d4/d4aNfaces.cpp
  modules/d4/d4aNverts.cpp
  modules/d4/d4atsAgent.cpp
  modules/d4/manifest.cpp)
set_target_properties(d4_module PROPERTIES PREFIX "" OUTPUT_NAME "d4")   # → d4.so
target_link_libraries(d4_module PRIVATE pig)   # libpig(共有) にリンク (2026-08-12)
set_target_properties(d4_module PROPERTIES INSTALL_RPATH "${SRAVA_MODULE_RPATH}")   # lib/srava/modules → $PREFIX/lib
add_dependencies(d4_module pigts_codegen d4_ts_codegen)
target_include_directories(d4_module PRIVATE modules/d4)
add_dependencies(srava d4_module)
add_dependencies(srava_agent d4_module)

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
add_library(d5_module MODULE
  modules/d5/d5Mesh.cpp
  modules/d5/d5CacheCodec.cpp
  modules/d5/ptsd5WireCacheStreamWriterMesh.cpp
  modules/d5/ptsd5WireCacheStreamReaderMesh.cpp
  modules/d5/d5aCube.cpp
  modules/d5/d5aMerge.cpp
  modules/d5/d5aNfaces.cpp
  modules/d5/d5aNverts.cpp
  modules/d5/d5atsAgent.cpp
  modules/d5/manifest.cpp)
set_target_properties(d5_module PROPERTIES PREFIX "" OUTPUT_NAME "d5")   # → d5.so
target_link_libraries(d5_module PRIVATE pig)   # libpig(共有) にリンク (2026-08-12)
set_target_properties(d5_module PROPERTIES INSTALL_RPATH "${SRAVA_MODULE_RPATH}")   # lib/srava/modules → $PREFIX/lib
add_dependencies(d5_module pigts_codegen d5_ts_codegen)
target_include_directories(d5_module PRIVATE modules/d5)
add_dependencies(srava d5_module)
add_dependencies(srava_agent d5_module)

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
add_library(d2_module MODULE
  modules/d2/d2Shape.cpp
  modules/d2/d2CacheCodec.cpp
  modules/d2/ptsd2WireCacheStreamWriterShape.cpp
  modules/d2/ptsd2WireCacheStreamReaderShape.cpp
  modules/d2/d2aSquare.cpp
  modules/d2/d2aCount.cpp
  modules/d2/d2atsAgent.cpp
  modules/d2/manifest.cpp)
set_target_properties(d2_module PROPERTIES PREFIX "" OUTPUT_NAME "d2")   # → d2.so
target_link_libraries(d2_module PRIVATE pig)   # libpig(共有) にリンク (2026-08-12)
set_target_properties(d2_module PROPERTIES INSTALL_RPATH "${SRAVA_MODULE_RPATH}")   # lib/srava/modules → $PREFIX/lib
add_dependencies(d2_module pigts_codegen d2_ts_codegen)
target_include_directories(d2_module PRIVATE modules/d2)
add_dependencies(srava d2_module)
add_dependencies(srava_agent d2_module)

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
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-planner-test-cache"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped")

# 解決後 agent エラーの集約検証: 根(union)が A_SAVE_BEGIN(継続解決)後にエラーを出す。
# プランナーは ptsApp->set_agentError 経由でこれを拾い、countAgent==0 後にエラーを表示し異常終了する。
# 毎回 MISS にするため dir を消してから実行。集約エラーメッセージが出力に出れば経路 OK。
add_test(NAME srava_planner_agenterr
  COMMAND sh -c "rm -rf /tmp/srava-agenterr-test && exec '$<TARGET_FILE:srava>'")
set_tests_properties(srava_planner_agenterr PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-agenterr-test;PIG_TEST_ERR_AFTER_SAVE=1"
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
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sigint-test;PIG_TEST_SLOW=1;SRAVA_SOURCE=var acc = box(1,1,1)\; for (var i = 1\; i < 30\; i = i + 1) { acc = acc ||| box(i,i,1)\; } export(acc)\;"
    PASS_REGULAR_EXPRESSION "interrupted by SIGINT"
    FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|HUNG"
    TIMEOUT 40)
  # SIGTERM: Windows は CTRL_CLOSE_EVENT(→SIGTERM)をプログラム生成できず、graceful SIGTERM-kill
  # 相当も無いため自己テスト不可 → 除外(グレースフル終了経路は上の SIGINT で検証済み)。
else()
add_test(NAME srava_planner_sigint
  COMMAND sh -c "rm -rf /tmp/srava-sigint-test && exec '$<TARGET_FILE:srava>'")
set_tests_properties(srava_planner_sigint PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sigint-test;PIG_TEST_RAISE_SIGINT=1;PIG_TEST_SLOW=1"
  PASS_REGULAR_EXPRESSION "interrupted by SIGINT"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# abort 精査: 素の kill(SIGTERM=15)も SIGINT と同じ撤収経路に合流(in-flight agent kill + クリーン
# cleanup、ハングなし)。exit 143=128+15。SIGINT 専用でなく終了系シグナル全般を捌けること。
add_test(NAME srava_planner_sigterm
  COMMAND sh -c "rm -rf /tmp/srava-sigterm-test && exec '$<TARGET_FILE:srava>'")
set_tests_properties(srava_planner_sigterm PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sigterm-test;PIG_TEST_RAISE_SIGNAL=15;PIG_TEST_SLOW=1"
  PASS_REGULAR_EXPRESSION "interrupted by SIGTERM"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)
endif()

# 終了時クリーンアップ(1.2.5): この run で使われなかった完了キャッシュを usedCaches 登録簿で判定し削除。
add_test(NAME srava_planner_cleanup_unused
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_cleanup_unused.sh $<TARGET_FILE:srava>)
set_tests_properties(srava_planner_cleanup_unused PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-cleanup-test"
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
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
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

# 制御文 return/break/continue。for の continue で step が走る(無限ループしない)ことを TIMEOUT で担保。
add_test(NAME srava_control_flow
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> control_flow)
set_tests_properties(srava_control_flow PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-control"
  PASS_REGULAR_EXPRESSION "CF= 122"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR|parse error"
  TIMEOUT 15)

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
  PASS_REGULAR_EXPRESSION "include: cannot find"
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
  PASS_REGULAR_EXPRESSION "nv=16 nf=24"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# combine(a,b,c): n-ary も二項分解で合体。3 箱 = 24v36f。
add_test(NAME srava_combine_fn
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> combine_fn)
set_tests_properties(srava_combine_fn PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-combinefn"
  PASS_REGULAR_EXPRESSION "nv=24 nf=36"
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
  PASS_REGULAR_EXPRESSION "ERROR\\[.*,4\\] array index out of range"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 負の幅 rect は退化ポリゴン → 2D union でエージェントがクラッシュしていた回帰の防止。
# rect が明示エラー(位置付き)で弾き、agent クラッシュしないことを確認。
add_test(NAME srava_rect_neg
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> rect_neg)
set_tests_properties(srava_rect_neg PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-rectneg"
  PASS_REGULAR_EXPRESSION "rect: width and height must be positive"
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
  PASS_REGULAR_EXPRESSION "ERROR\\[.*,3\\] volume"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# ソースファイル実行 (srava file.sra) + 先頭シェバング行 (#!...) の読み飛ばし。
add_test(NAME srava_filearg
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> filearg)
set_tests_properties(srava_filearg PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-filearg"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error|cannot open"
  TIMEOUT 20)

# transform 系(translate/rotate/mirror/transform): Aff_transformation_3<EPECK> を mesh に適用。
# 座標が実際に動く証明として union の頂点/面数で検証(回転は任意角=double cos/sin、mirror は向き補正)。
add_test(NAME srava_xf_translate
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> xlate)
set_tests_properties(srava_xf_translate PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-xf-xl"
  PASS_REGULAR_EXPRESSION "nv=24 nf=44"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR"
  TIMEOUT 20)
add_test(NAME srava_xf_rotate
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> rotate)
set_tests_properties(srava_xf_rotate PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-xf-rot"
  PASS_REGULAR_EXPRESSION "nv=22 nf=40"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR"
  TIMEOUT 20)
add_test(NAME srava_xf_mirror
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> mirror)
set_tests_properties(srava_xf_mirror PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-xf-mir"
  PASS_REGULAR_EXPRESSION "nv=16 nf=24"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR"
  TIMEOUT 20)
add_test(NAME srava_xf_transform
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> xform)
set_tests_properties(srava_xf_transform PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-xf-xfm"
  PASS_REGULAR_EXPRESSION "nv=24 nf=44"
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
  PASS_REGULAR_EXPRESSION "nv=24 nf=44"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_op_rotate
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> op_rotate)
set_tests_properties(srava_op_rotate PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-op-rot"
  PASS_REGULAR_EXPRESSION "nv=22 nf=40"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_op_mirror
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> op_mirror)
set_tests_properties(srava_op_mirror PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-op-mir"
  PASS_REGULAR_EXPRESSION "nv=16 nf=24"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 任意ベクトル軸: rotate(m,[0,0,1],deg) は文字列 "z" と一致(Rodrigues の主軸特例)。
add_test(NAME srava_xf_vec_axis
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> vec_axis)
set_tests_properties(srava_xf_vec_axis PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-vecax"
  PASS_REGULAR_EXPRESSION "nv=22 nf=40"
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
  PASS_REGULAR_EXPRESSION "nv=8 nf=12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_scale_op
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> scale_op)
set_tests_properties(srava_scale_op PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sc-op"
  PASS_REGULAR_EXPRESSION "nv=8 nf=12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_scale_vec
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> scale_vec)
set_tests_properties(srava_scale_vec PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sc-v"
  PASS_REGULAR_EXPRESSION "nv=16 nf=24"
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
  PASS_REGULAR_EXPRESSION "nv=16 nf=24"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 2D(Step C): cgMesh2D + rect + extrude + 2D booleans + 多態スピン(reader/writer/op_* が次元で分岐)。
add_test(NAME srava_2d_extrude
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> extrude)
set_tests_properties(srava_2d_extrude PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-ext"
  PASS_REGULAR_EXPRESSION "nv=8 nf=12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_poly_union
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> poly2d_union)
set_tests_properties(srava_2d_poly_union PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-pu"
  PASS_REGULAR_EXPRESSION "nv=16 nf=28"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_extrude_union3d
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> extrude_union3d)
set_tests_properties(srava_2d_extrude_union3d PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-eu"
  PASS_REGULAR_EXPRESSION "nv=17 nf=30"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_mixed_dim_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> mixed_dim_err)
set_tests_properties(srava_2d_mixed_dim_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-mix"
  PASS_REGULAR_EXPRESSION "cannot mix 2D and 3D"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 2D プリミティブ拡充(ngon/circle/polygon)。
add_test(NAME srava_2d_ngon
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> prim_ngon)
set_tests_properties(srava_2d_ngon PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-ng"
  PASS_REGULAR_EXPRESSION "nv=12 nf=20"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_circle
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> prim_circle)
set_tests_properties(srava_2d_circle PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-ci"
  PASS_REGULAR_EXPRESSION "nv=64 nf=124"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_circle_segs
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> prim_circle_segs)
set_tests_properties(srava_2d_circle_segs PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-ciseg"
  PASS_REGULAR_EXPRESSION "nv=16 nf=28"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_sphere_subdiv
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> prim_sphere_subdiv)
set_tests_properties(srava_sphere_subdiv PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sph-sd"
  PASS_REGULAR_EXPRESSION "nv=162 nf=320"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 30)
add_test(NAME srava_2d_polygon
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> prim_polygon)
set_tests_properties(srava_2d_polygon PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-pg"
  PASS_REGULAR_EXPRESSION "nv=6 nf=8"
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
  PASS_REGULAR_EXPRESSION "nv=16 nf=32"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_extrude_hole_union
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> extrude_hole_union)
set_tests_properties(srava_2d_extrude_hole_union PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-ehu"
  PASS_REGULAR_EXPRESSION "nv=19 nf=34"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 2D rotate 軸不要(単一角度)+ 演算子 @(deg)。
add_test(NAME srava_2d_rotate
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> rotate2d)
set_tests_properties(srava_2d_rotate PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-rot"
  PASS_REGULAR_EXPRESSION "nv=8 nf=12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 値返し op(area)を式で観測 = value-parse 対称化 + 演算子の継続 deref + A_SAVE_BEGIN 本文修正。
add_test(NAME srava_area_expr
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> area_expr)
set_tests_properties(srava_area_expr PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-area-expr"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_area_arith
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> area_arith)
set_tests_properties(srava_area_arith PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-area-arith"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# レキシカルスコープ(eager-DEF): 遅延束縛の自由変数が内側シャドウを誤って拾わない。
add_test(NAME srava_lexical_shadow
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> lexical_shadow)
set_tests_properties(srava_lexical_shadow PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-lexshadow"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 配列構築の演算子化: インライン配列内 varref がネスト agent op(union mesh 引数)でも正しい env で解決。
add_test(NAME srava_arr_varref
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> arr_varref)
set_tests_properties(srava_arr_varref PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-arrvarref"
  PASS_REGULAR_EXPRESSION "nv=16 nf=24"
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
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# trigger(並列 spark): volume(a)==volume(b) を並列起動。正当性(8!=3→else 25v46f)を検証。
add_test(NAME srava_parallel_cmp
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> parallel_cmp)
set_tests_properties(srava_parallel_cmp PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-parcmp"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# valid/repair(検査=値返し / 修復=mesh 返し。多態 op_valid/op_repair。2D=is_simple/even-odd, 3D=自己交差/autorefine)。
add_test(NAME srava_valid_ok
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> valid_ok)
set_tests_properties(srava_valid_ok PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-valid-ok"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_valid_bad
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> valid_bad)
set_tests_properties(srava_valid_bad PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-valid-bad"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_repair_2d
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> repair_2d)
set_tests_properties(srava_repair_2d PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-repair-2d"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_repair_3d
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> repair_3d)
set_tests_properties(srava_repair_3d PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-repair-3d"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 計測の残り(volume/perimeter/centroid)。値返し(scalar)+ 配列返し(centroid・VALUE 復元+添字)+ dim ガード。
add_test(NAME srava_volume
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> volume)
set_tests_properties(srava_volume PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-volume"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_volume_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> volume_err)
set_tests_properties(srava_volume_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-volume-err"
  PASS_REGULAR_EXPRESSION "volume: 2D has no volume"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|nv="
  TIMEOUT 20)
add_test(NAME srava_perimeter
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> perimeter)
set_tests_properties(srava_perimeter PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-perimeter"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_centroid_2d
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> centroid_2d)
set_tests_properties(srava_centroid_2d PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-centroid-2d"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_centroid_3d
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> centroid_3d)
set_tests_properties(srava_centroid_3d PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-centroid-3d"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 近接(distance/closest/farthest)= 二項 value-return(新組合せ BINMESH_IN+out=AK_INLINE)・AABB 近接 + 配列返し。
add_test(NAME srava_distance
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> distance)
set_tests_properties(srava_distance PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-distance"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_distance_err
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> distance_err)
set_tests_properties(srava_distance_err PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-distance-err"
  PASS_REGULAR_EXPRESSION "distance: needs two 3D meshes"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|nv="
  TIMEOUT 20)
add_test(NAME srava_closest
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> closest)
set_tests_properties(srava_closest PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-closest"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_farthest
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> farthest)
set_tests_properties(srava_farthest PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-farthest"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# #4 性能崖の回帰ガード: 巨大インライン配列(2048 点・serialize 後 >64KB)。崖が再発すると TIMEOUT で落ちる。
add_test(NAME srava_bigtube
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> bigtube)
set_tests_properties(srava_bigtube PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-bigtube"
  PASS_REGULAR_EXPRESSION "nv=12290 nf=24576"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)

# tube(3D 折れ線まわりの掃引管)+ 太さ可変/尖り端 + 閉多様体検証。
add_test(NAME srava_tube
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> tube)
set_tests_properties(srava_tube PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-tube"
  PASS_REGULAR_EXPRESSION "nv=18 nf=32"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_tube_taper
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> tube_taper)
set_tests_properties(srava_tube_taper PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-tube-tp"
  PASS_REGULAR_EXPRESSION "nv=10 nf=16"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_tube_union
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> tube_union)
set_tests_properties(srava_tube_union PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-tube-un"
  PASS_REGULAR_EXPRESSION "nv=34 nf=64"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)

# tube: 連続重複頂点を弾かず間引く(spline 等の区間境界重複を吸収)。間引き後 2 頂点 = 素の管 18v32f。
add_test(NAME srava_tube_dedup
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> tube_dedup)
set_tests_properties(srava_tube_dedup PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-tube-dd"
  PASS_REGULAR_EXPRESSION "nv=18 nf=32"
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

# revolve(2D→3D 回転体・全周 360°)+ 閉多様体検証 + 部分角エラー。
add_test(NAME srava_2d_revolve
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> revolve)
set_tests_properties(srava_2d_revolve PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-rev"
  PASS_REGULAR_EXPRESSION "nv=66 nf=128"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_2d_revolve_union
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> revolve_union)
set_tests_properties(srava_2d_revolve_union PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-revu"
  PASS_REGULAR_EXPRESSION "nv=70 nf=136"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_2d_revolve_partial
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> revolve_partial)
set_tests_properties(srava_2d_revolve_partial PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-revp"
  PASS_REGULAR_EXPRESSION "nv=20 nf=36"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_2d_revolve_segs
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> revolve_segs)
set_tests_properties(srava_2d_revolve_segs PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-revs"
  PASS_REGULAR_EXPRESSION "nv=18 nf=32"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)
add_test(NAME srava_2d_revolve_partial_union
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> revolve_partial_union)
set_tests_properties(srava_2d_revolve_partial_union PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-revpu"
  PASS_REGULAR_EXPRESSION "nv=22 nf=40"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)

# 2D SVG/DXF I/O: SVG round-trip(穴保持)+ DXF export。
add_test(NAME srava_2d_svg_roundtrip
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> svg_roundtrip)
set_tests_properties(srava_2d_svg_roundtrip PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-svg"
  PASS_REGULAR_EXPRESSION "nv=16 nf=32"
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
add_test(NAME srava_disable_bad_option
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> disable_bad_option)
set_tests_properties(srava_disable_bad_option PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-disable-bad"
  PASS_REGULAR_EXPRESSION "module: string option must be"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error"
  TIMEOUT 20)
endif()   # SRAVA_MODULE_CGAL (disable_cgal 系)
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
  PASS_REGULAR_EXPRESSION "nv=16 nf=32"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 20)

# 2D offset(straight skeleton・EPECK 出力): インセット/肉厚枠/消滅。
add_test(NAME srava_2d_offset_inset
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> offset_inset)
set_tests_properties(srava_2d_offset_inset PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-offi"
  PASS_REGULAR_EXPRESSION "nv=8 nf=12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_2d_offset_shell
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> offset_shell)
set_tests_properties(srava_2d_offset_shell PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-offs"
  PASS_REGULAR_EXPRESSION "nv=16 nf=32"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)
add_test(NAME srava_2d_offset_vanish
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> offset_vanish)
set_tests_properties(srava_2d_offset_vanish PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-offv"
  PASS_REGULAR_EXPRESSION "nv=0 nf=0"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 25)

# 3D Minkowski offset(Nef・icosphere 近似球): 膨張 / 外殻 / inset エラー。重いので timeout 多め。
add_test(NAME srava_3d_offset
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> offset_3d)
set_tests_properties(srava_3d_offset PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-3d-off"
  PASS_REGULAR_EXPRESSION "nv=72 nf=140"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 60)
add_test(NAME srava_3d_offset_coarse
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> offset_3d_coarse)
set_tests_properties(srava_3d_offset_coarse PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-3d-offc"
  PASS_REGULAR_EXPRESSION "nv=24 nf=44"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 60)
add_test(NAME srava_3d_offset_shell
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> offset_3d_shell)
set_tests_properties(srava_3d_offset_shell PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-3d-offs"
  # 3D offset(Minkowski/Nef)の正確な nv/nf は CGAL バージョン依存(例: Linux baseline 82/156 vs
  # macOS の CGAL 6.2 で 84/160)。厳密個数を固定せず「非自明な閉メッシュが出て crash しない」を検証
  # (空/退化なら nv/nf が 1 桁 → 不一致。real な破綻は FAIL_REGULAR_EXPRESSION で捕捉)。
  PASS_REGULAR_EXPRESSION "nv=[1-9][0-9]+ nf=[1-9][0-9]+"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 60)
add_test(NAME srava_3d_offset_inset
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> offset_3d_inset)
set_tests_properties(srava_3d_offset_inset PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-3d-offin"
  PASS_REGULAR_EXPRESSION "nv=8 nf=12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 90)
add_test(NAME srava_3d_offset_inner_shell
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> offset_3d_inner_shell)
set_tests_properties(srava_3d_offset_inner_shell PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-3d-offish"
  PASS_REGULAR_EXPRESSION "nv=16 nf=24"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 90)

# キャッシュ HIT/MISS ログ: 同ソース 2 回実行 → 2 回目は全 HIT(miss=0)= 再利用が効いている。
add_test(NAME srava_cache_hit
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> cachehit)
set_tests_properties(srava_cache_hit PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-hit-test"
  PASS_REGULAR_EXPRESSION "0 miss"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|parse error"
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
  PASS_REGULAR_EXPRESSION "nv=15 nf=26"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op"
  TIMEOUT 20)
add_test(NAME srava_op_difference
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> difference)
set_tests_properties(srava_op_difference PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-op-diff"
  PASS_REGULAR_EXPRESSION "nv=16 nf=28"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op"
  TIMEOUT 20)

# 2.1 言語拡張: if/else + ブロック + 比較 + 自己代入(strict SET)。
add_test(NAME srava_lang_ifaccum
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> ifaccum)
set_tests_properties(srava_lang_ifaccum PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-if1"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)
add_test(NAME srava_lang_ifskip
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> ifskip)
set_tests_properties(srava_lang_ifskip PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-if2"
  PASS_REGULAR_EXPRESSION "nv=8 nf=12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 2.x array リテラル + 添字参照(ソース)。
add_test(NAME srava_lang_array
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> arrayidx)
set_tests_properties(srava_lang_array PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-arr"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 2.x source 側 hash リテラル {k:v,..} + メンバ参照(層化文法で文頭 { ブロック衝突を解消)。
add_test(NAME srava_lang_hash
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> hashlit)
set_tests_properties(srava_lang_hash PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-hash"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 2.x 構造 inline 引数の実ワイヤ接続: array を serialize→C_ARG_INLINE→agent value-parse→cgaBox 展開。
add_test(NAME srava_inline_struct
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> inlineval)
set_tests_properties(srava_inline_struct PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-inlinev"
  PASS_REGULAR_EXPRESSION "nv=8 nf=12"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped"
  TIMEOUT 20)

# 2.1 lambda + apply(clone/thunk 再評価モデル)。
add_test(NAME srava_lambda
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> lambda)
set_tests_properties(srava_lambda PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-lambda"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 20)

# 2.1 同一 lambda の再 apply(body->clone() でメモ衝突回避)。
add_test(NAME srava_lambda_reapply
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> lambda_reapply)
set_tests_properties(srava_lambda_reapply PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-lambda-re"
  PASS_REGULAR_EXPRESSION "nv=33 nf=62"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 20)

# 2.1 クロージャ(カリー化): 捕捉した env を親に持つ lambda を返す。
add_test(NAME srava_lambda_closure
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> lambda_closure)
set_tests_properties(srava_lambda_closure PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-lambda-cl"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
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
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-wgate;PIG_MAX_WORKERS=2"
  PASS_REGULAR_EXPRESSION "worker gate: cap=2"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 40)

# fork EAGAIN: PIG_MAX_WORKERS(32) が実 fork 上限(2)を超えると、limit 固定方針なので backoff せず
# 「PIG_MAX_WORKERS を下げて再実行」の明確なエラーで終了する(ハングや黙ったデッドロックを避ける)。
add_test(NAME srava_workergate_eagain
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> workergate_eagain)
set_tests_properties(srava_workergate_eagain PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-wgate-ea;PIG_MAX_WORKERS=32;PIG_TEST_FORKLIMIT=2"
  PASS_REGULAR_EXPRESSION "Lower PIG_MAX_WORKERS"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|Timeout"
  TIMEOUT 40)

# fd リーク回帰: ulimit -n 64・cap=8(同時 8 agent で fd は収まる)で多数の agent を回す。FIN で rfd/pipe を
# 閉じれば fd は再利用され完走("result cache="/"result value="・export の out_cache=0 化で後者)。
# 閉じ忘れ=リークがあれば fd が累積枯渇し、launch 失敗で
# "Lower PIG_MAX_WORKERS"(ERROR[)になり FAIL。★ limit 固定方針なので backoff に頼らず cap を明示する。
add_test(NAME srava_fdleak
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> fdleak)
set_tests_properties(srava_fdleak PROPERTIES
  # PIG_MAX_WORKERS=4 (2026-08-01): ピーク fd 使用量は同時 agent 数にほぼ比例し、最小で通る
  # ulimit -n は W8:56 / W6:48 / W4:40 (実測)。上限 64 に対し W8 では余裕 8 fd しかなく、
  # フル ctest の同時実行下では ~10%/cold run で "fork failed" になっていた。W4 なら余裕 24 fd。
  # fork 回数(240)は変わらないのでリーク検出力は落ちない (1 agent 1 fd でも漏れれば 40 個で枯渇)。
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-fdleak;PIG_MAX_WORKERS=4"
  PASS_REGULAR_EXPRESSION "result (cache=|value=)"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR\\["
  TIMEOUT 60)

# 2.1 while ループ(毎周 clone 再評価・共有 env でループ変数更新)。
add_test(NAME srava_while
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> while_loop)
set_tests_properties(srava_while PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-while"
  PASS_REGULAR_EXPRESSION "nv=52 nf=100"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 30)

# 2.1 for ループ(while への desugar)。
add_test(NAME srava_for
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> for_loop)
set_tests_properties(srava_for PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-for1"
  PASS_REGULAR_EXPRESSION "nv=52 nf=100"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 30)

# 2.1 入れ子 for(内側ループが外側 body の clone で毎周再初期化される)。
add_test(NAME srava_for_nested
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> for_nested)
set_tests_properties(srava_for_nested PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-for2"
  PASS_REGULAR_EXPRESSION "nv=33 nf=62"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 30)

# 2.1 一般呼び出し(直接/連鎖適用): callee は任意の式。中間変数なしのカリー適用。
add_test(NAME srava_apply_chain
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> apply_chain)
set_tests_properties(srava_apply_chain PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-applychain"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 20)

# 2.1 再帰 lambda(引数 call-by-value で変数捕捉回避)。
add_test(NAME srava_recursion
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> recursion)
set_tests_properties(srava_recursion PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-recursion"
  PASS_REGULAR_EXPRESSION "nv=52 nf=100"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|ERROR"
  TIMEOUT 30)

# 2.2 import(path): 外部ファイル(STL/OFF/..)読み込み。export→import 往復 + DAG 葉として union。
add_test(NAME srava_import
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> import)
set_tests_properties(srava_import PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-import"
  PASS_REGULAR_EXPRESSION "nv=25 nf=46"
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
  PASS_REGULAR_EXPRESSION "nv=27 nf=50"
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
  PASS_REGULAR_EXPRESSION "nv=14 nf=24"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op"
  TIMEOUT 20)
add_test(NAME srava_shape_pyramid
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> pyramid)
set_tests_properties(srava_shape_pyramid PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pyr"
  PASS_REGULAR_EXPRESSION "nv=6 nf=8"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op"
  TIMEOUT 20)
add_test(NAME srava_shape_sphere
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> sphere)
set_tests_properties(srava_shape_sphere PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sph"
  PASS_REGULAR_EXPRESSION "nv=258 nf=512"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op"
  TIMEOUT 20)

# ★sphere / icosphere が cgal と manifold で体積 bit 一致 (共通 geodesic.h)。manifold ビルド時のみ。
if(SRAVA_MODULE_MANIFOLD)
add_test(NAME srava_sphere_kernel_agree
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> sphere_kernel_agree)
set_tests_properties(srava_sphere_kernel_agree PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-sph-agree"
  PASS_REGULAR_EXPRESSION "AGREE"
  FAIL_REGULAR_EXPRESSION "MISMATCH|Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 30)

# ★tube (3D 掃引管 / 2D 帯) が cgal と manifold で一致 (共通 common/tube.h・#3415)。
#   bit 一致は要求しない (cgal=厳密積算 / manifold=double 積算・2D は合併エンジンが別) → 相対許容で見る。
add_test(NAME srava_tube_kernel_agree
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> tube_kernel_agree)
set_tests_properties(srava_tube_kernel_agree PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-tube-agree"
  PASS_REGULAR_EXPRESSION "AGREE"
  FAIL_REGULAR_EXPRESSION "MISMATCH|Segmentation|Aborted|core dumped|unknown op|ERROR|parse error"
  TIMEOUT 30)

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
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-mfnested"
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
  add_executable(pipeprox_test_pipe ${PIPEPROX_DIR}/tests/test_pipe.cpp)
  target_link_libraries(pipeprox_test_pipe PRIVATE pipeprox)
  add_test(NAME srava_pipeprox_vendored_test COMMAND pipeprox_test_pipe)
  set_tests_properties(srava_pipeprox_vendored_test PROPERTIES
    FAIL_REGULAR_EXPRESSION "FAIL:" TIMEOUT 30)

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
  add_library(pipe_proximity_module MODULE
    modules/pipe_proximity/ppatsAgent.cpp
    modules/pipe_proximity/ppaCompute.cpp
    modules/pipe_proximity/pipe_proximity_compute.cpp
    modules/pipe_proximity/pipe_proximity_adapter.cpp
    modules/pipe_proximity/manifest.cpp)
  set_target_properties(pipe_proximity_module PROPERTIES PREFIX "" OUTPUT_NAME "pipe_proximity")  # → pipe_proximity.so
  add_dependencies(pipe_proximity_module pigts_codegen pp_ts_codegen)
  target_include_directories(pipe_proximity_module PRIVATE modules/pipe_proximity)
  target_link_libraries(pipe_proximity_module PRIVATE pig pipeprox)
  set_target_properties(pipe_proximity_module PROPERTIES INSTALL_RPATH "${SRAVA_MODULE_RPATH}")   # lib/srava/modules → $PREFIX/lib
  add_dependencies(srava pipe_proximity_module)   # planner が起動時に dlopen (in-proc thread 実行)
  add_dependencies(srava_agent pipe_proximity_module)   # ★Plan A: process 実行も srava_agent が dlopen

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
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib;SRAVA_CACHE_DIR=/tmp/srava-pipefixed"
    PASS_REGULAR_EXPRESSION "PIPEFIXED_OK"
    FAIL_REGULAR_EXPRESSION "PIPEFIXED_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 300)

  # wSpace(制御点間隔の均一化・上流 v0.1.7 の項)の配線回帰。既定 0=従来一致・指定時に効くこと。
  add_test(NAME srava_pipeprox_wspace
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> pipeprox_wspace)
  set_tests_properties(srava_pipeprox_wspace PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pipewspace"
    PASS_REGULAR_EXPRESSION "PIPEWSPACE_OK"
    FAIL_REGULAR_EXPRESSION "PIPEWSPACE_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 120)

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
  set_tests_properties(srava_plugin_separate PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib;SRAVA_CACHE_DIR=/tmp/srava-pipeseparate"
    PASS_REGULAR_EXPRESSION "PIPESEPARATE_OK"
    FAIL_REGULAR_EXPRESSION "PIPESEPARATE_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 60)

  # ★ Plan A: install は pipe_proximity.so を探索路 (lib/srava/modules) へ置く方式へ (kernel と同じ)。
  #   旧 process バイナリ + .plugin manifest 自動生成は廃止。
  install(TARGETS pipe_proximity_module LIBRARY DESTINATION lib/srava/modules)
endif()

# ------------------------------------------------------------------
# 第2幾何カーネル: Manifold(github.com/elalish/manifold・Apache-2.0・#3404)。
#   opt-in。ON で Manifold を FetchContent(v3.5.2 pin)取得。MANIFOLD_PAR=OFF(シリアル)＝
#   srava のプロセス fan-out に並列を寄せる方針(#3404 / 実測 2026-07-23: Manifold は per-op 直列律速)。
#   まずは疎通 mf_smoke(box∖sphere 1回)。通れば mfatsAgent(cgatsAgent ミラー)へ。
# ------------------------------------------------------------------
# option(SRAVA_MODULE_MANIFOLD) と旧名の後方互換はファイル冒頭 (§option 集約) で宣言済み。
if(SRAVA_MODULE_MANIFOLD)
  include(FetchContent)
  set(MANIFOLD_TEST   OFF CACHE BOOL "" FORCE)
  set(MANIFOLD_PAR    OFF CACHE BOOL "" FORCE)   # シリアル(並列は srava の fan-out で稼ぐ)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaSphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaIcosphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaUnion.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaValid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaCentroid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaImport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaRect.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaCircle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaNgon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaExtrude.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaCombine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaSection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/manifold/c++/mfaOffset.cpp
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
    modules/manifold/c++/mfaSphere.cpp
    modules/manifold/c++/mfaIcosphere.cpp
    modules/manifold/c++/mfaUnion.cpp
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
    modules/manifold/c++/mfaValid.cpp
    modules/manifold/c++/mfaCentroid.cpp
    modules/manifold/c++/mfaImport.cpp
    modules/manifold/c++/mfaRect.cpp
    modules/manifold/c++/mfaCircle.cpp
    modules/manifold/c++/mfaNgon.cpp
    modules/manifold/c++/mfaExtrude.cpp
    modules/manifold/c++/mfaCombine.cpp
    modules/manifold/c++/mfaSection.cpp
    modules/manifold/c++/mfaOffset.cpp
    modules/manifold/c++/mfaTube.cpp
    modules/manifold/c++/mfaColor.cpp
    modules/manifold/c++/mfatsAgent.cpp
  )
  add_library(manifold_module MODULE ${MF_KERNEL_SRC} modules/manifold/manifest.cpp)
  set_target_properties(manifold_module PROPERTIES PREFIX "" OUTPUT_NAME "manifold")   # → manifold.so
  add_dependencies(manifold_module pigts_codegen mf_ts_codegen gen_srava_parser)
  target_include_directories(manifold_module PRIVATE ${SRAVA_GEN})
  target_compile_definitions(manifold_module PRIVATE SRAVA_VALUE_ONLY)
  target_link_libraries(manifold_module PRIVATE pig manifold::manifold)
  set_target_properties(manifold_module PROPERTIES INSTALL_RPATH "${SRAVA_MODULE_RPATH}")   # lib/srava/modules → $PREFIX/lib
  install(TARGETS manifold_module LIBRARY DESTINATION lib/srava/modules)   # → $PREFIX/lib/srava/modules/manifold.so
  add_dependencies(srava_agent manifold_module)   # srava_agent が実行時に manifold.so を dlopen

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
  add_dependencies(srava manifold_module)

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
