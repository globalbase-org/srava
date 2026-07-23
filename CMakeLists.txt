cmake_minimum_required(VERSION 3.16)
project(srava LANGUAGES CXX C)   # C は lemon_cpp(パーサ生成器)のビルド用

# include "..." ライブラリの **コンパイル時既定探索先**($PREFIX/share/srava/lib)。
# レキサ(cgptsLemonParser)が $SRAVA_PATH の後にここを探す → インストール済み stdlib を環境変数なしで引ける。
# add_compile_definitions は以降に定義する全ターゲットに効く(冒頭に置く)。
add_compile_definitions(SRAVA_LIBDIR="${CMAKE_INSTALL_PREFIX}/share/srava/lib")

find_package(Threads REQUIRED)

# CGAL(エージェント側のみリンク。プランナーは CGAL 非依存を維持)。
# 重い幾何演算(corefinement union 等)に EPECK kernel を使う → GMP/MPFR 依存は CGAL::CGAL が運ぶ。
find_package(CGAL REQUIRED)
# cgaVoxelize(export_vox)の vox.h5 書き出し用。無ければ export_vox だけ無効化(srava 本体はビルドされる)。
# ⚠ Cygwin の libhdf5-devel は cmake config(/usr/cmake/hdf5-config.cmake)が壊れており
#   find_package(HDF5) が FATAL で configure を落とす。voxelize は任意機能なので Cygwin では既定で無効化。
#   有効化したい(config を直した)場合は -DSRAVA_ENABLE_HDF5=ON。
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
  ${CMAKE_BINARY_DIR}
  ${TINYSTATE_INC}
)

include(${CMAKE_CURRENT_SOURCE_DIR}/proj/cmake/TinyState.cmake)

enable_testing()

# ------------------------------------------------------------------
# pig コア(stdObject ベースの軽パス。状態機械ではない → tscpp2 不要)
# ------------------------------------------------------------------
add_library(pig STATIC
  src/classes/pig/c++/pigData.cpp
  src/classes/pig/c++/pigCacheManager.cpp   # キャッシュ dir 機構(pig 層・pigwire のみ依存)。sweep を呼ぶ実行体は pig をリンク済み
  src/classes/pig/c++/pigValueCodec.cpp     # 値の往復(serialize の対の parser)。プラグイン機構の I-O
  src/classes/pig/c++/pigPluginSDK.cpp      # プラグインエージェント SDK(pigwire serve・CGAL/srava 非依存)
  src/classes/pig/c++/pigPluginRegistry.cpp # プラグイン op レジストリ(マニフェスト読取・op→bin)
)
target_compile_definitions(pig PRIVATE PIG_PLUGIN_SYSDIR="${CMAKE_INSTALL_PREFIX}/share/srava/plugins")
target_link_libraries(pig PUBLIC ${TINYSTATE_LIB} Threads::Threads m)

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
# 参照プラグインエージェント(プラグイン規約の疎通確認)。pig + libtinyState のみ。
#   CGAL も srava も状態機械もリンクしない = 薄い SDK の実証。
# ------------------------------------------------------------------
add_executable(echo_agent plugins/echo/echo_agent.cpp)
target_link_libraries(echo_agent PRIVATE pig ${TINYSTATE_LIB} Threads::Threads m)

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
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfFunction.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfAssign.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfMain.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWireCacheStreamWriter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWireCacheStreamWriterText.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWireCacheStreamReader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWireCacheStreamReaderText.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/ptscgWireCacheStreamWriterMesh.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWireCacheStreamWriterRef.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/ptscgWireCacheStreamReaderMesh.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWireStreamTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWirePipe.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsWirePipeTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/wpBigParent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/wpBigChild.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsAgentStub.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfAgent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/pigfSravaAgent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfPluginAgent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/pigfAgentTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/pig/c++/ptsCalcBody.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaBox.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaPrism.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaPyramid.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaSphere.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaUnion.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaCombine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaIntersection.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaDifference.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaExport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaVoxelize.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaImport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaTranslate.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaRotate.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaMirror.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaScale.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaTransform.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaColor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaRect.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaNgon.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaCircle.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaPolygon.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaLine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaSection.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaExtrude.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaTube.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaRevolve.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaOffset.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaArea.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaValid.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaRepair.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaVolume.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaPerimeter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaCentroid.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaBbox.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaDistance.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaClosest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaFarthest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgaThinSpots.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgatsAgent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/classes/cg/c++/cgatsAgentTest.cpp
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

