/*
 * cgatsAgent — srava-agent 本体(ptsApplication 派生 = エージェントプロセスの実態元祖)。
 *   ptsAgentStub(echo)を拡張し、ディスパッチ
 * テーブルで演算子ごとの計算本体(ptsCalcBody 派生)を起動する。
 *
 * 配線: 子プロセスとして起動され、自 stdin(fd0)=rio / stdout(fd1)=wio に ptsWirePipe 1 本。
 *
 * 流れ:
 *   INI      : pipe を立てる
 *   WAIT     : C_OP で OPS 検索(無→A_ERROR)。C_ARG_* を型リストと照合して収集(狂い→A_ERROR)。
 *              pigDataCache 入力は reader を開始(Stage2; box は無し)。C_ARG_END で計算本体起動。
 *   CALC     : 計算本体の TSE_RETURN を待ち、結果を引く。Writer を起こす
 *   WRITING〜: cache(mesh)出力は writer の TSE_ASSERT(header+meta 書込済)で **A_SAVE_BEGIN を先に**
 *              送り(下流が書込中 attach 可=同時ストリーミング)、writer 本体完了(TSE_RETURN)を待って
 *              A_SAVE_DONE。値(インライン)出力は全書込完了後に本文相乗りの A_SAVE_BEGIN。/BYE/wend
 *              (ev 非依存・1 状態 1 write)
 *   ERROR    : A_ERROR + wend
 *
 * 出力シリアライズ(確認①): 値(インライン)は calc の result(pigData)を WriterText で保存し
 *   A_SAVE_BEGIN に本文相乗り。mesh(cache)は calc が parent=cgatsAgent で生成した WriterMesh を
 *   get_writer() で受け取り、D_META "MESH" + D_CHUNK(厳密有理数フレーミング)でストリーム書き込み。
 *
 * ディスパッチ: 演算子名→{入力型リスト, 出力型, 入力 reader 生成子, 計算本体生成子}。生成子は
 *   テンプレート thunk(各クラスに static New 不要)。型は当面 {INLINE, CACHE} の 2 値。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* 基底(エージェントプロセスの実態元祖) */
#include	"ts2/c++/tsApplication.h"    /* ctor の parent 型 sPtr<tsApplication> */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigwire.h"
#include	"pig/c++/ptsWirePipe.h"
#include	"pig/c++/ptsWirePacket.h"
#include	"pig/c++/ptsWireCacheStreamWriter.h"       /* writer メンバの基底型(WriterText/WriterMesh) */
#include	"pig/c++/ptsWireCacheStreamWriterText.h"   /* 値(インライン)出力の保存 */
#include	"pig/c++/ptsWireCacheStreamReader.h"       /* ReaderFactory の戻り型 */
#include	"cg/c++/ptscgWireCacheStreamReaderMesh.h"   /* cache 入力の読み(mesh バイナリ D_CHUNK) */
#include	"pig/c++/ptsCalcBody.h"
#include	"cg/c++/cgaBox.h"
#include	"cg/c++/cgaPrism.h"
#include	"cg/c++/cgaPyramid.h"
#include	"cg/c++/cgaSphere.h"
#include	"cg/c++/cgaUnion.h"
#include	"cg/c++/cgaCombine.h"
#include	"cg/c++/cgaIntersection.h"
#include	"cg/c++/cgaDifference.h"
#include	"cg/c++/cgaExport.h"
#ifdef SRAVA_HAVE_HDF5
#include	"cg/c++/cgaVoxelize.h"
#endif
#include	"cg/c++/cgaImport.h"
#include	"cg/c++/cgaTranslate.h"   /* transform 系: 1 mesh + スカラ */
#include	"cg/c++/cgaRotate.h"
#include	"cg/c++/cgaMirror.h"
#include	"cg/c++/cgaScale.h"
#include	"cg/c++/cgaTransform.h"
#include	"cg/c++/cgaColor.h"       /* color(mesh, c): 面色 f:color */
#include	"cg/c++/cgaRect.h"        /* 2D プリミティブ */
#include	"cg/c++/cgaNgon.h"
#include	"cg/c++/cgaCircle.h"
#include	"cg/c++/cgaPolygon.h"
#include	"cg/c++/cgaLine.h"
#include	"cg/c++/cgaSection.h"
#include	"cg/c++/cgaExtrude.h"     /* 2D→3D */
#include	"cg/c++/cgaTube.h"        /* 3D 折れ線まわりの掃引管 */
#include	"cg/c++/cgaRevolve.h"
#include	"cg/c++/cgaOffset.h"
#include	"cg/c++/cgaArea.h"        /* 計測(値返し op): area(m) */
#include	"cg/c++/cgaValid.h"       /* 検査(値返し op): valid(m) */
#include	"cg/c++/cgaRepair.h"      /* 修復(mesh 返し op): repair(m) */
#include	"cg/c++/cgaVolume.h"      /* 計測(値返し op): volume(m) */
#include	"cg/c++/cgaPerimeter.h"   /* 計測(値返し op): perimeter(m) */
#include	"cg/c++/cgaCentroid.h"    /* 計測(配列返し op): centroid(m) */
#include	"cg/c++/cgaBbox.h"        /* 計測(入れ子配列返し op): bbox(m) */
#include	"cg/c++/cgaDistance.h"    /* 近接(値返し op): distance(a,b) */
#include	"cg/c++/cgaClosest.h"     /* 近接(配列返し op): closest(a,b) */
#include	"cg/c++/cgaFarthest.h"    /* 近接(配列返し op): farthest(a,b) */
#include	"cg/c++/cgaThinSpots.h"   /* 肉厚 SDF(入れ子配列返し op): thin_spots(m,t) */
#include	"cg/c++/cgptsLemonParser.h"   /* inline 引数を VALUE モードで value-parse(構造値復元) */
#include	"ts2/c++/s2IOstd.h"          /* 自 stdin/stdout を portable に ts2IO 化(MinGW 対応) */
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/cgatsAgent_.h"

