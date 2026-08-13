#ifndef ___pigOpEntry_H___
#define ___pigOpEntry_H___
/*
 * pigOpEntry — エージェント op ディスパッチ表の共通エントリ型 (.so 化 Phase1-4)。
 *   cgatsAgent / mfatsAgent が各自持っていた同型の ArgKind / CalcFactory / cgaOpEntry を
 *   pig 層へ格上げして 1 つにする。docs/agent_so_design.md の descriptor.ops はこの型を使う。
 *
 * ★ Phase 1 は機能不変: cg/mf は enum/typedef/struct 定義を消してこのヘッダを include し、
 *   旧名 (ArgKind / CalcFactory / cgaOpEntry) をこの共通型の typedef 別名として残すだけ
 *   (OPS テーブル本体と dispatch コードは無改修)。計算本体生成子 thunk (mkCalcT) は
 *   各エージェント固有のクラスに依存するので各 .cpp に据え置く。
 *
 * enum 値名 (AK_INLINE / AK_CACHE) は cg/mf の既存参照と一致させるため据え置く。
 */
#include "ts2/c++/sPtr.h"
#include "ts2/c++/sArray.h"

class ptsCalcBody;
class ptsObject;
class pigData;
class stdString;

/* 引数の受け取り種別: INLINE=値リテラル(構造値) / CACHE=上流結果の pigDataCache ハンドル。 */
enum pigArgKind { AK_INLINE = 0, AK_CACHE = 1 };

/* 計算本体 (ptsCalcBody 派生) の生成子。親・引数配列(ポインタ)・目標キャッシュパスを取る。 */
typedef sPtr<ptsCalcBody> (*pigCalcFactory)(sPtr<ptsObject>, sArray<sPtr<pigData> >*, sPtr<stdString>);

/* op 名 → 入力型列 / 出力型 / 計算本体生成子 の対応 1 行。 */
struct pigOpEntry {
	const char*       op;        /* 演算子名(キー) */
	const pigArgKind* in;        /* 入力型リスト(固定先頭 nin 個) */
	int               nin;       /* 固定入力数 */
	pigArgKind        out;       /* 出力型 */
	pigCalcFactory    mkCalc;    /* 計算本体生成子 */
	int               variadic;  /* 1=nin 個の固定引数の後ろに AK_CACHE(mesh)を可変個。既定 0 */

	/* ★ rev4 Phase B: **幾何型シグネチャ** (実装型名・タグと 1:1)。decide_executor が (op, 入力型[])
	 *   を直接 handler へ振るのに使う。書式 = "(in1,in2,...)->out"。複数シグネチャは ';' 区切り
	 *   (例 offset= "(cg-cross2d)->cg-cross2d;(cg-mesh3d)->cg-mesh3d")。列挙するのは **幾何型 (mesh) 入力のみ**
	 *   (スカラ/値の inline 引数は型を持たない=省略)。出力が値 (体積等) の op は out に "value"。
	 *   0 = 未指定 (レガシー/未注釈)。routing の実消費は B-2 の decide_executor から (B-1 は付与のみ)。 */
	const char*       sig;
};

#endif