add_executable(smoke_pts
  src/main/pts_main.cpp
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
)
add_dependencies(smoke_pts pigts_codegen)
target_link_libraries(smoke_pts PRIVATE ${TINYSTATE_LIB} Threads::Threads m)
add_test(NAME pts_smoke COMMAND smoke_pts)

# pigf async 機構の end-to-end(pigDataFunction<pigfConst> → 非同期 yield/resume → 42)
add_executable(test_pigf
  src/main/pigf_main.cpp
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/pigfFunction.cpp
  src/classes/pig/c++/pigfAssign.cpp
  src/classes/pig/c++/pigfMain.cpp
  src/classes/pig/c++/pigfOps.cpp
)
add_dependencies(test_pigf pigts_codegen)
target_link_libraries(test_pigf PRIVATE pig ${TINYSTATE_LIB} Threads::Threads m)
add_test(NAME pigf COMMAND test_pigf)

# ------------------------------------------------------------------
# step4 pigwire I/O エンジン: ptsWireCacheStream{Writer,Reader}[Text] 往復テスト
#   WriterText が D_META/D_TEXT を書き → ReaderText がポーリング読みして連結 →
#   元の payload とバイト一致するか(逐次 + 同一プロセス並行の 2 段階)。
# ------------------------------------------------------------------
add_executable(test_cachestream
  src/main/cachestream_main.cpp
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
  src/classes/pig/c++/osglue.cpp
  src/classes/pig/c++/ptsWireCacheStreamWriter.cpp
  src/classes/pig/c++/ptsWireCacheStreamWriterText.cpp
  src/classes/pig/c++/ptsWireCacheStreamReader.cpp
  src/classes/pig/c++/ptsWireCacheStreamReaderText.cpp
  src/classes/pig/c++/ptsWireStreamTest.cpp
)
add_dependencies(test_cachestream pigts_codegen)
target_link_libraries(test_cachestream PRIVATE pig ${TINYSTATE_LIB} Threads::Threads m)
add_test(NAME cachestream COMMAND test_cachestream)

# ------------------------------------------------------------------
# step5 ptsWirePipe: socketpair 越しの双方向メッセージパイプ echo 往復テスト
#   2 本の ptsWirePipe が handshake → ping/echo → 番兵で双方 self-terminate。
#   NB: AF_UNIX socketpair(2) を使うため Windows(MinGW)では除外(winsock に socketpair 無し)。
# ------------------------------------------------------------------
if(NOT WIN32)
add_executable(test_wirepipe
  src/main/wirepipe_main.cpp
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
  src/classes/pig/c++/osglue.cpp
  src/classes/pig/c++/ptsWirePacket.cpp
  src/classes/pig/c++/ptsWirePipe.cpp
  src/classes/pig/c++/ptsWirePipeTest.cpp
)
add_dependencies(test_wirepipe pigts_codegen)
target_link_libraries(test_wirepipe PRIVATE pig ${TINYSTATE_LIB} Threads::Threads m)
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
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
  src/classes/pig/c++/osglue.cpp
  src/classes/pig/c++/ptsWirePacket.cpp
  src/classes/pig/c++/ptsWirePipe.cpp
  src/classes/pig/c++/wpBigChild.cpp
)
add_dependencies(wp_child pigts_codegen)
target_link_libraries(wp_child PRIVATE pig ${TINYSTATE_LIB} Threads::Threads m)