#include	<string.h>
#include	<stdlib.h>   /* getenv(テスト用フォールトインジェクション) */
#include	<unistd.h>   /* usleep(テスト用の計算遅延) */
#include	<stdio.h>
#include	<sys/time.h>

CLASS_TINYSTATE(cg/c++/cgatsAgent,pig/c++/ptsApplication)

/* PIG_TIMING にファイルパスを設定すると、各フェーズ境界の経過 ms をそのファイルへ追記する
 * (性能内訳の計測用。agent は sh -c 経由起動で stderr が親に届かないためファイル出力)。 */
static void cgts_timing(const char* tag) {
	const char* path = ::getenv("PIG_TIMING");
	if ( path == 0 || path[0] == 0 ) return;
	static double t0 = -1.0;
	struct timeval tv; ::gettimeofday(&tv, 0);
	double now = tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
	if ( t0 < 0 ) t0 = now;
	FILE* f = ::fopen(path, "a");
	if ( f ) { ::fprintf(f, "[timing pid=%d] %-14s %9.1f ms\n", (int)::getpid(), tag, now - t0); ::fclose(f); }
}

/* ---- ディスパッチテーブル(ファイルスコープ) ---- */
enum ArgKind { AK_INLINE = 0, AK_CACHE = 1 };

typedef sPtr<ptsCalcBody> (*CalcFactory)(sPtr<ptsObject>, sArray<sPtr<pigData> >*, sPtr<stdString>);
typedef sPtr<ptsWireCacheStreamReader> (*ReaderFactory)(sPtr<ptsObject>, sPtr<stdString>);

/* テンプレート thunk: 各クラスに static New を書かずコンストラクタ呼びを生成。
 * 入力は **ポインタ** で渡す(親 cgatsAgent が所有・寿命中生存。sArray の値渡し/コピーは避ける)。 */
template<class T> static sPtr<ptsCalcBody>
mkCalcT(sPtr<ptsObject> p, sArray<sPtr<pigData> > *a, sPtr<stdString> t) { return thNEW(T,(p, a, t)); }
template<class T> static sPtr<ptsWireCacheStreamReader>
mkReaderT(sPtr<ptsObject> p, sPtr<stdString> path) { return thNEW(T,(p, path)); }

struct cgaOpEntry {
	const char*    op;        /* 演算子名(キー) */
	const ArgKind* in;        /* 入力型リスト(固定先頭 nin 個) */
	int            nin;       /* 固定入力数 */
	ArgKind        out;       /* 出力型 */
	ReaderFactory  mkReader;  /* cache 入力用(無ければ 0) */
	CalcFactory    mkCalc;    /* 計算本体生成子 */
	int            variadic;  /* 1=nin 個の固定引数の後ろに AK_CACHE(mesh)を可変個取れる。既定 0(末尾ゼロ詰め) */
};

