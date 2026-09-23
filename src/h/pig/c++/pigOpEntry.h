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
#include <string.h>

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
 *   可変長 op (variadic=1 かつ vtail_value=0) の尾部は **最後の要素が繰り返す**。
 *
 * ★★ #3469 (ABI v19・2026-09-01): 1 スロットは **候補列** (0 終端) になった。
 *   export_vox が「メッシュ **または** ボリューム格子」を受けるようになり、
 *   「このスロットはこのクラス 1 つ」では表現できなくなったため。
 *   材料化は候補を **前から順に試し、最初に成功したもの**を使う (ptsGenericAgent)。
 *   ⚠ 単一クラスのスロットは要素 1 の候補列になるだけで、既存の OPS 行は無改変。 */
struct pigOpWiring {
	pigCalcFactory                    mkCalc;
	const pigWireClass* const* const* want;  /* nwant スロット。各要素が 0 終端の候補列 */
	int                               nwant; /* 幾何 cache 引数なしなら 0 (want も 0 可) */
};

/* ★ #3469: 1 スロットが受け入れる本体クラスの **候補列**。
 *   OPS 行では OPWIRE(Calc, cgMesh, PIGWIRE_ANY(cgMesh, vdGrid)) のように混ぜて書ける。 */
template<class... T> struct pigWireAny {
	static constexpr const pigWireClass* L[] = { &T::WIRE..., 0 };
};
#define PIGWIRE_ANY(...) pigWireAny<__VA_ARGS__>

/* 単一クラスを書いたスロットを候補列 1 個へ包む (既存の OPS 行の書き方を保つため)。 */
template<class T> struct pigWireSlotOf            { using type = pigWireAny<T>; };
template<class... T> struct pigWireSlotOf<pigWireAny<T...> > { using type = pigWireAny<T...>; };

/* OPS 行に書く配線。Calc = 計算本体クラス、In... = cache 引数のスロット
 * (本体クラス 1 つ、または PIGWIRE_ANY(...) の候補列)。
 *   { "union", BINMESH_IN, 2, AK_CACHE, OPWIRE(cgaUnion, cgMesh, cgMesh), 1, "…", 1 }
 * ★ 文字列ではなく **クラス**で縛るので、クラスを消せば/改名すればコンパイルが落ちる。 */
template<class Calc, class... In> struct pigOpWire {
	static sPtr<ptsCalcBody>
	mk(sPtr<ptsObject> p, sArray<sPtr<pigData> >* a, sPtr<stdString> t) { return thNEW(Calc,(p, a, t)); }
	static constexpr const pigWireClass* const* W[] = { pigWireSlotOf<In>::type::L..., 0 };
	static constexpr pigOpWiring WIRING             = { &mk, W, (int)sizeof...(In) };
};
#define OPWIRE(Calc, ...) (&pigOpWire<Calc __VA_OPT__(,) __VA_ARGS__>::WIRING)

/* ★★ #3554 段1 (2026-09-19): **op の行は `op` と `op#変種` の 2 形**。
 *   routing は基底名で引くとき `op` と `op#…` を **前方一致で集め**、OPS に書かれた順に
 *   照合して **最初に成立した 1 行**を採り、*その行名をそのまま C_OP に載せる*。
 *   ⇒ agent 側の lookup_op は完全一致のままで当たる (名前が別なので「先勝ちで 2 行目が
 *     静かに死ぬ」罠が構造的に消える) / 行ごとに sig・wiring・in[] を持てる /
 *     op 名は結果ハッシュの先頭に混ざるので **キャッシュキーが自動で分かれる**。
 *   ⚠ `#` は利用者には書けない (識別子は [A-Za-z_][A-Za-z0-9_]*)。**書けないが、隠さない**。
 *   ★★ 表示規約 (ひさ 2026-09-19 確定): 勝った行名は @ps@ ・ 診断 ・ キャッシュキーに
 *     **そのまま出す**。`cast#cg-cross2d` / `transform#xy` が利用者の目に触れてよい。
 *     ⇒ 畳んで基底名 (`cast`) に見せると、*どの行が選ばれたか* を外から確かめる手段が無くなる —
 *       値の中身で配線が変わる機能 (AK_MATCH) では、そこが唯一の観測点になる。
 *     ⚠ 「書けない名前を見せるのは不親切」より **「実際に走ったものを名乗る」**を採る。
 *   ★ なおエラー文の前置き (`cgal/cast: ...`) はここではなく **op 本体が書いた文字列**なので、
 *     行名は出ない (2026-09-19 実測)。*名乗り方が 2 通りある*ことを承知しておくこと。
 *   ⚠ **無条件の行は常に成立するので、変種より前に置くと覆い隠す** ⇒ 記述子のロード時に
 *     静的検査で弾く (pig_descriptor_violation)。
 *   ★ 行数に **上限は無い** — routing は registry の op_row() で 1 行ずつ引く。 */
