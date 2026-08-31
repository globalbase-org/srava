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
#include "pig/c++/pigCacheCodec.h"   /* pigCacheReaderFn (配線が持つ stream reader) */
#include <stdint.h>

class ptsCalcBody;
class ptsObject;
class pigData;
class stdString;

/* 引数の受け取り種別: INLINE=値リテラル(構造値) / CACHE=上流結果の pigDataCache ハンドル。 */
enum pigArgKind { AK_INLINE = 0, AK_CACHE = 1 };

/* 計算本体 (ptsCalcBody 派生) の生成子。親・引数配列(ポインタ)・目標キャッシュパスを取る。 */
typedef sPtr<ptsCalcBody> (*pigCalcFactory)(sPtr<ptsObject>, sArray<sPtr<pigData> >*, sPtr<stdString>);

/* ★ 2026-08-28 (ひさ設計・ABI v12): **op の引数配線**。
 *
 *   op の計算本体は compute() の中で必ず特定の pigDataWireTyped 派生を d_cast する
 *   (cgaUnion なら cgMesh・d4aMerge なら d4Mesh)。「その op がいま何の型を欲しいか」は
 *   本体クラスが知っている唯一の事実で、codec 表からも sig からも導けない
 *   (writer を持たない検査専用モジュール / reader を持たない生成専用モジュール /
 *    codec を 1 つも持たないモジュールが、いずれも現に成立するため)。
 *   そこで **欲しいクラスを OPS 表へ直接配線する**。
 *
 *   配線の実体は各本体クラスの static ファクトリ create_for_meta(4CC) で、
 *   「その形式を自分として実体化できるか」を判定して具象インスタンスを返す。
 *   ★ 型名の一覧を別途申告する必要はない — 返ってきたインスタンスに type_name() を
 *     訊けばよい (cgMesh のように抽象基底 1 つが複数の型名を持つ場合も、具象が返るので正しく引ける)。 */
typedef sPtr<pigData> (*pigWireFactoryFn)(const uint8_t* meta, int len);

/* 本体クラス T の create_for_meta を pigWireFactoryFn の形に揃える thunk。
 * sPtr<T> → sPtr<pigData> は sPtr の変換コンストラクタが担う。 */
template<class T> sPtr<pigData>
pig_wire_factory(const uint8_t* meta, int len) { return T::create_for_meta(meta, len); }

/* ★ 本体クラス 1 階層ぶんの配線先。**reader はクラスに帰属する** — 各モジュールの codec 行が
 *   持っていた mkReader は、どの行でも同じ 1 本 (cgal なら cg_mk_reader) で、実体は
 *   その階層の stream reader (ptscgWireCacheStreamReaderMesh) だった。
 *   create が「この 4CC を自分として実体化できるか」を答え、mkReader が復号を駆動する。
 *   ★ writer / match も階層に帰属する。codec 表では openvdb だけ writer 行が 2 本あったが
 *     (vd-grid / vd-mesh)、**両行の writer は同一**で match だけが具象で分かれていた。
 *     vdGrid も vdMesh も vdGeom 派生なので、根の d_cast 1 つが両方を覆う。 */
template<class T> int
pig_wire_match(sPtr<pigData> body) { return sPtr<T>::d_cast(body).is_notNull(); }

struct pigWireClass {
	/* ★ 2026-08-28 (ひさ指摘): 診断で階層を識別するための **クラス名**。
	 *   PIG_WIRE_DEF が **クラス名トークンをそのまま文字列化**するので、申告のずれは起きない。
	 *   これが無いと `srava --module-info` の wires が [0] [1] としか出せず、
	 *   codecs の並びと **対応していると誤読させる** (対応していない — openvdb は codec 2 行に
	 *   対し wire 1 つ。vdGrid と vdMesh が同じ vdGeom 階層だから)。 */
	const char*      name;
	pigWireFactoryFn create;     /* = &pig_wire_factory<T> (T::create_for_meta) */
	pigCacheReaderFn mkReader;   /* この階層の stream reader 生成子 */
	pigCacheWriterFn mkWriter;   /* この階層の stream writer 生成子 (0 = 書けない) */
	pigCacheMatchFn  match;      /* = &pig_wire_match<T> (body がこの階層か) */
};

/* 階層の根に置く WIRE の定義。クラス・reader・writer だけを書けばよい。
 *   PIG_WIRE_DEF(cgMesh, cg_mk_reader, cg_mk_writer);
 * ★ name / create / match はクラスから生成されるので、手で書くのは reader/writer の 2 本だけ。 */