static const ArgKind SHAPE3_IN[] = { AK_INLINE, AK_INLINE, AK_INLINE };  /* box/prism/pyramid */
static const ArgKind SHAPE2_IN[] = { AK_INLINE, AK_INLINE };             /* rect(w,h) 2D */
static const ArgKind SHAPE1_IN[] = { AK_INLINE };                        /* sphere(r) / boxa([..]) */
static const ArgKind EXPORT_IN[] = { AK_INLINE, AK_CACHE, AK_INLINE };  /* export(path, mesh, unit) */
static const ArgKind EXPORTVOX_IN[] = { AK_INLINE, AK_INLINE };  /* export_vox(path, params, mesh…可変) */
static const ArgKind BINMESH_IN[] = { AK_CACHE, AK_CACHE };  /* 2 mesh 入力(cache ハンドル→reader 読み) */
/* transform 系: 入力 mesh(cache)1 個 + スカラ/構造(inline)。mesh は reader、残りは value-parse。 */
static const ArgKind ROTATE_IN[]  = { AK_CACHE, AK_INLINE, AK_INLINE };             /* rotate(m,axis,deg) */
static const ArgKind MESH1ARG_IN[] = { AK_CACHE, AK_INLINE };  /* translate(m,vec) / mirror(m,axis) / transform(m,matrix) */
static const ArgKind MEASURE_IN[] = { AK_CACHE };  /* 計測(値返し): mesh 1 個入力 → 値(AK_INLINE)出力 */
static const ArgKind THIN_IN[] = { AK_CACHE, AK_INLINE, AK_INLINE, AK_INLINE };  /* thin_spots(m, t, rays, cone) */
static const cgaOpEntry OPS[] = {
	{ "box",          SHAPE3_IN, 3, AK_CACHE, 0,                                        &mkCalcT<cgaBox>          },
	{ "boxa",         SHAPE1_IN, 1, AK_CACHE, 0,                                        &mkCalcT<cgaBox>          },  /* 寸法を array(構造 inline)で */
	{ "import",       SHAPE1_IN, 1, AK_CACHE, 0,                                        &mkCalcT<cgaImport>       },  /* import(path): 外部ファイル読み */
	{ "prism",        SHAPE3_IN, 3, AK_CACHE, 0,                                        &mkCalcT<cgaPrism>        },
	{ "pyramid",      SHAPE3_IN, 3, AK_CACHE, 0,                                        &mkCalcT<cgaPyramid>      },
	{ "sphere",       SHAPE2_IN, 2, AK_CACHE, 0,                                        &mkCalcT<cgaSphere>       },  /* sphere(r, subdiv): subdiv=icosphere 細分化(既定0) */
	{ "union",        BINMESH_IN,2, AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaUnion>        },
	{ "combine",      BINMESH_IN,2, AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaCombine>      },  /* +++ 交差許容の単純合体(viewer 用) */
	{ "intersection", BINMESH_IN,2, AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaIntersection> },
	{ "difference",   BINMESH_IN,2, AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaDifference>   },
	{ "export",       EXPORT_IN, 3, AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaExport>       },
#ifdef SRAVA_HAVE_HDF5
	{ "export_vox",   EXPORTVOX_IN,2,AK_CACHE,&mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaVoxelize>, 1 },  /* voxel化→vox.h5。末尾メッシュ可変(variadic=1)。HDF5 必須 */
#endif
	{ "translate",    MESH1ARG_IN,2,AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaTranslate>   },
	{ "rotate",       ROTATE_IN, 3, AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaRotate>      },
	{ "mirror",       MESH1ARG_IN,2,AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaMirror>      },
	{ "scale",        MESH1ARG_IN,2,AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaScale>       },
	{ "transform",    MESH1ARG_IN,2,AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaTransform>   },
	{ "color",        MESH1ARG_IN,2,AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaColor>       },  /* 面色 f:color */
	{ "rect",         SHAPE2_IN, 2, AK_CACHE, 0,                                        &mkCalcT<cgaRect>        },  /* 2D 長方形 */
	{ "ngon",         SHAPE2_IN, 2, AK_CACHE, 0,                                        &mkCalcT<cgaNgon>        },  /* 2D 正 n 角形 */
	{ "circle",       SHAPE2_IN, 2, AK_CACHE, 0,                                        &mkCalcT<cgaCircle>      },  /* circle(r, segs): segs=多角形辺数(既定32) */
	{ "polygon",      SHAPE1_IN, 1, AK_CACHE, 0,                                        &mkCalcT<cgaPolygon>     },  /* 2D 明示点列 */
	{ "line",         SHAPE1_IN, 1, AK_CACHE, 0,                                        &mkCalcT<cgaLine>        },  /* 2D ガイド(寸法線・開ポリライン) */
	{ "extrude",      MESH1ARG_IN,2,AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaExtrude>     },  /* 2D→3D 角柱 */
	{ "tube",         SHAPE2_IN, 2, AK_CACHE, 0,                                        &mkCalcT<cgaTube>        },  /* tube(path, segs): 3D 折れ線まわりの掃引管 */
	{ "revolve",      ROTATE_IN, 3, AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaRevolve>     },  /* revolve(m,angle,segs): 2D→3D 回転体 */
	{ "offset",       ROTATE_IN, 3, AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaOffset>      },  /* offset(m,d,subdiv): 2D skeleton / 3D Minkowski */
	{ "area",         MEASURE_IN,1, AK_INLINE,&mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaArea>        },  /* area(m): 値返し(2D 面積 / 3D 表面積) */
	{ "valid",        MEASURE_IN,1, AK_INLINE,&mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaValid>       },  /* valid(m): 値返し(1=正常/0=問題) */
	{ "repair",       MEASURE_IN,1, AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaRepair>      },  /* repair(m): mesh 返し(autorefine / even-odd) */
	{ "section",      ROTATE_IN, 3, AK_CACHE, &mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaSection>     },  /* section(m,P,N): 平面で切った 2D 断面 */
	{ "volume",       MEASURE_IN,1, AK_INLINE,&mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaVolume>      },  /* volume(m): 値返し(3D 体積・2D エラー) */
	{ "perimeter",    MEASURE_IN,1, AK_INLINE,&mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaPerimeter>   },  /* perimeter(m): 値返し(2D 境界長・3D エラー) */
	{ "centroid",     MEASURE_IN,1, AK_INLINE,&mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaCentroid>    },  /* centroid(m): 配列返し([x,y]/[x,y,z]) */
	{ "bbox",         MEASURE_IN,1, AK_INLINE,&mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaBbox>        },  /* bbox(m): 入れ子配列返し([min隅,max隅]) */
	{ "distance",     BINMESH_IN,2, AK_INLINE,&mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaDistance>    },  /* distance(a,b): 値返し(3D 最近接距離・近似) */
	{ "closest",      BINMESH_IN,2, AK_INLINE,&mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaClosest>     },  /* closest(a,b): 配列返し([d,[pa],[pb]]) */
	{ "farthest",     BINMESH_IN,2, AK_INLINE,&mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaFarthest>    },  /* farthest(a,b): 配列返し(頂点総当り・厳密) */
	{ "thin_spots",   THIN_IN,   4, AK_INLINE,&mkReaderT<ptscgWireCacheStreamReaderMesh>, &mkCalcT<cgaThinSpots>   },  /* thin_spots(m,t,rays,cone): 肉厚<t の面の[[x,y,z,thk],..](SDF・cone=コーン全角°) */
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

static int lookup_op(const char* name) {
	for ( int i = 0 ; i < N_OPS ; ++i )
		if ( ::strcmp(OPS[i].op, name) == 0 ) return i;
	return -1;
}

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgatsAgent_(
		sPtr<tsApplication> parent);

	sRptr<tsApplication,tinyState>		parent;
protected:
	sPtr<ts2IO>		rio;
	sPtr<ts2IO>		wio;
	sPtr<ptsWirePipe>	pipe;
	sPtr<ptsCalcBody>	calc;
	sPtr<ptsWireCacheStreamWriter>	writer;   /* mesh=WriterMesh / 値=WriterText */
	sArray<sPtr<pigData> >	argv;     /* arg_index で収集した入力 */
	sArray<sPtr<ptsWireCacheStreamReader> >	readers;   /* cache 入力の reader(idx 対応) */
	sArray<sPtr<tinyState> >	vparsers;  /* inline 引数の value-parse 子(idx 対応) */
	sPtr<stdString>		cachePath;   /* C_ARG_END の目標パス */
	sPtr<pigData>		result;      /* 計算結果 */
	sPtr<stdString>		errMsg;
	int			opIdx;       /* 現 op(OPS index)。-1=未設定 */
	int			pending;     /* 未完了 reader 数 */
	int			gotEnd;      /* C_ARG_END 受信済み */
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
class tinyState;
class tsApplication;
class ts2IO;
class ptsWirePipe;
class ptsCalcBody;
class ptsWireCacheStreamWriter;
class ptsWireCacheStreamReader;
class pigData;
class stdString;
TS_END_INTERFACE

#endif


cgatsAgent_::cgatsAgent_(TS_ARGS0)
        : ptsApplication_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    opIdx   = -1;
    pending = 0;
    gotEnd  = 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsApplication_START)   /* ptsApplication 派生: ptsApp=自分 の後にここで配線 */
{
	sPtr<tinyState> self = ifThis;
	/* 自 stdin(fd0)=rio / stdout(fd1)=wio を portable に ts2IO 化(Linux=fd を ts2IOdescriptor で
	 * 包む / Windows=GetStdHandle+GetFileType でコンソール/パイプを判定)。生 fd 直指定を排し MinGW 対応。 */
	s2IOstd::init(self, &rio, &wio);           /* rio=stdin(読み) / wio=stdout(書き) */
	/* ★ 応答書き込みも分割書き込みに(planner 側 wfd と同じ理由)。値返し op の結果は A_SAVE_BEGIN に
	 * 本文相乗りで pipe へ返るので、巨大な値(大きな配列等)だと >64KB になり得る。ts2IO pipe の既定は
	 * 不可分書き込み(指定 length をきっちり書く)で、length>64KB は絶対成功せず・部分空きでは CPU100%
	 * ループになる。応答は連続バイト列で不可分性不要(レコード境界=上位の長さ前置)→ set_divisible 必須。 */
	wio->set_divisible();
	pipe = thNEW(ptsWirePipe,(self, rio, wio));
	opIdx = -1;
	return ACT_cgatsAgent_WAIT;   /* WAIT は pipe のイベント待ち → rDO なし(イベントキューを読む) */
}

