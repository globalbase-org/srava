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
	/* ★ #3439 ②: 型名↔4CC は **記述子の types×type_tags** で申告する (下の descriptor)。
	 *   旧実装はここで型軸レジストリへ直接登録していたが、派生テーブルを廃したので経路は
	 *   register_descriptor の 1 本になった。 */
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
		/* ★ #3440: pigfAgentTest の往復ノードが使う値 op。幾何型入力を取らないので **入力 0 個の
		 *   sig** で routing される (値/文字列の引数は型を持たない = sig に列挙しない規約)。
		 *   これが無いと「型で解決できない呼び出しは明示エラー」に引っかかる。 */
		{ "test_echo", 0, 0, (pigArgKind)0, 0, 0, "->value" },
	};
	/* ★ 2026-08-28 (ABI v11): 旧 types/type_tags ("cg-mesh3d,cg-cross2d" / "MESH,PLY2") の置き換え。
	 *   所有型の申告は codecs 行 (types × tags) が兼ねるようになったので、型を名乗るには
	 *   1 行必要になる。factory は 0 のまま — このテストは外部 agent が書いた cache を raw 読みする
	 *   ので wires は 0 = reader/writer を持たない (旧 codecs=0 と同じ挙動)。これが無いと sig の出力型を刻んだ
	 *   継続スタンプが型未登録で pig_is_delayed に認識されない (P2d)。 */
	/* ★ ABI v16: **型名を名乗るなら階層を名指しする**必要がある (wire が行の識別子 = 番兵)。
	 *   このテストは外部 agent が書いた cache を raw 読みするだけで reader/writer/create を持たない。
	 *   名前だけの wire を置いて型名の申告を成立させる。
	 *   ⚠ ここが空だと type_is_known が cg-mesh3d を知らず、pig_is_delayed が継続 pair を
	 *     見分けられなくなる (cgatsagent の T1/T3 が落ちて気づいた)。 */
	static const pigWireClass cgal_test_wire = { "cgMesh(test fixture)", 0, 0, 0, 0 };
	static const pigModuleType cgal_test_provides[] = {
		{ &cgal_test_wire, "cg-mesh3d,cg-cross2d", 0 /* tags: create が無いので列挙しない */ },
		{ 0, 0, 0 },
	};
	static const srava_module_descriptor cgal_test_descriptor = {
		SRAVA_MODULE_ABI, "cgal", 20,
		0 /*make_agent: External*/, (unsigned)EXEC_PROCESS, EXEC_PROCESS,
		cgal_test_ops, (int)(sizeof cgal_test_ops / sizeof cgal_test_ops[0]),
		"off:cg-mesh3d,stl:cg-mesh3d,obj:cg-mesh3d,ply:cg-mesh3d,svg:cg-cross2d,dxf:cg-cross2d",
		"off,stl,obj,ply,3mf,amf,svg,dxf",
		cgal_test_provides,         /* provides: 階層 × 型名 × 4CC (ABI v16) */
		0,                          /* hash_salt: 基準カーネル */
	};
	reg->register_descriptor(&cgal_test_descriptor);
}
