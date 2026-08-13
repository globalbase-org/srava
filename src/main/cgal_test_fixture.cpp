/*
 * cgal_test_fixture — 単体テスト専用の「最小 cgal 記述子」登録。
 *
 * rev4 Phase D-1 (2026-08-09): 旧 pigfModuleAgent.cpp の cgal placeholder をここへ移設した。
 *   planner (srava) は起動時に cgal.so をロードして実 cgatsAgent_descriptor を register_descriptor
 *   するので placeholder は不要になった。一方、test_pigfagent / test_cgatsagent は
 *     - cgal.so をロードしない (probe/loader を持たない)
 *     - 実 cgatsAgent.cpp (CGAL 本体) も link しない (計算は別プロセス srava_agent に投げる)
 *   ため、planner 側の routing 判断 (decide_out_module/decide_executor) と、外部 agent が書いた
 *   キャッシュ 4CC タグの owner 解決に必要な最小 cgal メタを、テスト自身が registry へ登録する。
 *
 *   実 cgatsAgent_descriptor は OPS[] (幾何本体) を参照し未 link だと undefined ref になるため、
 *   ops=0 の最小 descriptor を使う (routing は codec_tags/import_exts/exec_caps だけで足りる)。
 *   cgatsAgent の静的自己登録 (cross-TU 初期化順リスク) は避け、各 main() から明示呼び出しする。
 */
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigModuleRegistry.h"
#include	"cg/c++/cgptsLemonParser.h"   /* cg_mk_value_parser (テストは lemon パーサをリンク済) */

/* ★ #3427 ③: レジストリは app 所有になったので、fixture は **テスト app の INI** から
 * app のレジストリを引数に呼ばれる (旧: main() から
 * グローバルへ登録)。 */
void
srava_register_cgal_test_fixture(sPtr<pigModuleRegistry> reg)
{
	if ( reg == thNULL )
		return;
	/* ★ P2d: 型名↔4CC を登録 (cgal.so 記述子の types×type_tags と同じ)。これが無いと sig の出力型
	 *   ("cg-mesh3d") を刻んだ継続スタンプが型レジストリ未登録で pig_is_delayed に認識されない
	 *   (旧 fixture は型を持たずカーネル名 "cgal" スタンプに落ちていた)。 */
	reg->types.register_type("cg-mesh3d",  "MESH");
	reg->types.register_type("cg-cross2d", "PLY2");
	/* ★ #3427 ③: VALUE パーサ登録も旧 cgptsLemonParser.cpp 静的初期化から fixture へ
	 *   (テスト実行体 = 言語パーサを持つ実行体)。 */
	reg->vparser.register_parser("srava", &cg_mk_value_parser);
	/* register_descriptor はポインタを保持するので static 記憶域が必須 (旧 placeholder と同様)。 */
	/* ★ P2d: 型軸 routing (decide_executor) が fixture へ振れるよう、テストが routing する op
	 *   (box=leaf 3D / union=二項 3D) に **sig だけ**の最小 ops を付与 (mkCalc=0・planner の routing は
	 *   op 名と sig だけ読む)。旧 ops=0 では decide_executor が -1 を返し入力型 home フォールバック頼みだった。 */
	static const pigOpEntry cgal_test_ops[] = {
		{ "box",   0, 0, (pigArgKind)0, 0, 0, "->cg-mesh3d" },
		{ "union", 0, 0, (pigArgKind)0, 0, 0, "(cg-mesh3d,cg-mesh3d)->cg-mesh3d" },
	};
	static const srava_module_descriptor cgal_test_descriptor = {
		SRAVA_MODULE_ABI, "cgal", 20,
		0 /*make_agent: External*/, (unsigned)EXEC_PROCESS, EXEC_PROCESS,
		cgal_test_ops, (int)(sizeof cgal_test_ops / sizeof cgal_test_ops[0]),
		"off:cg-mesh3d,stl:cg-mesh3d,obj:cg-mesh3d,ply:cg-mesh3d,svg:cg-cross2d,dxf:cg-cross2d",
		"off,stl,obj,ply,3mf,amf,svg,dxf",
		"MESH,PLY2",                /* codec_tags */
		0,                          /* codecs: テストは外部 agent が書いた cache を raw 読みするので factory 不要 */
	};
	reg->register_descriptor(&cgal_test_descriptor);
}