#define PIG_WIRE_DEF(Cls, rd, wr) \
	const pigWireClass Cls::WIRE = { #Cls, &pig_wire_factory<Cls>, &rd, &wr, &pig_wire_match<Cls> }

/* op 1 個ぶんの配線: 計算本体の生成子と、**cache 引数**が欲しい本体クラスの列。
 * ⚠ want は **cache 引数 (AK_CACHE) だけ**を出現順に並べる (AK_INLINE の位置は数えない)。
 *   可変長 op (variadic=1 かつ vtail_value=0) の尾部は **最後の要素が繰り返す**。 */
struct pigOpWiring {
	pigCalcFactory              mkCalc;
	const pigWireClass* const*  want;    /* nwant 個。幾何 cache 引数なしなら 0 */
	int                         nwant;
};

/* OPS 行に書く配線。Calc = 計算本体クラス、In... = cache 引数が欲しい本体クラス。
 *   { "union", BINMESH_IN, 2, AK_CACHE, OPWIRE(cgaUnion, cgMesh, cgMesh), 1, "…", 1 }
 * ★ 文字列ではなく **クラス**で縛るので、クラスを消せば/改名すればコンパイルが落ちる。 */
template<class Calc, class... In> struct pigOpWire {
	static sPtr<ptsCalcBody>
	mk(sPtr<ptsObject> p, sArray<sPtr<pigData> >* a, sPtr<stdString> t) { return thNEW(Calc,(p, a, t)); }
	static constexpr const pigWireClass* W[] = { &In::WIRE..., 0 };
	static constexpr pigOpWiring WIRING       = { &mk, W, (int)sizeof...(In) };
};
#define OPWIRE(Calc, ...) (&pigOpWire<Calc __VA_OPT__(,) __VA_ARGS__>::WIRING)

/* op 名 → 入力型列 / 出力型 / 計算本体生成子 の対応 1 行。 */
struct pigOpEntry {
	const char*       op;        /* 演算子名(キー) */
	const pigArgKind* in;        /* 入力型リスト(固定先頭 nin 個) */
	int               nin;       /* 固定入力数 */
	pigArgKind        out;       /* 出力型 */
	/* ★ v12: 旧 pigCalcFactory mkCalc を **配線ポインタ**へ置き換えた (ひさ設計 2026-08-28)。
	 *   計算本体の生成子と「引数として欲しい本体クラス」を 1 箇所にまとめる。OPWIRE() で書く。
	 *   0 可 (OPS dispatch を持たない demo / pipe_proximity・sig だけの試験用 ops)。 */
	const pigOpWiring* wiring;
	int               variadic;  /* 1=nin 個の固定引数の後ろに AK_CACHE(mesh)を可変個。既定 0 */

	/* ★ rev4 Phase B: **幾何型シグネチャ** (実装型名・タグと 1:1)。decide_executor が (op, 入力型[])
	 *   を直接 handler へ振るのに使う。書式 = "(in1,in2,...)->out"。複数シグネチャは ';' 区切り
	 *   (例 offset= "(cg-cross2d)->cg-cross2d;(cg-mesh3d)->cg-mesh3d")。列挙するのは **幾何型 (mesh) 入力のみ**
	 *   (スカラ/値の inline 引数は型を持たない=省略)。出力が値 (体積等) の op は out に "value"。
	 *   0 = 未指定 (レガシー/未注釈)。routing の実消費は B-2 の decide_executor から (B-1 は付与のみ)。
	 *
	 * ★ #3436 P4 (2026-08-25): 文法を形式化し可変長を 2 記法にした (docs/sig_grammar_design.md §3)。
	 *     固定形     "(a,b)->c"             位置と個数が確定
	 *     繰り返し形 "(f…,{a,b}...)->ref"   末尾が 1 個以上。**分解も昇格もしない** (export_vox)
	 *     fold 形    "(f…,[a,b](N))->a"     2〜N 項。先頭 a = **主型**。木に分解してよい (union 等)
	 *   ⚠ 旧記法 "T..." は "{T}..." の糖衣なので既存 sig はそのまま有効。
	 *   ⚠ 2 記法は統合できない — 分解の可否・主型の有無・昇格を宣言するか、が違う (§3.4)。 */
	const char*       sig;

	/* ★ #3436 P4 (docs/sig_grammar_design.md §5.3): **可換か** (1=可換・既定 0)。
	 *   fold 形の op を木に分解するときの形を決める:
	 *       可換 ON  … get_hashkey() 昇順にソート → 均衡 k 分木 (a,b,c と c,b,a が同一木)
	 *       可換 OFF … 順序保持の左 fold を k 個ずつ (difference)
	 *   キャッシュキーの正規化 (pigfAgent::compute_arg_hash) もこの申告を見る。★ どちらも eval 時
	 *   (pigfModuleAgent::try_decompose / pigfAgent::compute_arg_hash) にしか正しく引けない —
	 *   #3452 でモジュール登録が起動時 eager-load から eval 時の module() 呼び出しへ移ったため、
	 *   parse 直後にこの申告を読もうとすると (旧 pigDataOperator::normalize()) 何もロードされて
	 *   おらず常に「非可換」を返す回帰になった (撤去済み)。
	 *   ⚠ **必ず末尾に足すこと**。OPS[] は位置指定の初期化子なので、途中に挿げると静かにずれる
	 *     (occt の記述子で実際に踏んだ・int/0 互換なのでコンパイラも版番号も止めない)。 */
	int               commutative;

	/* ★ #3436 P4 §6.2: **可変部 (variadic) の引数種別**。0 = 既定 = AK_CACHE (幾何)、
	 *   1 = AK_INLINE (値)。
	 *   ⚠ これが無いあいだ、`variadic` は「nin の後ろに mesh を可変個」としか書けず、実際には
	 *     **値を可変個取る op** (demo_add / pipe_proximity 等) がそれを名乗っていた = 記述子の嘘。
	 *     planner 側に引数種別の検査を置いた瞬間に、その嘘が正しい呼び出しを弾いた。
	 *   ⚠ 末尾に足すこと (OPS[] は位置指定初期化子)。既定 0 が従来の意味と一致する。 */
	int               vtail_value;
};

#endif
