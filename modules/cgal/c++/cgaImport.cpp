/*
 * cgaImport — 外部メッシュファイルを読み込む計算本体(ptsCalcBody 派生)。args=[path](INLINE)。
 * 形式は **拡張子で自動判別**(CGAL::Polygon_mesh_processing::IO::read_polygon_mesh): OFF/STL/OBJ/PLY。
 * STL は三角形スープ(頂点重複・連結情報なし)なので PMP の read が de-dup/orient/repair を内部で行い
 * 多様体メッシュに変換する。結果は普通のメッシュキャッシュ(box 等と同じ repr_type=1)で、以降は
 * DAG の葉として union 等に使える。
 * キャッシュキーの content-addressing はプランナー側(pigDataFileRef::get_hashkey がファイル内容を
 * ハッシュ)で行うので、ここはパスを受けて読むだけ。読めなければ空メッシュ(nv=0)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"   /* 基底 ptsObject の sPtr<ptsApplication> メンバ用 */
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"common/affine.h"   /* ★ #3533: DXF の任意軸は書き手と共有する (dxf_ocs_axes) */
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaImport_.h"

#include	<string>
#include	<vector>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<math.h>   /* ★ #3551: bulge (円弧の折れ線) の判定 */

CLASS_TINYSTATE(cg/c++/cgaImport,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaImport_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;     /* compute() が読んだ 3D/2D mesh(get_result が agent へ返す) */
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
class ptsObject;
class pigData;
class stdString;
class cgMesh;
TS_END_INTERFACE

#endif


cgaImport_::cgaImport_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* path のファイルを読み、cgMesh に decode。拡張子で OFF/STL/OBJ/PLY 判別、STL は repair される。
 * 読めなければ(不在/未対応形式/パース失敗/空)result に pigDataError を立てる → cgatsAgent が
 * 検出して A_ERROR をプランナーへ返す(従来はサイレント空メッシュ nv=0 だった)。 */
/* SVG の path data(M/L/Z・絶対座標のみ。曲線 C/Q/A は非対応)を 2D 多角形に。我々の出力を round-trip
 * できる最小パーサ。1 つの <path> = 1 領域、その中の M..Z サブパスが 外周(先頭)+ 穴。
 *
 * ★★★ #3551 (ひさ判断 2026-09-17): 読めないコマンドは **黙って飛ばさない**。
 *   「折れ線の世界の cgal が、そうじゃない入力にこだわるのは変。円が cgal で欲しいならば、
 *     occt を通して cgal へ持っていくのが正解」
 *   ⇒ 曲線を折れ線へ落として *読んであげる* のではなく、**断って次の一手を言う**。
 *   ⚠ 従来は @else { c++; }@ で 1 文字ずつ捨てていたので、@A@ (円弧) や @C@ (3 次ベジエ) が
 *     **在ったことすら報告されず**に消えていた。実測: 正方形 (M/L) と円 (A) を並べた .svg で
 *     area=100 ・ bbox=[[0,0],[10,10]] が返る = 円が完全に無かったことになる。
 *   ★ 刻み方 (どれくらい細かい折れ線にするか) の約束は @polygonize(2d, defl)@ が既に持って
 *     いるので、ここで 2 つ目を決めない (二重に決めない)。 */
static bool parse_svg(const char *path, sPtr<cgMesh2D> out, char *badCmd)
{
	if ( badCmd != 0 ) *badCmd = '\0';
	FILE *f = ::fopen(path, "rb");
	if ( f == 0 ) return false;
	std::string s;
	char buf[65536]; size_t n;
	while ( (n = ::fread(buf, 1, sizeof buf, f)) > 0 ) s.append(buf, n);
	::fclose(f);

	bool any = false;
	size_t pos = 0;
	while ( (pos = s.find("d=\"", pos)) != std::string::npos ) {
		pos += 3;
		size_t end = s.find('"', pos);
		if ( end == std::string::npos ) break;
		std::string d = s.substr(pos, end - pos);
		pos = end + 1;

		/* ★ #3545: リングは **素の (x,y) 列**で組む。外周/穴への組み立てと向きの正規化は
		 *   幾何 lib 側 (cgMesh2D::add_regions_from_rings) — CGAL の述語を op に置かない。 */
		std::vector<double> xy;      /* 全リングの点を連結 */
		std::vector<int>    ringLen; /* リングごとの点数 */
		int curN = 0;
		const char *c = d.c_str();
		while ( *c ) {
			if ( *c == 'M' || *c == 'L' ) {
				char cmd = *c++;
				char *p2;
				double x = ::strtod(c, &p2); c = p2;
				while ( *c == ',' || *c == ' ' || *c == '\t' || *c == '\n' ) c++;
				double y = ::strtod(c, &p2); c = p2;
				if ( cmd == 'M' && curN >= 3 ) { ringLen.push_back(curN); curN = 0; }
				else if ( cmd == 'M' ) { xy.resize(xy.size() - (size_t)curN * 2); curN = 0; }
				xy.push_back(x); xy.push_back(y); curN++;
			} else if ( *c == 'Z' || *c == 'z' ) {
				c++;
				if ( curN >= 3 ) { ringLen.push_back(curN); curN = 0; }
			} else {
				/* ★ #3551: **読めないコマンド**を覚えておく (最初の 1 つ)。
				 *   ⚠ 数字・空白・符号もここに来るので、*コマンド文字だけ* を見る。
				 *   ⚠ 小文字の m / l は **相対座標** — これも読めない (絶対しか扱わない)。 */
				if ( badCmd != 0 && *badCmd == '\0' &&
				     ::strchr("AaCcSsQqTtHhVvml", *c) != 0 )
					*badCmd = *c;
				c++;
			}
		}
		if ( curN >= 3 ) ringLen.push_back(curN);
		else             xy.resize(xy.size() - (size_t)curN * 2);
		if ( ringLen.empty() ) continue;

		/* nest=0: 1 つの <path> は「先頭が外周・残りは穴」 */
		out->add_regions_from_rings(&xy[0], &ringLen[0], (int)ringLen.size(), 0);
		any = true;
	}

	/* ★★★ #3551: **<polyline> をガイド層へ読む**。
	 *   ⚠ こちらの書き手はガイド (寸法線などの開ポリライン) を @<polyline points="...">@ で
	 *     出しているのに、読み手は @d="..."@ しか見ていなかった ⇒ 自分で書いたものを
	 *     読み戻すと **ガイドが黙って消えていた**。実測:
	 *       combine(rect(4,4), line([[0,0],[3,3],[1,4]]))  nverts = 7
	 *       .svg へ書いて読み戻す                          nverts = **4** (ガイドが消える)
	 *   ★ DXF 側の「開いた LWPOLYLINE はガイド」と対になる扱い。 */
	pos = 0;
	while ( (pos = s.find("<polyline", pos)) != std::string::npos ) {
		const std::size_t tagEnd = s.find('>', pos);
		if ( tagEnd == std::string::npos ) break;
		const std::size_t pk = s.find("points=\"", pos);
		if ( pk == std::string::npos || pk > tagEnd ) { pos = tagEnd + 1; continue; }
		const std::size_t pb = pk + 8;
		const std::size_t pe = s.find('"', pb);
		if ( pe == std::string::npos ) break;
		const std::string pts = s.substr(pb, pe - pb);
		std::vector<double> g;
		const char *c = pts.c_str();
		while ( *c ) {
			while ( *c == ' ' || *c == ',' || *c == '\t' || *c == '\n' || *c == '\r' ) ++c;
			if ( *c == '\0' ) break;
			char *p2;
			const double x = ::strtod(c, &p2);
			if ( p2 == c ) { ++c; continue; }
			c = p2;
			while ( *c == ' ' || *c == ',' || *c == '\t' || *c == '\n' || *c == '\r' ) ++c;
			const double y = ::strtod(c, &p2);
			if ( p2 == c ) break;
			c = p2;
			g.push_back(x); g.push_back(y);
		}
		if ( g.size() >= 4 ) { out->add_guide(&g[0], (int)(g.size() / 2)); any = true; }
		pos = pe + 1;
	}
	return any;
}

static const char* import_ext(const char* path) {
	const char* dot = ::strrchr(path, '.');
	return dot ? dot + 1 : "";
}

/* DXF(ASCII)の LWPOLYLINE を 2D 多角形に。group code/value のペア列を読み、code 10/20 で頂点 x/y。
 * 我々の DXF 出力 + 単純な DXF を round-trip。閉ポリライン群を包含関係で外周/穴に nest(偶数=外周/奇数=穴)。
 * 曲線/円弧(bulge)・他エンティティは無視。
 *
 * ★★ #3533: **OCS (置き場所) も読む**。10/20 は *その平面の座標* で、平面は押し出し方向
 *   210/220/230 (法線) と elevation 38 で表される。⚠ ここを読まないと、書き手が OCS を出して
 *   いても **読み戻しで枠が落ちて z=0 に戻る** (2026-09-15 に往復で実測して見つけた)。
 *   ⇒ しかも @import_exts@ は @dxf:cg-face3d@ と申告しているので、*型スタンプは face3d
 *     なのに値は cross2d* という食い違いになっていた (#3533 で避けたはずの形)。
 *   ★ 軸は @srava_affine::dxf_ocs_axes@ = **書き手と同じ実装**を使う (片方だけ直すと
 *     書いたものが読めなくなる)。
 *   ⚠ 1 つの 2D 領域は枠を 1 つしか持てないので、**最初に現れた OCS** を採る。 */
/* ★ #3551: 組み立て中のポリラインを **閉じ旗で振り分けて**確定する。
 *   閉じている (かつ 3 点以上) ならリング / 開いているなら **ガイド** (2 点以上)。
 *   ⚠ どちらにもならない端切れは捨てる (従来と同じ)。 */
static void dxf_flush_poly(std::vector<double> &xy, std::vector<int> &ringLen,
                           std::vector<double> &gxy, std::vector<int> &gLen,
                           int curN, int curClosed)
{
	if ( curN <= 0 ) return;
	const std::size_t take = (std::size_t)curN * 2;
	if ( curClosed && curN >= 3 ) { ringLen.push_back(curN); return; }
	if ( ! curClosed && curN >= 2 ) {
		gxy.insert(gxy.end(), xy.end() - (long)take, xy.end());
		gLen.push_back(curN);
	}
	xy.resize(xy.size() - take);
}

static bool parse_dxf(const char *path, sPtr<cgMesh2D> out, std::string *badEnt)
{
	if ( badEnt != 0 ) badEnt->clear();
	FILE *f = ::fopen(path, "rb");
	if ( f == 0 ) return false;
	std::string s;
	char rbuf[65536]; size_t rn;
	while ( (rn = ::fread(rbuf, 1, sizeof rbuf, f)) > 0 ) s.append(rbuf, rn);
	::fclose(f);

	/* 行をトリムして取り出すヘルパ(行頭末の空白を除く)。 */
	std::vector<std::string> lines;
	{
		std::size_t i = 0;
		while ( i < s.size() ) {
			std::size_t j = s.find('\n', i);
			if ( j == std::string::npos ) j = s.size();
			std::string ln = s.substr(i, j - i);
			while ( ! ln.empty() && (ln[ln.size()-1]=='\r' || ln[ln.size()-1]==' ' || ln[ln.size()-1]=='\t') )
				ln.erase(ln.size()-1);
			std::size_t b = 0; while ( b < ln.size() && (ln[b]==' '||ln[b]=='\t') ) b++;
			lines.push_back(ln.substr(b));
			i = j + 1;
		}
	}

	/* ★ #3545: リングは **素の (x,y) 列**で組む。包含関係での nest と向きの正規化は
	 *   幾何 lib 側 (cgMesh2D::add_regions_from_rings) — CGAL の述語を op に置かない。 */
	std::vector<double> xy;       /* 全リングの点を連結 */
	std::vector<int>    ringLen;  /* リングごとの点数 */
	/* ★★★ #3551 (2026-09-17): **開いた LWPOLYLINE はガイド層へ**。
	 *   ⚠ 従来は 70 (閉じ旗) を見ずに *全部リング* にしていたので、こちらが書いた
	 *     ガイド (寸法線などの開ポリライン・レイヤ GUIDES ・ 70=0) を読み戻すと
	 *     **閉じた領域に化けて面積が増えた**。実測:
	 *       combine(rect(4,4), line([[0,0],[3,3],[1,4]]))  area = 16
	 *       それを .dxf へ書いて読み戻す                    area = **20.5**
	 *     ⇒ 落ちるのではなく *違う答えになる* ので、こちらのほうが悪い。
	 *   ★ cgMesh2D は元からガイド層を持っている (line() の行き先) ので、器はもう在る。 */
	std::vector<double> gxy;      /* ガイドの点を連結 */
	std::vector<int>    gLen;
	bool inPoly = false;
	int  curClosed = 0;
	int curN = 0;
	int pendingCode = -99999;
	double px = 0; bool haveX = false;
	/* ★ #3533: OCS。既定は world (+Z・elevation 0)。 */
	double ocsN[3] = { 0, 0, 1 }, ocsElev = 0;
	int haveOcs = 0;
	for ( std::size_t li = 0 ; li + 1 < lines.size() ; li += 2 ) {
		int code = ::atoi(lines[li].c_str());
		const std::string& val = lines[li+1];
		if ( code == 0 ) {                       /* エンティティ境界 */
			dxf_flush_poly(xy, ringLen, gxy, gLen, curN, curClosed);
			curN = 0; curClosed = 0; haveX = false;
			inPoly = ( val == "LWPOLYLINE" || val == "POLYLINE" );
			/* ★★★ #3551: **読めない幾何実体**を覚えておく (最初の 1 つ)。
			 *   ⚠ TEXT / DIMENSION / INSERT など *幾何でない* 実体は従来どおり無視する
			 *     — そこまで断ると第三者の .dxf がほとんど読めなくなる。
			 *   ★ 断るのは「図形の一部なのに落ちるもの」だけ。occt はこれらを
			 *     **曲線のまま**読めるので、次の一手として案内できる (#3544 段 3)。 */
			if ( badEnt != 0 && badEnt->empty() &&
			     ( val == "LINE" || val == "CIRCLE" || val == "ARC" ||
			       val == "ELLIPSE" || val == "SPLINE" ) )
				*badEnt = val;
		} else if ( inPoly && code == 70 ) {     /* ★ #3551: 閉じ旗 (bit 0) */
			curClosed = ( ::atoi(val.c_str()) & 1 ) ? 1 : 0;
		} else if ( inPoly && code == 42 ) {     /* ★ #3551: bulge = **円弧の折れ線** */
			/* ⚠ bulge が非零の区間は弦ではなく円弧。無視すると *形が変わったまま成功する*
			 *   (「曲線/円弧(bulge)は無視」と元のコメントに書いてあった振る舞い)。 */
			if ( badEnt != 0 && badEnt->empty() && ::fabs(::strtod(val.c_str(), 0)) > 1e-12 )
				*badEnt = "LWPOLYLINE with a bulge (arc segment)";
		} else if ( inPoly && code == 10 ) {     /* 頂点 x */
			px = ::strtod(val.c_str(), 0); haveX = true;
		} else if ( inPoly && code == 20 ) {     /* 頂点 y(直前の x と対) */
			double py = ::strtod(val.c_str(), 0);
			if ( haveX ) { xy.push_back(px); xy.push_back(py); curN++; haveX = false; }
		} else if ( inPoly && code == 38 && ! haveOcs ) {    /* ★ elevation */
			ocsElev = ::strtod(val.c_str(), 0);
		} else if ( inPoly && code == 210 && ! haveOcs ) {   /* ★ 押し出し方向 = 平面の法線 */
			ocsN[0] = ::strtod(val.c_str(), 0);
		} else if ( inPoly && code == 220 && ! haveOcs ) {
			ocsN[1] = ::strtod(val.c_str(), 0);
		} else if ( inPoly && code == 230 && ! haveOcs ) {
			ocsN[2] = ::strtod(val.c_str(), 0);
			haveOcs = 1;                                     /* 230 まで揃ったら以後は固定 */
		}
		(void)pendingCode;
	}
	dxf_flush_poly(xy, ringLen, gxy, gLen, curN, curClosed);
	if ( ringLen.empty() && gLen.empty() ) return false;

	/* nest=1: 包含の深さで組む (偶数=外周・奇数=直近外周の穴)。 */
	if ( ! ringLen.empty() &&
	     out->add_regions_from_rings(&xy[0], &ringLen[0], (int)ringLen.size(), 1) == 0 )
		return false;
	/* ★ #3551: 開いたものはガイド層へ (面積に効かない・SVG/DXF で線として描かれる)。 */
	{
		std::size_t at = 0;
		for ( std::size_t i = 0 ; i < gLen.size() ; ++i ) {
			out->add_guide(&gxy[at * 2], gLen[i]);
			at += (std::size_t)gLen[i];
		}
	}
	/* ★ #3533: 読んだ OCS を枠にする。⚠ 10/20 は OCS の座標なので **局所座標はそのまま**
	 *   (書き手が world → OCS へ射影して書いている)。⇒ 往復でビットが動かない。 */
	{
		double n[3], ax[3], ay[3];
		if ( srava_affine::dxf_ocs_axes(ocsN, n, ax, ay) ) {
			const double o[3] = { n[0]*ocsElev, n[1]*ocsElev, n[2]*ocsElev };
			out->set_frame(o, ax, ay);
		}
	}
	/* ★ #3533: DXF は @import_exts@ で **常に cg-face3d** と申告している (拡張子 → 型は静的な表で、
	 *   同じ .dxf が OCS を持つか持たないかで型を変えられない・ひさ判断)。⇒ 平らな DXF でも
	 *   face3d にする。そうしないと *型スタンプは face3d なのに値は cross2d* になる。 */
	out->set_placed(1);
	return true;
}

void
cgaImport_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<stdString> path = ( na > 0 ) ? (*args)[0]->get_str()
	                                  : sPtr<stdString>(thNEW(stdString,("")));
	const char *p = path->get_str();
	const char *e = import_ext(p);

	if ( ::strcasecmp(e, "svg") == 0 ) {                 /* 2D: SVG → cgMesh2D */
		sPtr<cgMesh2D> m2 = thNEW(cgMesh2D,());
		char bad = '\0';
		const bool okSvg = parse_svg(p, m2, &bad);
		if ( bad != '\0' ) {
			/* ★ #3551: 読めたものが在っても **断る** — 半分だけ読めた図形を返すのが一番悪い。 */
			char m[420];
			::snprintf(m, sizeof m,
			    "import: this SVG uses the path command '%c', which cgal cannot read "
			    "(cgal's 2D is polylines, so it holds no arcs or beziers, and it does not "
			    "silently turn them into chords); read it with the occt module, which keeps "
			    "the curves, then polygonize(..., deflection) to choose how finely to "
			    "flatten them: %s", bad, p);
			result = cga_err(thNEW(stdString,(m)));
			mesh = m2;
			return;
		}
		if ( ! okSvg ) {
			sPtr<stdString> msg = thNEW(stdString,("import: failed to read SVG "));
			result = cga_err(msg->add(path));
		}
		mesh = m2;
		return;
	}
	if ( ::strcasecmp(e, "dxf") == 0 ) {                 /* 2D: DXF(LWPOLYLINE)→ cgMesh2D */
		sPtr<cgMesh2D> m2 = thNEW(cgMesh2D,());
		std::string bad;
		const bool okDxf = parse_dxf(p, m2, &bad);
		if ( ! bad.empty() ) {
			char m[460];
			::snprintf(m, sizeof m,
			    "import: this DXF contains %s, which cgal cannot read "
			    "(cgal's 2D is polylines, so it holds no lines, arcs or splines, and it does "
			    "not silently turn them into chords); read it with the occt module, which "
			    "keeps the curves, then polygonize(..., deflection) to choose how finely to "
			    "flatten them: %s", bad.c_str(), p);
			result = cga_err(thNEW(stdString,(m)));
			mesh = m2;
			return;
		}
		if ( ! okDxf ) {
			sPtr<stdString> msg = thNEW(stdString,("import: failed to read DXF "));
			result = cga_err(msg->add(path));
		}
		mesh = m2;
		return;
	}

	/* 3D: OFF/STL/OBJ/PLY → cgMesh3D(従来) */
	sPtr<cgMesh3D> m3 = thNEW(cgMesh3D,());
	/* ★ #3535②: 読むのは libsrava_cg 側 (cgMesh3D::read_file)。⚠ ここで
	 *   read_polygon_mesh を呼ぶと **IO モードの大域変数が cgal.so にもできる**
	 *   (理由は cgMesh.h の read_file の宣言のところ)。 */
	if ( ! m3->read_file(p) ) {
		sPtr<stdString> msg = thNEW(stdString,("import: failed to read "));
		result = cga_err(msg->add(path));
	}
	mesh = m3;
}

/* mesh 出力: box 等と同じく WriterMesh で cgMesh を D_CHUNK にストリーム。 */
/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaImport_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