add_executable(wp_parent
  src/main/wp_parent_main.cpp
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
  src/classes/pig/c++/osglue.cpp
  src/classes/pig/c++/ptsWirePacket.cpp
  src/classes/pig/c++/ptsWirePipe.cpp
  src/classes/pig/c++/wpBigParent.cpp
)
add_dependencies(wp_parent pigts_codegen wp_child)
target_link_libraries(wp_parent PRIVATE pig ${TINYSTATE_LIB} Threads::Threads m)

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
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
  src/classes/pig/c++/osglue.cpp
  src/classes/pig/c++/ptsWirePacket.cpp
  src/classes/pig/c++/ptsWirePipe.cpp
  src/classes/pig/c++/ptsWireCacheStreamWriter.cpp
  src/classes/pig/c++/ptsWireCacheStreamWriterText.cpp
  src/classes/pig/c++/ptsAgentStub.cpp
)
add_dependencies(srava_agent_stub pigts_codegen)
target_link_libraries(srava_agent_stub PRIVATE pig ${TINYSTATE_LIB} Threads::Threads m)

add_executable(test_pigfagent
  src/main/pigfagent_main.cpp
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
  src/classes/pig/c++/osglue.cpp
  src/classes/pig/c++/ptsWirePacket.cpp
  src/classes/pig/c++/ptsWirePipe.cpp
  src/classes/pig/c++/ptsWireCacheStreamReader.cpp
  src/classes/pig/c++/ptsWireCacheStreamReaderText.cpp
  src/classes/pig/c++/pigfFunction.cpp
  src/classes/pig/c++/pigfAgent.cpp
  src/classes/cg/c++/pigfSravaAgent.cpp
  src/classes/cg/c++/cgptsLemonParser.cpp   # pigfAgent が値結果を VALUE パース(対称化)
  src/classes/pig/c++/pigfOps.cpp            # pigDataOperatorVariable(値規則 IDENT 用)
  src/classes/pig/c++/pigfAgentTest.cpp
  ${SRAVA_GEN}/ns_sravaParser.cpp            # 値規則のみ(SRAVA_VALUE_ONLY)
)
add_dependencies(test_pigfagent pigts_codegen srava_agent_stub gen_srava_parser)
target_include_directories(test_pigfagent PRIVATE ${SRAVA_GEN})
target_compile_definitions(test_pigfagent PRIVATE SRAVA_VALUE_ONLY)
target_link_libraries(test_pigfagent PRIVATE pig ${TINYSTATE_LIB} Threads::Threads m)
add_test(NAME pigfagent COMMAND test_pigfagent)
# agent バイナリのパスを test 実行時に注入(pigfAgent が getenv("SRAVA_AGENT") で読む)
set_tests_properties(pigfagent PROPERTIES
  ENVIRONMENT SRAVA_AGENT=$<TARGET_FILE:srava_agent_stub>)