TS_STATE(ACT_cgatsAgent_WAIT)
{
	if ( ev->type == TSE_PACKET ) {
		sPtr<ptsWirePacket> pkt = sPtr<ptsWirePacket>::d_cast(ev->msg_obj);
		int n = pkt->payload.length();
		switch ( pkt->type ) {
		case C_OP: {
			sPtr<stdString> opName = ( n > 0 )
			    ? sPtr<stdString>(thNEW(stdString,((const char*)&pkt->payload[0], 0, n)))
			    : sPtr<stdString>(thNEW(stdString,("")));
			opIdx = lookup_op(opName->get_str());
			if ( opIdx < 0 ) {
				errMsg = thNEW(stdString,("unknown op: "))->add(opName);
				return rDO|ACT_cgatsAgent_ERROR;
			}
			argv.length(0);
			readers.length(0);
			vparsers.length(0);
			pending = 0;
			gotEnd  = 0;
			cgts_timing("recv_op");   /* T0: リクエスト受信開始 */
			break;
		}
		case C_ARG_PATH:
		case C_ARG_INLINE: {
			if ( opIdx < 0 ) {
				errMsg = thNEW(stdString,("arg before C_OP"));
				return rDO|ACT_cgatsAgent_ERROR;
			}
			if ( n < 4 ) {
				errMsg = thNEW(stdString,("arg missing index"));
				return rDO|ACT_cgatsAgent_ERROR;
			}
			const uint8_t *p = &pkt->payload[0];
			uint32_t idx = (uint32_t)p[0] | ((uint32_t)p[1]<<8)
			             | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
			ArgKind kind = ( pkt->type == C_ARG_PATH ) ? AK_CACHE : AK_INLINE;
			const cgaOpEntry& e = OPS[opIdx];
			if ( (int)idx >= e.nin && ! e.variadic ) {
				char b[160];
				::snprintf(b, sizeof b, "%s: too many arguments (takes %d)", e.op, e.nin);
				errMsg = thNEW(stdString,(b));
				return rDO|ACT_cgatsAgent_ERROR;
			}
			if ( ( (int)idx < e.nin ? e.in[(int)idx] : AK_CACHE ) != kind ) {
				const char* want = ( ( (int)idx < e.nin ? e.in[(int)idx] : AK_CACHE ) == AK_CACHE ) ? "a mesh" : "a value (number/array)";
				const char* got  = ( kind == AK_CACHE )           ? "a mesh" : "a value";
				char b[192];
				::snprintf(b, sizeof b, "%s: argument %d should be %s, got %s",
				           e.op, (int)idx + 1, want, got);
				errMsg = thNEW(stdString,(b));
				return rDO|ACT_cgatsAgent_ERROR;
			}
			sPtr<stdString> text = thNEW(stdString,((const char*)(p+4), 0, n-4));
			if ( (int)idx >= argv.length() )
				argv.length((int)idx + 1);
			if ( (int)idx >= readers.length() )
				readers.length((int)idx + 1);
			if ( (int)idx >= vparsers.length() )
				vparsers.length((int)idx + 1);
			if ( kind == AK_INLINE ) {
				/* inline テキスト = プランナーが serialize() した値リテラル。VALUE モードで
				 * パースして構造値(int/float/string/array/hash)に復元する。reader と同様に
				 * 子の TSE_RETURN を待って argv[idx] を埋める(pending で C_ARG_END と同期)。 */
				vparsers[(int)idx] = thNEW(cgptsLemonParser,(ifThis, text, 1, thNULL));
				pending++;
			} else {
				/* cache 入力: text=入力キャッシュパス。reader を起動し OFF を読む(別プロセスが
				 * 書いたキャッシュをまたいで読む = CacheStream。完了時に argv[idx] を埋める)。 */
				readers[(int)idx] = e.mkReader(ifThis, text);
				pending++;
			}
			break;
		}
		case C_ARG_END: {
			const cgaOpEntry& e = OPS[opIdx];
			if ( ( e.variadic ? argv.length() < e.nin : argv.length() != e.nin ) ) {
				char b[160];
				::snprintf(b, sizeof b, "%s: expected %d argument(s), got %d",
				           e.op, e.nin, argv.length());
				errMsg = thNEW(stdString,(b));
				return rDO|ACT_cgatsAgent_ERROR;
			}
			cachePath = ( n > 0 )
			    ? sPtr<stdString>(thNEW(stdString,((const char*)&pkt->payload[0], 0, n)))
			    : sPtr<stdString>(thNEW(stdString,("/tmp/srava-agent.cache")));
			gotEnd = 1;
			if ( pending == 0 )            /* cache 入力なし/全 reader 完了済み → すぐ計算 */
				return rDO|ACT_cgatsAgent_STARTCALC;
			return 0;                      /* reader 完了待ち */
		}
		default:
			break;
		}
		return 0;
	}
	if ( ev->type == TSE_RETURN ) {
		if ( ev->source == pipe ) {
			/* pipe の TSE_RETURN = 相手の W_END。gotEnd 済みなら plan の「送信完了(wend)」なので
			 * 無視して処理続行(reader/計算へ)。未受領なら途中で閉じられた = 異常 → FIN。
			 * (box は C_ARG_END で即 WAIT を出るのでここに来ない。union は reader 待ちで滞留する) */
			if ( ! gotEnd )
				return rDO|FIN_START;
			return 0;
		}
		/* reader / value-parser 完了: どの idx か特定し結果(pigData。mesh は cgMesh、
		 * inline は構造値)を argv へ。全完了 + C_ARG_END で計算へ。 */
		for ( int i = 0 ; i < readers.length() ; ++i ) {
			if ( readers[i] != thNULL && readers[i] == ev->source ) {
				argv[i] = sPtr<pigData>::d_cast(ev->msg_obj);
				readers[i] = thNULL;
				pending--;
				/* reader が mesh を返せなかった(null)= 入力キャッシュの読込/decode 失敗。
				 * 非決定的に起きるなら **キャッシュ競合**(同一 recipe を複数 agent が同時計算)が
				 * 疑わしい。op 名・入力番号付きで明示エラー(下流の "missing operand" より早く・具体的)。 */
				if ( argv[i] == thNULL && opIdx >= 0 ) {
					char b[160];
					::snprintf(b, sizeof b,
					    "%s: input %d failed to read/decode its cache (possible cache race — try clearing the cache dir or rerun)",
					    OPS[opIdx].op, i + 1);
					errMsg = thNEW(stdString,(b));
					return rDO|ACT_cgatsAgent_ERROR;
				}
				break;
			}
		}
		for ( int i = 0 ; i < vparsers.length() ; ++i ) {
			if ( vparsers[i] != thNULL && vparsers[i] == ev->source ) {
				sPtr<pigData> v = sPtr<pigData>::d_cast(ev->msg_obj);
				vparsers[i] = thNULL;
				pending--;
				if ( v == thNULL || v->is_error() ) {   /* inline 値のパース失敗 */
					errMsg = thNEW(stdString,("inline arg parse error"));
					return rDO|ACT_cgatsAgent_ERROR;
				}
				argv[i] = v;
				break;
			}
		}
		if ( gotEnd && pending == 0 )
			return rDO|ACT_cgatsAgent_STARTCALC;
		return 0;
	}
	return 0;
}