/* 行名 row が 基底名 base の行か: `base` そのもの、または `base#…`。 */
inline int
pig_op_row_is(const char *row, const char *base)
{
	if ( row == 0 || base == 0 ) return 0;
	size_t bl = ::strlen(base);
	if ( ::strncmp(row, base, bl) != 0 ) return 0;
	return ( row[bl] == '\0' || row[bl] == '#' ) ? 1 : 0;
}

/* 行名の `#` の位置 (無ければ 0)。変種名を読む側はここから後ろを見る。 */
inline const char *
pig_op_variant_of(const char *row)
{
	if ( row == 0 ) return 0;
	const char *h = ::strchr(row, '#');
	return ( h != 0 ) ? h + 1 : 0;
}

/* ★★ #3554 段2 (2026-09-19): **引数の値の中身を見るマッチ関数** (AK_MATCH)。
 *   sig は *幾何引数の型* しか見ないので、「値の中身で行き先を変える」がこれまで書けなかった
 *   (@transform(cross2d, mx)@ の mx が xy 平面に帰着するか / @export@ の拡張子 / @cast@ の目標型)。
 *
 *   int match(d, e, argNo, arg);   マッチ 1 / 非マッチ 0
 *
 *   ★ @e@ (行そのもの) が効く — @e->op@ の @#@ の後ろを読めば **1 本の汎用関数で全変種を
 *     賄える** (@export#stl@ / @cast#cg-mesh3d@ …)。無いと変種ごとに C の関数が 1 本ずつ要る。
 *     (既存の @pig_wire_match@ が引数を取らないのは *テンプレートでクラスごとに生成される*から。
 *      **共有される関数は文脈を引数で貰うしかない**。)
 *   ★ @d@ … モジュール名 (診断) と設定。routing のループは記述子を持っているのでただで渡せる。
 *   ★ @argNo@ … 同じ関数を複数スロットに付けたとき / 可変長の尾部 / 「argument N が…」の文言。
 *
 *   ⚠⚠ **純粋であること** (副作用なし・値を読むだけ)。候補ごとに *何度でも呼ばれうる*。
 *   ⚠⚠ 値は **@compact()@ して読む。決まらなければ決まるまで待つ** — 「読めたときだけ見る」は
 *     *キャッシュの温度で routing が変わる*ので採らない (2026-08-19 に 4CC フォールバックで
 *     同じ轍を踏んでいる)。⇒ routing は TS_STATE の中なので yield しても状態が再走するだけ。
 *     ★ ただし **再走に耐える書き方**でなければならない (2026-09-18 の #3511 の件)。
 *   ⚠ 待つのは @pigDataDelay@ (評価待ち) であって、継続 pair (キャッシュ実体待ち) ではない。 */