# ------------------------------------------------------------------
# step6 cgatsAgent: dispatch テーブル + 計算本体(ptsCalcBody)を持つ実エージェント骨格
#   srava_agent     : cgatsAgent を起動する実エージェントプロセス(CGAL 未リンク・box=ダミー)
#   test_cgatsagent : pigDataFunction<pigfSravaAgent>(op=box) を compact → srava_agent と往復
# ------------------------------------------------------------------
add_executable(srava_agent
  src/main/srava_agent_main.cpp
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
  src/classes/pig/c++/osglue.cpp
  src/classes/pig/c++/ptsWirePacket.cpp
  src/classes/pig/c++/ptsWirePipe.cpp
  src/classes/pig/c++/ptsWireCacheStreamWriter.cpp
  src/classes/pig/c++/ptsWireCacheStreamWriterText.cpp
  src/classes/pig/c++/ptsWireCacheStreamReader.cpp
  src/classes/cg/c++/ptscgWireCacheStreamWriterMesh.cpp
  src/classes/pig/c++/ptsWireCacheStreamWriterRef.cpp
  src/classes/cg/c++/ptscgWireCacheStreamReaderMesh.cpp
  src/classes/cg/c++/cgMesh.cpp
  src/classes/cg/c++/cgMesh3D.cpp       # cgMesh3D の多態メソッド(booleans/affine/codec/factory)
  src/classes/cg/c++/cgMesh2D.cpp       # cgMesh2D の多態メソッド(2D booleans/affine/codec)
  src/classes/pig/c++/ptsCalcBody.cpp
  src/classes/cg/c++/cgaBox.cpp
  src/classes/cg/c++/cgaPrism.cpp
  src/classes/cg/c++/cgaPyramid.cpp
  src/classes/cg/c++/cgaSphere.cpp
  src/classes/cg/c++/cgaUnion.cpp
  src/classes/cg/c++/cgaCombine.cpp
  src/classes/cg/c++/cgaIntersection.cpp
  src/classes/cg/c++/cgaDifference.cpp
  src/classes/cg/c++/cgaExport.cpp
  $<$<BOOL:${HDF5_FOUND}>:src/classes/cg/c++/cgaVoxelize.cpp>   # HDF5 がある時だけコンパイル
  src/classes/cg/c++/cgaImport.cpp          # import(path): 外部ファイル読み(read_polygon_mesh)
  src/classes/cg/c++/cgaTranslate.cpp       # transform 系: translate/rotate/mirror/scale/transform
  src/classes/cg/c++/cgaRotate.cpp
  src/classes/cg/c++/cgaMirror.cpp
  src/classes/cg/c++/cgaScale.cpp
  src/classes/cg/c++/cgaTransform.cpp
  src/classes/cg/c++/cgaColor.cpp
  src/classes/cg/c++/cgaRect.cpp            # 2D プリミティブ
  src/classes/cg/c++/cgaNgon.cpp
  src/classes/cg/c++/cgaCircle.cpp
  src/classes/cg/c++/cgaPolygon.cpp
  src/classes/cg/c++/cgaLine.cpp
  src/classes/cg/c++/cgaSection.cpp
  src/classes/cg/c++/cgaExtrude.cpp         # 2D→3D 角柱
  src/classes/cg/c++/cgaTube.cpp            # 3D 折れ線まわりの掃引管
  src/classes/cg/c++/cgaRevolve.cpp         # 2D→3D 回転体
  src/classes/cg/c++/cgaOffset.cpp          # 2D オフセット
  src/classes/cg/c++/cgaArea.cpp            # 計測(値返し): area
  src/classes/cg/c++/cgaValid.cpp           # 検査(値返し): valid
  src/classes/cg/c++/cgaRepair.cpp          # 修復(mesh 返し): repair
  src/classes/cg/c++/cgaVolume.cpp          # 計測(値返し): volume
  src/classes/cg/c++/cgaPerimeter.cpp       # 計測(値返し): perimeter
  src/classes/cg/c++/cgaCentroid.cpp        # 計測(配列返し): centroid
  src/classes/cg/c++/cgaBbox.cpp            # 計測(入れ子配列返し): bbox
  src/classes/cg/c++/cgaDistance.cpp        # 近接(値返し): distance
  src/classes/cg/c++/cgaClosest.cpp         # 近接(配列返し): closest
  src/classes/cg/c++/cgaFarthest.cpp        # 近接(配列返し): farthest
  src/classes/cg/c++/cgaThinSpots.cpp       # 肉厚 SDF(入れ子配列返し): thin_spots
  src/classes/cg/c++/cgatsAgent.cpp
  src/classes/cg/c++/cgptsLemonParser.cpp   # inline 引数の value-parse(VALUE モード)
  src/classes/pig/c++/pigfOps.cpp            # pigDataOperatorVariable::_start(value 規則 IDENT 用)
  ${SRAVA_GEN}/ns_sravaParser.cpp            # 共有文法。SRAVA_VALUE_ONLY で値規則のみ有効化
)
add_dependencies(srava_agent pigts_codegen gen_srava_parser)
target_include_directories(srava_agent PRIVATE ${SRAVA_GEN} ${HDF5_C_INCLUDE_DIRS} ${HDF5_INCLUDE_DIRS})
# 同一文法を VALUE 専用にコンパイル(PROGRAM 用 pigf* ノードをリンクしない)。
target_compile_definitions(srava_agent PRIVATE SRAVA_VALUE_ONLY $<$<BOOL:${HDF5_FOUND}>:SRAVA_HAVE_HDF5>)
target_link_libraries(srava_agent PRIVATE pig ${TINYSTATE_LIB} Threads::Threads m CGAL::CGAL ${HDF5_C_LIBRARIES} ${HDF5_LIBRARIES})