TS_STATE(ACT_cgatsAgent_STARTCALC)   /* 全入力(値 + reader 結果)が揃った → 計算本体起動 */
{
	cgts_timing("parse_done");   /* T1: 受信 + value-parse(reader)完了 */
	/* テスト用: 計算を遅くして、プランナーの SIGINT が評価中に確実に届くようにする。 */
	if ( ::getenv("PIG_TEST_SLOW") != 0 )
		::usleep(200000);   /* 200ms */
	/* 入力はポインタ渡し(argv は本オブジェクトのメンバ=計算本体の寿命中ずっと生存)。 */
	calc = OPS[opIdx].mkCalc(ifThis, &argv, cachePath);
	return ACT_cgatsAgent_CALC;   /* calc の TSE_RETURN 待ち → rDO なし */
}

TS_STATE(ACT_cgatsAgent_CALC)
{
	if ( ev->type == TSE_RETURN && ev->source == calc ) {
		cgts_timing("compute_done");   /* T2: 計算本体(cgaTube 等の geometry)完了 */
		/* 計算本体がエラーを立てた(import 失敗等)→ A_ERROR でプランナーへ伝播。
		 * mesh 系 op は通常 get_result()=thNULL(writer 経路)なので影響なし。 */
		sPtr<pigData> cr = calc->get_result();
		if ( cr != thNULL && cr->is_error() ) {
			/* errMsg は raw メッセージ(他の agent エラーと同様)。pigDataError::get_str は
			 * "ERROR: " 前置するので、プランナー側の再包装と二重化しないよう message() を使う。 */
			sPtr<pigDataError> e = sPtr<pigDataError>::d_cast(cr);
			errMsg = e.is_notNull() ? e->message() : cr->get_str();
			return rDO|ACT_cgatsAgent_ERROR;
		}
		/* 出力種別で writer を分岐:
		 *  - cache(mesh): calc が parent=cgatsAgent で生成した WriterMesh を受け取り、バイナリ
		 *    D_CHUNK でストリーム書き込み(blob は calc=本オブジェクトの calc メンバが所有・生存)。
		 *  - 値(インライン): 結果テキスト(get_result)を WriterText で保存(本文を A_SAVE_BEGIN 相乗り)。 */
		if ( OPS[opIdx].out == AK_CACHE ) {
			writer = calc->get_writer();
		} else {
			result = calc->get_result();
			/* 値(インライン)は **serialize()** で保存(VALUE 往復の正準形)。get_str() は表示形で
			 * float の小数点が落ち(4.0→"4")、プランナの VALUE 再パースで整数化してしまう(centroid/bbox の
			 * 整数除算バグ)。serialize は float に必ず ".0"・文字列に引用符を付け型を保つ。 */
			writer = thNEW(ptsWireCacheStreamWriterText,(ifThis, cachePath, result->serialize()));
		}
		return ACT_cgatsAgent_WRITING;   /* writer の TSE_ASSERT/TSE_RETURN 待ち → rDO なし */
	}
	return 0;
}