typedef int (*pigOpMatchFn)(const struct srava_module_descriptor *d, const struct pigOpEntry *e,
                            int argNo, sPtr<pigData> arg);

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
	 *       可換 ON  … **書かれた順のまま**均衡 k 分木 (a,b,c と c,b,a は別の木)
	 *       可換 OFF … 順序保持の左 fold を k 個ずつ (difference)
	 *   ★ #3500: 可換でも **引数は並べ替えない**。以前は get_hashkey() 昇順にソートしていたが、
	 *     畳む順で中間結果の大きさ = 実行時間が変わるうえ、そのハッシュは記述子の指紋
	 *     (名前 + cache_version) を含むので **幾何に無関係な版を上げるだけで木が変わっていた**。
	 *     正規化は二項ノードのキャッシュキー (compute_arg_hash) にだけ残す。
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

	/* ★ #3474 続き (ひさ設計 2026-09-05): **必須の引数個数**。0 = 既定 = 「nin 個すべて必須」
	 *   なので既存の OPS 行は 1 行も書き換えずに従来どおりの意味になる。
	 *
	 *   ⚠ これが無いあいだ、「省略できる引数」を表現する手段が記述子に無かった。arity 検査が
	 *     `n != nin` の **完全一致**を要求するので、`sphere(r)` のような省略形はパーサ側で
	 *     **固定 arity のノードに組み直して**通すしかなく、その特例が
	 *       pushArg(arg0); if (na>=2) pushArg(arg1); else pushArg(既定);
	 *     と添字直書きだったため **index 2 以降が黙って捨てられて**いた
	 *     (`sphere(1,32,5)` が第 3 引数を捨てて通る。検査が見るのは組み上がったノードの args
	 *      なので、検査より上流で消えた引数は誰にも気づかれない)。
	 *
	 *   ★★ 既定値を **パーサが持てない**のが本質 (ひさ指摘)。パーサはどのモジュールが実行するか
	 *     知らない (routing は eval 時) のに、同じ op 名でも引数の意味がモジュールで違う:
	 *         sphere(r, seg)  … メッシュ系。seg は省略可
	 *         sphere(r, dx)   … openvdb。dx はボクセルサイズで **省略できない**
   	 *     ⇒ 「何個まで省略してよいか」は **そのモジュールの記述子**が言い (nreq)、
	 *        **既定値そのものは op の compute() が入れる** (どの op も既に
	 *        `( na > k ) ? … : 既定` の形で持っている)。パーサは来た引数をそのまま渡すだけ。
	 *
	 *   検査: n > nin → too many / n < (nreq ? nreq : nin) → too few / その間は OK。
	 *   ⚠ **必ず末尾に足すこと** (OPS[] は位置指定初期化子・commutative / vtail_value と同じ理由)。 */
	int               nreq;

	/* ★★ #3554 段2 (ABI v32): **この行のマッチ関数** (1 本)。0 = 値の中身を見ない = *無条件の行*。
	 *   非 0 なら **引数 1 個ずつについて呼ばれ**、*全部真ならその行が成立*する。
	 *   ⇒ 見たくないスロットは @argNo@ で分岐して 1 を返す:
	 *       static int ext_stl(d,e,argNo,arg) { return (argNo != 0) ? 1 : …拡張子判定…; }
	 *   ★ **配列にしない理由** (ひさ 2026-09-19): スロットごとの配列にすると
	 *     ① @in[]@ と長さがずれても **誰も検査できない** (ポインタなので要素数が分からない)
	 *     ② @variadic@ の尾部に当てられない (@nin@ 個しか無いため)
	 *     1 本 + @argNo@ 分岐なら **どちらも起きない**。しかも「1 本の汎用関数で全変種を賄う」
	 *     (@e->op@ の @#@ の後ろを読む) という設計と一貫する。
	 *   ⚠ **必ず末尾に足すこと** (OPS[] は位置指定初期化子)。既存の行は 0 のままで意味不変。
	 *   ⚠ routing は「**マッチ関数 ∧ sig**」で行を選ぶ。どちらか一方でも外れればその行は不成立。 */
	pigOpMatchFn      match;
};

/* ★★ #3570 段4 / #3572: 行が **何も申告していない** か = *sig だけの行*。
 *   @in[]@ も @nin@ も無く、計算本体の配線 (@wiring@) も持たず、可変長でもない
 *   (テスト fixture の @{ "box", 0, 0, (pigArgKind)0, 0, 0, "->cg-mesh3d" }@ 等)。
 *
 *   ⚠⚠ **申告していないものを申告 0 と読んではいけない**。そう読むと
 *     引数の数    … 「0 引数だけを受ける行」になって routing から落ちる (#3570 段4)
 *     出力の種別  … @out@ が 0 = AK_INLINE = 「値を返す」になり、キャッシュ出力が消える
 *                   (#3572 の out_cache 決め直しで cgatsagent の fixture が全滅した)
 *   ⚠ 本当に 0 引数の op (@empty2d@ / @empty3d@) とは **wiring の有無**で分かれる —
 *     あちらは @OPWIRE(cgaEmpty3D)@ を持つので「0 個だけを受ける」が正しい申告になる。
 *   ★ 判定をこの 1 本に寄せてある。増やすと「申告の読み方」が場所ごとに割れる。 */
inline int
pig_op_row_declares_nothing(const pigOpEntry *e)
{
	return ( e != 0 && e->in == 0 && e->nin == 0 && e->wiring == 0 && ! e->variadic ) ? 1 : 0;
}

#endif