add_executable(test_cgatsagent
  src/main/cgatsagent_main.cpp
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
  src/classes/pig/c++/osglue.cpp
  src/classes/pig/c++/ptsWirePacket.cpp
  src/classes/pig/c++/ptsWirePipe.cpp
  src/classes/pig/c++/ptsWireCacheStreamReader.cpp
  src/classes/pig/c++/ptsWireCacheStreamReaderText.cpp
  src/classes/pig/c++/pigfFunction.cpp
  src/classes/pig/c++/pigfAgent.cpp
  src/classes/cg/c++/pigfSravaAgent.cpp
  src/classes/cg/c++/cgptsLemonParser.cpp   # pigfAgent が値結果を VALUE パース(対称化)
  src/classes/pig/c++/pigfOps.cpp            # pigDataOperatorVariable(値規則 IDENT 用)
  src/classes/cg/c++/cgatsAgentTest.cpp
  ${SRAVA_GEN}/ns_sravaParser.cpp            # 値規則のみ(SRAVA_VALUE_ONLY)
)
add_dependencies(test_cgatsagent pigts_codegen srava_agent gen_srava_parser)
target_include_directories(test_cgatsagent PRIVATE ${SRAVA_GEN})
target_compile_definitions(test_cgatsagent PRIVATE SRAVA_VALUE_ONLY)
target_link_libraries(test_cgatsagent PRIVATE pig ${TINYSTATE_LIB} Threads::Threads m)
add_test(NAME cgatsagent COMMAND test_cgatsagent)
set_tests_properties(cgatsagent PROPERTIES
  ENVIRONMENT SRAVA_AGENT=$<TARGET_FILE:srava_agent>)

# ------------------------------------------------------------------
# step17 cgptsPlanner: プランナープロセス srava(ptsApplication 派生 = プランナー実態元祖)
#   起動時スイープ → パーズ(スタブ手組み union(box,box)) → 評価 → 終了時クリーンアップ → exit_code
#   CGAL 非リンク(プランナーは CGAL 非依存)。エージェントは srava_agent を ts2System で起動。
# ------------------------------------------------------------------
add_executable(srava
  src/main/srava_main.cpp
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
  src/classes/pig/c++/osglue.cpp
  src/classes/pig/c++/ptsWirePacket.cpp
  src/classes/pig/c++/ptsWirePipe.cpp
  src/classes/pig/c++/ptsWireCacheStreamReader.cpp
  src/classes/pig/c++/ptsWireCacheStreamReaderText.cpp
  src/classes/pig/c++/pigfFunction.cpp
  src/classes/pig/c++/pigfAgent.cpp
  src/classes/cg/c++/pigfSravaAgent.cpp
  src/classes/pig/c++/pigfPluginAgent.cpp   # プラグイン op を別プロセスへ委ねる薄い agent(planner のみ)
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
  src/classes/cg/c++/cgptsLemonParser.cpp
  src/classes/cg/c++/cgptsPlanner.cpp
  ${SRAVA_GEN}/ns_sravaParser.cpp
)
add_dependencies(srava pigts_codegen srava_agent gen_srava_parser)
target_include_directories(srava PRIVATE ${SRAVA_GEN})
target_link_libraries(srava PRIVATE pig ${TINYSTATE_LIB} Threads::Threads m)

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