TS_STATE(ACT_cgatsAgent_WRITING)
{
	/* 出力種別で A_SAVE_BEGIN を出すタイミングが変わる(真の read-while-write 同時ストリーミング):
	 *  - cache(mesh): writer の **TSE_ASSERT**(streamhdr+D_META 書込完了)で SAVEBEGIN へ。本体
	 *    (D_TEXT/D_CHUNK)は writer がこの後ストリーム書き込み → 下流 reader が書込中に attach し読める。
	 *  - 値(インライン): 本文を A_SAVE_BEGIN に相乗りするので全書込完了(TSE_RETURN)を待ってから。 */
	if ( ev->source == writer ) {
		if ( OPS[opIdx].out == AK_CACHE ) {
			if ( ev->type == TSE_ASSERT )
				return rDO|ACT_cgatsAgent_SAVEBEGIN;   /* header+meta ready, body 後続 */
		} else {
			if ( ev->type == TSE_RETURN )
				return rDO|ACT_cgatsAgent_SAVEBEGIN;   /* 全書込完了(本文相乗り用) */
		}
	}
	return 0;
}

/* --- 保存応答(ev 非依存・1 状態 1 write_record。GOTCHAS §9) --- */
TS_STATE(ACT_cgatsAgent_SAVEBEGIN)
{
	/* 出力種別で分岐: cache(mesh) は本文を相乗りせず **空** A_SAVE_BEGIN(本体はキャッシュにある →
	 * planner は pigDataCache ハンドルにする)。値(インライン)は本文を相乗り。 */
	if ( opIdx >= 0 && OPS[opIdx].out == AK_CACHE ) {
		pipe->write(A_SAVE_BEGIN, 0, 0);
		/* cache: A_SAVE_BEGIN は body 書込前に出した。writer の本体ストリーム完了
		 * (W_END まで書いた TSE_RETURN)を待ってから A_SAVE_DONE を出す。 */
		return ACT_cgatsAgent_SAVEWRITEWAIT;   /* TSE_RETURN 待ち → rDO なし */
	}
	/* sPtr を変数で保持: serialize() の戻り(temporary stdString)が write 前に解放され
	 * body がダングリングするのを防ぐ(値返し op の本文。実 op 登場まで露呈しなかった潜在バグ)。
	 * serialize()(get_str ではなく)で VALUE 往復の正準形にし型を保つ(float の小数点を落とさない)。 */
	sPtr<stdString> bodyStr = result->serialize();
	const char *body = bodyStr->get_str();
	pipe->write(A_SAVE_BEGIN, (const uint8_t*)body, (int)::strlen(body));
	return rDO|ACT_cgatsAgent_SAVEDONE;
}
TS_STATE(ACT_cgatsAgent_SAVEWRITEWAIT)   /* cache: writer 本体書込完了(W_END 済)を待つ */
{
	if ( ev->type == TSE_RETURN && ev->source == writer ) {
		cgts_timing("write_done");   /* T3: cache 書き込み(mesh エンコード/ストリーム)完了 */
		/* テスト用フォールトインジェクション: 継続(promise)解決済み = A_SAVE_BEGIN 送信後に
		 * agent がエラーを出すケース。プランナー側の ptsApp->set_agentError 経路を検証する。
		 * 根(union)のみに限定(box まで巻き込むと cascade abort で検証点が曖昧になる)。 */
		if ( ::getenv("PIG_TEST_ERR_AFTER_SAVE") != 0
		     && opIdx >= 0 && ::strcmp(OPS[opIdx].op, "union") == 0 ) {
			errMsg = thNEW(stdString,("injected error after save (test)"));
			return rDO|ACT_cgatsAgent_ERROR;
		}
		return rDO|ACT_cgatsAgent_SAVEDONE;
	}
	return 0;
}
TS_STATE(ACT_cgatsAgent_SAVEDONE)
{
	pipe->write(A_SAVE_DONE, 0, 0);
	return rDO|ACT_cgatsAgent_SAVEBYE;
}
TS_STATE(ACT_cgatsAgent_SAVEBYE)
{
	pipe->write(A_BYE, 0, 0);
	return rDO|ACT_cgatsAgent_SAVEWEND;
}
TS_STATE(ACT_cgatsAgent_SAVEWEND)
{
	pipe->wend();
	return ACT_cgatsAgent_DONE;   /* pipe(plan)閉じの TSE_RETURN 待ち → rDO なし */
}

TS_STATE(ACT_cgatsAgent_DONE)
{
	if ( ev->type == TSE_RETURN && ev->source == pipe )
		return rDO|FIN_START;
	return 0;
}

/* --- エラー応答(ev 非依存・1 状態 1 write) --- */
TS_STATE(ACT_cgatsAgent_ERROR)
{
	const char *m = ( errMsg != thNULL ) ? errMsg->get_str() : "agent error";
	pipe->write(A_ERROR, (const uint8_t*)m, (int)::strlen(m));
	return rDO|ACT_cgatsAgent_ERRWEND;
}
TS_STATE(ACT_cgatsAgent_ERRWEND)
{
	pipe->wend();
	return ACT_cgatsAgent_DONE;   /* pipe 閉じの TSE_RETURN 待ち → rDO なし */
}

TS_STATE(FIN_START)
{
	return rDO|FIN_ptsApplication_START;
}