# プラグインエージェント疎通(別プロセスの echo プラグインを pigwire で呼ぶ・end-to-end)。
add_test(NAME srava_plugin_echo
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_echo)
set_tests_properties(srava_plugin_echo PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pluginecho;SRAVA_ECHO_AGENT=$<TARGET_FILE:echo_agent>"
  PASS_REGULAR_EXPRESSION "PLUGIN_OK"
  FAIL_REGULAR_EXPRESSION "PLUGIN_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
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
add_test(NAME srava_2d_dxf_export
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> dxf_export)
set_tests_properties(srava_2d_dxf_export PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-2d-dxf"
  PASS_REGULAR_EXPRESSION "result cache"
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
  PASS_REGULAR_EXPRESSION "nv=82 nf=156"
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
# 閉じれば fd は再利用され完走("result cache")。閉じ忘れ=リークがあれば fd が累積枯渇し、launch 失敗で
# "Lower PIG_MAX_WORKERS"(ERROR[)になり FAIL。★ limit 固定方針なので backoff に頼らず cap を明示する。
add_test(NAME srava_fdleak
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> fdleak)
set_tests_properties(srava_fdleak PROPERTIES
  ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-fdleak;PIG_MAX_WORKERS=8"
  PASS_REGULAR_EXPRESSION "result cache="
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
  PASS_REGULAR_EXPRESSION "nv=12 nf=20"
  FAIL_REGULAR_EXPRESSION "Segmentation|Aborted|core dumped|unknown op"
  TIMEOUT 20)

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
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
  src/classes/pig/c++/tsSysRepro.cpp
)
add_dependencies(repro_ts2_sigchld pigts_codegen)
target_link_libraries(repro_ts2_sigchld PRIVATE ${TINYSTATE_LIB} Threads::Threads m)

# ------------------------------------------------------------------
# ts2IO::write_c pipe バッファ超え停止バグの最小再現(ctest 非登録 = ハング前提の手動実行用)
#   TSPIPE_BYTES=131072 timeout 10 ./repro_pipe  → 64KB 超で停止(DONE が出ずハング)
#   TSPIPE_BYTES=32768  ./repro_pipe             → 完走("[repro] DONE wrote ...")
#   TSPIPE_DIVISIBLE=1 で set_divisible の効果を切り分け
# ------------------------------------------------------------------
add_executable(repro_pipe
  src/main/repro_pipe_main.cpp
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
  src/classes/pig/c++/tsPipeRepro.cpp
)
add_dependencies(repro_pipe pigts_codegen)
target_link_libraries(repro_pipe PRIVATE ${TINYSTATE_LIB} Threads::Threads m)

# ------------------------------------------------------------------
# 多数 ts2System 子の一括 teardown レース(worker thread の pipe FIN/abort vs main app teardown)
# の最小再現(ctest 非登録 = 確率的 SEGV のため手動実行用)
#   for i in $(seq 50); do ./repro_teardown >/dev/null 2>&1 || echo "SEGV($?) at $i"; done
#   TSTD_N=48 TSTD_DELAY=300 が既定。N を増やすと出やすい。
# ------------------------------------------------------------------
add_executable(repro_teardown
  src/main/repro_teardown_main.cpp
  src/classes/pig/c++/ptsObject.cpp
  src/classes/pig/c++/ptsApplication.cpp
  src/classes/pig/c++/tsTeardownRepro.cpp
)
add_dependencies(repro_teardown pigts_codegen)
target_link_libraries(repro_teardown PRIVATE ${TINYSTATE_LIB} Threads::Threads m)

# ------------------------------------------------------------------
# プラグイン: pipe_proximity(可変太さパイプの自己接近検出・別 repo MIT・CGAL 非依存)。
#   opt-in。ON で pipe_proximity を FetchContent(v0.1.1 pin)取得し、host 側アダプタ
#   (plugins/pipe_proximity/)と一緒に pipe_proximity_agent をビルドする。
#   依存方向は一方向(cgal-processor → pipe_proximity)。アダプタ + 登録は host 側。
# ------------------------------------------------------------------
option(SRAVA_PLUGIN_PIPEPROX "build pipe_proximity plugin agent (FetchContent)" OFF)
if(SRAVA_PLUGIN_PIPEPROX)
  include(FetchContent)
  FetchContent_Declare(pipeprox
    GIT_REPOSITORY ssh://git@project.globalbase.org/git_repo/proj/gs/pipeProximity.git
    GIT_TAG v0.1.6)
  FetchContent_MakeAvailable(pipeprox)
  # 上流バグ回避(ソースは fork しない・Issue で戻す): pipe_proximity v0.1.1 の bvh.hpp が
  # std::max({...}) を <algorithm> 無しで使う。新しい libstdc++(Debian13)は透過 include しないので
  # pipeprox の各 TU に <algorithm> を強制 include する(ビルドレベルの回避・上流修正で除去可)。
  if(TARGET pipeprox)
    target_compile_options(pipeprox PRIVATE -include algorithm)
  endif()

  # 2 TU 分離: agent(SDK/::pipe)と adapter(pipe ヘッダ/namespace pipe)。namespace pipe ↔ POSIX
  # ::pipe の衝突回避。境界は plain 型(pipe_proximity_adapter.h)。
  add_executable(pipe_proximity_agent
    plugins/pipe_proximity/pipe_proximity_agent.cpp
    plugins/pipe_proximity/pipe_proximity_adapter.cpp)
  target_include_directories(pipe_proximity_agent PRIVATE plugins/pipe_proximity)
  # pig(SDK/値コーデック)+ libtinyState(sPtr)+ pipeprox(MIT)。CGAL も srava もリンクしない。
  target_link_libraries(pipe_proximity_agent PRIVATE pig ${TINYSTATE_LIB} Threads::Threads m pipeprox)

  # 統合テスト(別プロセス起動・自己接近検出が返るか)。
  add_test(NAME srava_plugin_pipeprox
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_pipeprox)
  set_tests_properties(srava_plugin_pipeprox PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pipeprox;SRAVA_PIPEPROX_AGENT=$<TARGET_FILE:pipe_proximity_agent>"
    PASS_REGULAR_EXPRESSION "PIPEPROX_OK"
    FAIL_REGULAR_EXPRESSION "PIPEPROX_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 60)

  # 統合テスト(同一 bin の 2 つ目の op): 距離調整コントローラ pipe_adjust が違反を解消するか。
  add_test(NAME srava_plugin_pipeadjust
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_pipeadjust)
  set_tests_properties(srava_plugin_pipeadjust PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pipeadjust;SRAVA_PIPEPROX_AGENT=$<TARGET_FILE:pipe_proximity_agent>"
    PASS_REGULAR_EXPRESSION "PIPEADJUST_OK"
    FAIL_REGULAR_EXPRESSION "PIPEADJUST_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 60)

  # 統合テスト: 半径プロファイル([s,r] キーポイント線形補間)がスカラ一定と整合するか。
  add_test(NAME srava_plugin_radius
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_radius)
  set_tests_properties(srava_plugin_radius PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-piperadius;SRAVA_PIPEPROX_AGENT=$<TARGET_FILE:pipe_proximity_agent>"
    PASS_REGULAR_EXPRESSION "PIPERADIUS_OK"
    FAIL_REGULAR_EXPRESSION "PIPERADIUS_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 60)

  # 統合テスト(N 体): scene 近接検出 + adjustScene が固定障害物を避けて調整するか。
  add_test(NAME srava_plugin_scene
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_scene)
  set_tests_properties(srava_plugin_scene PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pipescene;SRAVA_PIPEPROX_AGENT=$<TARGET_FILE:pipe_proximity_agent>"
    PASS_REGULAR_EXPRESSION "PIPESCENE_OK"
    FAIL_REGULAR_EXPRESSION "PIPESCENE_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 60)

  # 統合テスト: pipe_sample(弧長等間隔サンプル)がテーパ半径と一定ピッチを返すか。
  add_test(NAME srava_plugin_sample
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_sample)
  set_tests_properties(srava_plugin_sample PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_CACHE_DIR=/tmp/srava-pipesample;SRAVA_PIPEPROX_AGENT=$<TARGET_FILE:pipe_proximity_agent>"
    PASS_REGULAR_EXPRESSION "PIPESAMPLE_OK"
    FAIL_REGULAR_EXPRESSION "PIPESAMPLE_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 60)

  # 統合テスト: 射影的分離パスが、接触する 2 周コイル(ピッチ≈2r)を gap>=dMin へ押し広げるか。
  add_test(NAME srava_plugin_separate
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/srava_parse.sh $<TARGET_FILE:srava> plugin_separate)
  set_tests_properties(srava_plugin_separate PROPERTIES
    ENVIRONMENT "SRAVA_AGENT=$<TARGET_FILE:srava_agent>;SRAVA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/lib;SRAVA_CACHE_DIR=/tmp/srava-pipeseparate;SRAVA_PIPEPROX_AGENT=$<TARGET_FILE:pipe_proximity_agent>"
    PASS_REGULAR_EXPRESSION "PIPESEPARATE_OK"
    FAIL_REGULAR_EXPRESSION "PIPESEPARATE_FAIL|Segmentation|Aborted|core dumped|parse error|unknown op"
    TIMEOUT 60)

  # install: バイナリを libexec/srava/plugins へ、installed bin を指すマニフェストを share/srava/plugins
  # (= registry の既定 sysdir)へ自動生成して配置。→ install 後は環境変数なしで pipe_proximity() が使える。
  #   同一 bin が 5 つの op(検出 / 調整 / scene 検出 / scene 調整 / サンプル)を serve するので、
  #   マニフェストは op ごとに 1 ファイル(規約 = 1 ファイル 1 op)生成する。
  install(TARGETS pipe_proximity_agent RUNTIME DESTINATION libexec/srava/plugins)
  set(_pp_bin "${CMAKE_INSTALL_PREFIX}/libexec/srava/plugins/pipe_proximity_agent")
  function(_pp_manifest op arity comment)
    file(WRITE "${CMAKE_BINARY_DIR}/${op}.plugin"
"# ${comment}(cmake --install が installed bin を指して自動生成)。\n\
op   = ${op}\n\
bin  = ${_pp_bin}\n\
out  = value\n\
args = ${arity}\n")
  endfunction()
  _pp_manifest(pipe_proximity       "1..3" "pipe_proximity: 自己接近検出")
  _pp_manifest(pipe_adjust          "1..6" "pipe_adjust: 距離調整コントローラ(params ハッシュ or 位置引数)")
  _pp_manifest(pipe_scene_proximity "1..2" "pipe_scene_proximity: N 体近接検出")
  _pp_manifest(pipe_scene_adjust    "2..3" "pipe_scene_adjust: N 体距離調整(adjustScene)")
  _pp_manifest(pipe_sample          "1..3" "pipe_sample: 弧長等間隔サンプル([pos,r] を tube へ)")
  install(FILES "${CMAKE_BINARY_DIR}/pipe_proximity.plugin"
                "${CMAKE_BINARY_DIR}/pipe_adjust.plugin"
                "${CMAKE_BINARY_DIR}/pipe_scene_proximity.plugin"
                "${CMAKE_BINARY_DIR}/pipe_scene_adjust.plugin"
                "${CMAKE_BINARY_DIR}/pipe_sample.plugin"
          DESTINATION share/srava/plugins)
endif()
