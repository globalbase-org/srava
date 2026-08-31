/*
 * pipe_proximity_adapter — pipe_proximity(namespace pipe)を plain 型へ橋渡しする TU。
 * SDK/pigData も POSIX ::pipe も触らない(namespace pipe との衝突を避けるため分離・adapter.h 参照)。
 */

/* NB(上流報告事項): pipe_proximity v0.1.1 の bvh.hpp が std::max({...}) を <algorithm> 無しで
 * 使う。新しい libstdc++ は透過 include しないので、本体を patch せず先に <algorithm> を出す。 */
#include <algorithm>

#include "pipe/bezier.hpp"
#include "pipe/radius.hpp"
#include "pipe/proximity.hpp"
#include "pipe/controller.hpp"
#include "pipe/scene.hpp"

#include "pipe_proximity_adapter.h"

#include <cmath>
#include <optional>
#include <utility>
#include <array>
#include <cstdio>
#include <cstdlib>

/* 半径ラムダ生成(両 op 共用)。radial_sr が空 → 指数 r0*exp(m*s)。
 * 非空 → [s,r] キーポイント([s0,r0,s1,r1,...])を s 昇順に整列して線形補間(端の外側は端値クランプ)。
 * s<0 は常に nullopt(管端)。gateUpper=true のとき s>Smax も nullopt(検出時=チェーン静的)。
 *   調整(adjust)では弧長が変わるので gateUpper=false(上限で切らない)。 */
static pipe::RadiusFn
makeRadiusFn(double r0, double m, std::vector<double> sr, bool gateUpper, double Smax,
             double clampS0 = -1, double clampS1 = -1, double clampR = 0)
{
	if ( sr.empty() ) {
		/* 指数 r0*exp(m*s)。clampS0>=0 のとき弧長クランプ:
		 *   s<=clampS0=指数 / clampS0..clampS1=指数値から clampR へ線形 / s>=clampS1=clampR 固定。
		 *   [r,m,p1,r1] は clampS0=clampS1=p1(p1 で r1 へ不連続)、[r,m,p1,p2,r2] は p1..p2 線形。 */
		return [=](double s) -> std::optional<double> {
			if ( s < 0 || ( gateUpper && s > Smax ) ) return std::nullopt;
			if ( clampS0 < 0 || s <= clampS0 ) return r0 * std::exp(m * s);
			if ( s >= clampS1 )                return clampR;
			double rp1 = r0 * std::exp(m * clampS0);
			double t = ( clampS1 > clampS0 ) ? (s - clampS0) / (clampS1 - clampS0) : 1.0;
			return rp1 + (clampR - rp1) * t;
		};
	}
	int n = (int)sr.size() / 2;
	for ( int i = 0 ; i < n ; ++i )           /* s 昇順に整列(点数は小さい想定・単純選択ソート) */
		for ( int j = i + 1 ; j < n ; ++j )
			if ( sr[2*j] < sr[2*i] ) {
				std::swap(sr[2*i],   sr[2*j]);
				std::swap(sr[2*i+1], sr[2*j+1]);
			}
	return [=](double s) -> std::optional<double> {
		if ( s < 0 || ( gateUpper && s > Smax ) ) return std::nullopt;
		if ( s <= sr[0] )           return sr[1];                   /* 左端クランプ */
		if ( s >= sr[2*(n-1)] )     return sr[2*(n-1)+1];           /* 右端クランプ */
		for ( int i = 0 ; i < n - 1 ; ++i ) {
			double sa = sr[2*i], sb = sr[2*i+2];
			if ( s >= sa && s <= sb ) {
				double ra = sr[2*i+1], rb = sr[2*i+3];
				double t = ( sb > sa ) ? (s - sa) / (sb - sa) : 0.0;
				return ra + (rb - ra) * t;
			}
		}
		return sr[2*(n-1)+1];
	};
}

/* ctrl_xyz(平坦) → ChainDesign(S, E, C)。 */
static pipe::ChainDesign
makeDesign(const std::vector<double>& xyz, int npts)
{
	pipe::ChainDesign d;
	auto pt = [&](int i){ return pipe::Vec3{ xyz[3*i+0], xyz[3*i+1], xyz[3*i+2] }; };
	d.S = pt(0);
	d.E = pt(npts - 1);
	for ( int i = 1 ; i < npts - 1 ; ++i ) d.C.push_back(pt(i));
	return d;
}

/* pipe::Contact → PPContact(body 番号込み)。 */
static PPContact
toPP(const pipe::Contact& c)
{
	PPContact o;
	o.gap = c.gap;
	o.pA[0]=c.pA.x; o.pA[1]=c.pA.y; o.pA[2]=c.pA.z;
	o.pB[0]=c.pB.x; o.pB[1]=c.pB.y; o.pB[2]=c.pB.z;
	o.normal[0]=c.normal.x; o.normal[1]=c.normal.y; o.normal[2]=c.normal.z;
	o.sA=c.sA; o.sB=c.sB; o.rA=c.rA; o.rB=c.rB;
	o.bodyA=c.bodyA; o.bodyB=c.bodyB;
	return o;
}

/* CtrlResult → PPAdjustResult(調整後 design を平坦 ctrl_xyz: S, C..., E の順)。 */
static PPAdjustResult
toAdjustResult(const pipe::CtrlResult& res)
{
	PPAdjustResult out;
	auto pushv = [&](const pipe::Vec3& v){
		out.ctrl_xyz.push_back(v.x); out.ctrl_xyz.push_back(v.y); out.ctrl_xyz.push_back(v.z);
	};
	pushv(res.design.S);
	for ( size_t i = 0 ; i < res.design.C.size() ; ++i ) pushv(res.design.C[i]);
	pushv(res.design.E);
	out.npts              = 2 + (int)res.design.C.size();
	out.iters             = res.iters;
	out.energy            = res.energy;
	out.maxClearViolation = res.maxClearViolation;
	out.feasible          = res.constraintsFeasible ? 1 : 0;
	return out;
}

/* PPAdjustParams → pipe::CtrlParams(numC = 可動チェーンの制御点数 = npts-2)。 */
static pipe::CtrlParams
makeCtrlParams(const PPAdjustParams& p, int numC)
{
	pipe::CtrlParams cp;
	cp.dMin    = p.dMin;
	cp.maxIter = ( p.maxIter > 0 ) ? p.maxIter : 200;
	cp.wLen    = p.wLen;       /* 張力(弧長)重み。テール張力 ∝ wLen */
	cp.wBend   = p.wBend;
	cp.wPenalty= p.wPenalty;   /* 接触ペナルティ重み。dMin 復元力 ∝ wPenalty */
	cp.wSpace  = p.wSpace;     /* 制御点間隔の均一化重み。密集/崩壊を防ぐ */
	cp.stepMax = ( p.stepMax > 0.0 ) ? p.stepMax : 0.05;
	cp.alOuter = ( p.alOuter > 0 ) ? p.alOuter : 4;   /* 拡張ラグランジュ外ループ: クリアランスを厳密化 */
	cp.det.reportGap = p.dMin * 3.0 + 1.0;   /* 接触検出窓は dMin に対して十分広く */
	cp.solver     = p.solver;                                  /* 0=勾配降下, 1=座標降下 */
	cp.cdPitch0   = ( p.cdPitch0   > 0.0 ) ? p.cdPitch0   : 8.0;
	cp.cdPitchMin = ( p.cdPitchMin > 0.0 ) ? p.cdPitchMin : 0.01;
	cp.cdReverse  = p.cdReverse;                               /* 0=前方(根側から), 1=後方(尾側から) */
	cp.cdParallel = p.cdParallel;                              /* 0=直列 GS, 1=彩色ブロック Jacobi */
	cp.cdThreads  = p.cdThreads;                               /* 0=自動(コア数-2), 1=直列 */
	cp.fZ = p.fZ; cp.fAxis = p.fAxis; cp.fOrigin = p.fOrigin;
	if ( p.fixEnds ) { cp.fixedDOF.push_back(0); cp.fixedDOF.push_back(numC + 1); }
	for ( size_t i = 0 ; i < p.fixed.size() ; ++i ) cp.fixedDOF.push_back(p.fixed[i]);
	for ( size_t i = 0 ; i < p.pins.size() ; ++i ) {
		pipe::Pin q;
		q.joint  = p.pins[i].joint;
		q.target = pipe::Vec3{ p.pins[i].target[0], p.pins[i].target[1], p.pins[i].target[2] };
		q.hard   = ( p.pins[i].hard != 0 );
		cp.pins.push_back(q);
	}
	return cp;
}

/* 設計点(S, C..., E)を平坦 ctrl_xyz にして PPAdjustResult を組む。 */
static PPAdjustResult
designToResult(const pipe::ChainDesign& d, int iters, double energy, double maxViol, int feasible)
{
	PPAdjustResult out;
	auto pushv = [&](const pipe::Vec3& v){
		out.ctrl_xyz.push_back(v.x); out.ctrl_xyz.push_back(v.y); out.ctrl_xyz.push_back(v.z);
	};
	pushv(d.S);
	for ( size_t i = 0 ; i < d.C.size() ; ++i ) pushv(d.C[i]);
	pushv(d.E);
	out.npts              = 2 + (int)d.C.size();
	out.iters             = iters;
	out.energy            = energy;
	out.maxClearViolation = maxViol;
	out.feasible          = feasible;
	return out;
}

/* 接触点 B(seg,t) を delta だけ動かすのに必要な設計点移動を Bernstein 基底 × segDesignWeights で
 * 最小ノルム分配して corr/cnt に積む(固定 DOF は除外)。 */
static void
addCorr(std::vector<pipe::Vec3>& corr, std::vector<int>& cnt, int numDOF,
        int m, int seg, double t, const pipe::Vec3& delta, const std::vector<char>& fixed)
{
	double u = 1.0 - t;
	double bb[3] = { u*u, 2.0*u*t, t*t };
	std::array<std::vector<std::pair<int,double> >,3> W = pipe::segDesignWeights(m, seg);
	std::vector<double> wd((size_t)numDOF, 0.0);
	for ( int l = 0 ; l < 3 ; ++l )
		for ( size_t k = 0 ; k < W[l].size() ; ++k ) {
			int dd = W[l][k].first;
			if ( dd >= 0 && dd < numDOF ) wd[(size_t)dd] += bb[l] * W[l][k].second;
		}
	double s2 = 0.0;
	for ( int d = 0 ; d < numDOF ; ++d ) if ( ! fixed[(size_t)d] ) s2 += wd[(size_t)d] * wd[(size_t)d];
	if ( s2 < 1e-12 ) return;
	/* ★ s2 に下限を設けて 1/s2 の増幅を抑える(数値安定化)。接触点が固定 DOF に支配される
	 *   (例: 固定始点 S 近傍 t→0 で Bernstein 重みが固定点に集中)と s2→0 になり、最小ノルム解
	 *   delta*wd/s2 が爆発する。これが FP 依存で Linux 発散/Mac 収束の食い違いを生んでいた。
	 *   下限でクランプすれば、自由 DOF で動かせない分は無理に動かさず(過少補正)、爆発もしない。 */
	double inv = 1.0 / ( ( s2 > 0.25 ) ? s2 : 0.25 );
	for ( int d = 0 ; d < numDOF ; ++d )
		if ( ! fixed[(size_t)d] && wd[(size_t)d] != 0.0 ) {
			corr[(size_t)d] = corr[(size_t)d] + delta * ( wd[(size_t)d] * inv );
			cnt[(size_t)d] += 1;
		}
}

/* 射影的分離パス: movableIdx の Body を、検出した接触の **中心線間方向**(gap≈0 でも安定)に
 * 食い込み量(dMin-gap)だけ押し離す。固定 DOF 除外 + ラプラシアン平滑化でキンクを抑える。
 * energy 法(adjust)が苦手な「押し広げ」を担う。戻り = 最終 max clearance violation。 */
static double
separateScene(pipe::Scene& sc, int mi, const pipe::CtrlParams& cp,
              int maxOuter, double gain, double lambda, double tension)
{
	pipe::ChainDesign& design = sc.bodies[(size_t)mi].design;
	int m = (int)design.C.size();
	if ( m < 1 ) return 0.0;                     /* 制御点なし(直線 body)は対象外 */
	int numDOF = m + 2;
	std::vector<char> fixed((size_t)numDOF, 0);
	for ( size_t i = 0 ; i < cp.fixedDOF.size() ; ++i ) {
		int f = cp.fixedDOF[i];
		if ( f >= 0 && f < numDOF ) fixed[(size_t)f] = 1;
	}
	/* ★ 分離は「違反ペア(gap<dMin)」だけ押せばよい。検出窓は dMin の少し上に絞る
	 *   (energy の広い reportGap のままだと、dMin より十分離れた障害物等まで拾って
	 *    自己接近の解消を妨げることがある)。 */
	pipe::Params det = cp.det;
	det.reportGap = cp.dMin * 1.25 + 1e-3;

	/* ★ 発散ガード: 過拘束(自己 vs 障害物クリアランスが両立不能 等)だと押しが累積して
	 *   形状が爆発しうる。最小違反 best を保持し、初期寸法を超える移動が出たら打ち切って best を返す
	 *   (feasible は数十しか動かず、発散は数千動くので確実に区別できる)。 */
	double bestViol = 1e300;
	bool   diverged = false;
	pipe::ChainDesign best = design;
	std::vector<pipe::Vec3> orig((size_t)numDOF);
	pipe::Vec3 mn = design.point(0), mx = design.point(0);
	for ( int d = 0 ; d < numDOF ; ++d ) {
		orig[(size_t)d] = design.point(d);
		pipe::Vec3 p = design.point(d);
		mn = pipe::Vec3{ std::min(mn.x,p.x), std::min(mn.y,p.y), std::min(mn.z,p.z) };
		mx = pipe::Vec3{ std::max(mx.x,p.x), std::max(mx.y,p.y), std::max(mx.z,p.z) };
	}
	double cap = std::max(std::max(mx.x-mn.x, mx.y-mn.y), std::max(mx.z-mn.z, 1.0));

	/* デバッグ: 環境変数 PIG_SEP_LOG が指すファイルに反復の軌跡を追記(既定 OFF)。
	 *   プラグイン agent の stderr は親に届かないためファイル経由。調査時のみ使う。 */
	const char *dbgpath = ::getenv("PIG_SEP_LOG");
	FILE *dbg = ( dbgpath && *dbgpath ) ? ::fopen(dbgpath, "a") : 0;
	if ( dbg ) ::fprintf(dbg, "=== separateScene mi=%d numDOF=%d dMin=%g gain=%g lambda=%g tension=%g cap=%g maxOuter=%d ===\n",
	                     mi, numDOF, cp.dMin, gain, lambda, tension, cap, maxOuter);

	double maxViol = 0.0;
	for ( int outer = 0 ; outer < maxOuter ; ++outer ) {
		sc.bodies[(size_t)mi].rebuild();
		std::vector<pipe::Contact> cs = pipe::findSceneProximities(sc, det);
		std::vector<pipe::Vec3> corr((size_t)numDOF, pipe::Vec3{0,0,0});
		std::vector<int> cnt((size_t)numDOF, 0);
		maxViol = 0.0;
		for ( size_t k = 0 ; k < cs.size() ; ++k ) {
			const pipe::Contact& c = cs[k];
			double viol = cp.dMin - c.gap;
			if ( viol <= 0.0 ) continue;
			bool aMov = ( c.bodyA == mi ), bMov = ( c.bodyB == mi );
			if ( ! aMov && ! bMov ) continue;     /* 固定–固定は無視 */
			if ( viol > maxViol ) maxViol = viol;
			double share = ( aMov && bMov ) ? 0.5 : 1.0;
			double push  = share * viol * gain;
			/* ★ 中心線点 X,Y の差を方向に使う(接触面法線と違い gap≈0 でも安定)。 */
			pipe::Vec3 X = sc.bodies[(size_t)c.bodyA].chain.segs[(size_t)c.segA].at(c.tA);
			pipe::Vec3 Y = sc.bodies[(size_t)c.bodyB].chain.segs[(size_t)c.segB].at(c.tB);
			pipe::Vec3 dv = X - Y;
			double L = pipe::norm(dv);
			pipe::Vec3 dir = ( L > 1e-9 ) ? dv * (1.0 / L) : pipe::Vec3{0,0,1};
			if ( aMov ) addCorr(corr, cnt, numDOF, m, c.segA, c.tA, dir * push,        fixed);
			if ( bMov ) addCorr(corr, cnt, numDOF, m, c.segB, c.tB, dir * (-push),     fixed);
		}
		if ( maxViol < bestViol ) { bestViol = maxViol; best = design; }   /* 最小違反を保持 */
		if ( maxViol < cp.dMin * 0.01 ) break;    /* ほぼ解消 → 終了(初期から満足なら即抜け) */
		/* 各設計点の移動量(接触数で平均)。 */
		std::vector<pipe::Vec3> disp((size_t)numDOF, pipe::Vec3{0,0,0});
		for ( int d = 0 ; d < numDOF ; ++d )
			if ( ! fixed[(size_t)d] && cnt[(size_t)d] > 0 )
				disp[(size_t)d] = corr[(size_t)d] * (1.0 / cnt[(size_t)d]);
		/* ★ 移動量(push)を制御点列に沿って平滑化してジグザグ(局所自己交差の元)を除く。
		 *   位置でなく増分を平滑化するので曲線が縮まず、オーバーシュートを起こさない。 */
		if ( lambda > 0.0 )
			for ( int pass = 0 ; pass < 2 ; ++pass ) {
				std::vector<pipe::Vec3> sm = disp;
				for ( int dd = 1 ; dd < numDOF - 1 ; ++dd )
					if ( ! fixed[(size_t)dd] ) {
						pipe::Vec3 avg = ( disp[(size_t)(dd-1)] + disp[(size_t)(dd+1)] ) * 0.5;
						sm[(size_t)dd] = disp[(size_t)dd] * (1.0 - lambda) + avg * lambda;
					}
				disp = sm;
			}
		for ( int d = 0 ; d < numDOF ; ++d )
			if ( ! fixed[(size_t)d] ) design.point(d) = design.point(d) + disp[(size_t)d];
		/* ★ 位置テンション = Taubin λ–μ 平滑化(非収縮)。固定端付近の蛇行(高周波)だけ均し、
		 *   コイル径(低周波)は保つ。ラプラシアン(λ>0・収縮)と逆ステップ(μ<0・|μ|>λ・膨張)の
		 *   2 段で、低周波の縮みを打ち消す。押し(クリアランス)とつり合い「滑らか かつ gap≈dMin」へ。 */
		if ( tension > 0.0 ) {
			double mu = tension / ( tension * 0.1 - 1.0 );    /* Taubin(通過帯域 k_PB=0.1) */
			double coefs[2] = { tension, mu };                /* λ(>0,収縮), μ(<0,膨張) */
			for ( int s = 0 ; s < 2 ; ++s ) {
				double coef = coefs[s];
				std::vector<pipe::Vec3> nw((size_t)numDOF);
				for ( int dd = 0 ; dd < numDOF ; ++dd ) nw[(size_t)dd] = design.point(dd);
				for ( int dd = 1 ; dd < numDOF - 1 ; ++dd )
					if ( ! fixed[(size_t)dd] ) {
						pipe::Vec3 lap = ( design.point(dd-1) + design.point(dd+1) ) * 0.5 - design.point(dd);
						nw[(size_t)dd] = design.point(dd) + lap * coef;
					}
				for ( int dd = 1 ; dd < numDOF - 1 ; ++dd )
					if ( ! fixed[(size_t)dd] ) design.point(dd) = nw[(size_t)dd];
			}
		}
		/* 発散検出: 初期位置から cap を超えて動いたら過拘束とみなし best へ戻して打ち切り。 */
		double md = 0.0;
		for ( int d = 0 ; d < numDOF ; ++d ) {
			pipe::Vec3 e = design.point(d) - orig[(size_t)d];
			md = std::max(md, pipe::norm(e));
		}
		if ( dbg && ( outer < 5 || outer % 50 == 0 ) )
			::fprintf(dbg, "  outer=%d maxViol=%g md=%g ncontacts=%zu\n", outer, maxViol, md, cs.size());
		if ( md > cap ) {
			diverged = true;
			if ( dbg ) ::fprintf(dbg, "  DIVERGED outer=%d md=%g cap=%g bestViol=%g\n", outer, md, cap, bestViol);
			break;
		}
	}
	if ( dbg ) {
		::fprintf(dbg, "END diverged=%d bestViol=%g maxViol=%g\n", (int)diverged, bestViol, maxViol);
		::fclose(dbg);
	}
	/* 発散時のみ最小違反形へ戻す。正常時はテンションで均した最終形を返す。 */
	if ( diverged ) { design = best; sc.bodies[(size_t)mi].rebuild(); return bestViol; }
	sc.bodies[(size_t)mi].rebuild();
	return maxViol;
}

/* 接触集合の max clearance violation = max(0, dMin - gap)。 */
static double
ppMaxViol(const std::vector<pipe::Contact>& cs, double dMin)
{
	double v = 0.0;
	for ( size_t i = 0 ; i < cs.size() ; ++i ) {
		double dv = dMin - cs[i].gap;
		if ( dv > v ) v = dv;
	}
	return v;
}

/* シーン全 energy(幾何+接触ペナルティ)を評価。maxIter=0 で adjustScene を「反復ゼロ実行」し
 * 初期 phi を取り出す(Scene は値渡しなので非破壊)。keep-if-lower 判定に使う。 */
static double
evalSceneEnergy(pipe::Scene sc, int mi, pipe::CtrlParams cp)
{
	cp.maxIter = 0;
	cp.alOuter = 1;
	pipe::CtrlResult r = pipe::adjustScene(sc, mi, cp);
	return r.energy;
}

/* 接触フリー区間ポリッシュ。アクティブ接触(gap<=dMin*1.25)に関与しない設計点の極大区間を、
 * **独立サブチェーンに切り出して**(区間両隣のアンカーを端点 S,E に)再緩和する。
 *
 * なぜ「切り出し」か: 接触域をピン留めして区間 DOF を全チェーンのまま自由化しても、
 * ジャンクションの曲げ結合(∫κ²)が区間をスパイラルの急降下接線に連続させる metastable
 * 形状(=終端アーチ)に縛り、勾配降下はそこから出られない(全チェーン energy の局所最小)。
 * 区間を端点自由なサブチェーンに切り出すとジャンクション接線が解放され、より低 energy の
 * 直線基底へ落ちる。書き戻して全 energy が下がった時だけ採用する(単調・安全)。
 *
 * 本解で区間が動けなかったのはステップ飢餓: 巨大な接触ペナルティ勾配が α=stepMax/gmax を絞り、
 * 接触の無い区間が自分のスケールで動けない。切り出すと gmax は幾何スケールに戻る。
 * 戻り = ポリッシュ後の max clearance violation。 */
static double
polishScene(pipe::Scene& sc, int mi, const pipe::CtrlParams& cp)
{
	pipe::ChainDesign& design = sc.bodies[(size_t)mi].design;
	int m = (int)design.C.size();
	if ( m < 1 ) return 0.0;
	int numDOF = m + 2;

	/* 1) アクティブ接触に関与する可動側 DOF を blocked にマーク。 */
	sc.bodies[(size_t)mi].rebuild();
	pipe::Params det = cp.det;
	det.reportGap = cp.dMin * 1.25 + 1e-3;
	std::vector<char> blocked((size_t)numDOF, 0);
	blocked[0] = 1; blocked[(size_t)(numDOF-1)] = 1;          /* 端点は常にアンカー */
	for ( size_t i = 0 ; i < cp.fixedDOF.size() ; ++i ) {     /* 元から固定の DOF */
		int f = cp.fixedDOF[i];
		if ( f >= 0 && f < numDOF ) blocked[(size_t)f] = 1;
	}
	for ( size_t i = 0 ; i < cp.pins.size() ; ++i ) {        /* 硬ピンの拘束 DOF も跨がせない */
		if ( ! cp.pins[i].hard ) continue;                   /* M_j=(C_j+C_{j+1})/2 → DOF j+1, j+2 */
		int j = cp.pins[i].joint;
		if ( j + 1 >= 0 && j + 1 < numDOF ) blocked[(size_t)(j + 1)] = 1;
		if ( j + 2 >= 0 && j + 2 < numDOF ) blocked[(size_t)(j + 2)] = 1;
	}
	auto markSeg = [&](int seg){
		if ( seg < 0 || seg >= m ) return;
		std::array<std::vector<std::pair<int,double> >,3> W = pipe::segDesignWeights(m, seg);
		for ( int l = 0 ; l < 3 ; ++l )
			for ( size_t k = 0 ; k < W[l].size() ; ++k ) {
				int dd = W[l][k].first;
				if ( dd >= 0 && dd < numDOF ) blocked[(size_t)dd] = 1;
			}
	};
	/* ★ block するのは「真にアクティブ(gap≲dMin)」な接触のみ。near-contact(dMin<gap≲広い窓)まで
	 *   block すると、その近傍 DOF(例: 終端アーチの山ピークが spiral コイルに近づいた部分)を
	 *   巻き込んで自由化区間から外してしまい、肝心の山が直せない。山を下げれば spiral から
	 *   離れて gap は増えるので、near-contact DOF は block しなくてよい(keep-if-lower が安全網)。 */
	double blockGap = cp.dMin + 0.5;
	const char *dbgpath = ::getenv("PIG_POLISH_LOG");
	FILE *dbg = ( dbgpath && *dbgpath ) ? ::fopen(dbgpath, "a") : 0;
	std::vector<pipe::Contact> cs = pipe::findSceneProximities(sc, det);
	for ( size_t k = 0 ; k < cs.size() ; ++k ) {
		const pipe::Contact& c = cs[k];
		if ( c.gap > blockGap ) continue;
		if ( c.bodyA == mi ) markSeg(c.segA);
		if ( c.bodyB == mi ) markSeg(c.segB);
	}

	if ( dbg ) {
		int nb = 0; for ( int d = 0 ; d < numDOF ; ++d ) nb += blocked[(size_t)d];
		::fprintf(dbg, "=== polishScene mi=%d numDOF=%d ncontacts=%zu nblocked=%d ===\n",
		          mi, numDOF, cs.size(), nb);
	}

	/* 2) 設計点区間 [lo..hi] を、両隣 lo-1/hi+1 を端点に切り出して純幾何緩和した design を返すラムダ。
	 *    (区間内の自己接近は sub の adjust が dMin で抑える。区間外コイルとの接触は sub からは
	 *     見えないが、それは最後の keep-if-lower=全 energy 評価が捕まえる。) */
	auto relaxSpan = [&](const pipe::ChainDesign& base, int lo, int hi) -> pipe::ChainDesign {
		pipe::ChainDesign out = base;
		pipe::ChainDesign sub;
		sub.S = base.point(lo - 1);
		sub.E = base.point(hi + 1);
		for ( int k = lo ; k <= hi ; ++k ) sub.C.push_back(base.point(k));
		pipe::CtrlParams scp = cp;
		scp.fixedDOF.clear();
		scp.fixedDOF.push_back(0);
		scp.fixedDOF.push_back((int)sub.C.size() + 1);
		scp.pins.clear();
		scp.alOuter = 1;
		pipe::CtrlResult sres = pipe::adjust(sub, sc.bodies[(size_t)mi].radius, scp);
		for ( int k = lo ; k <= hi ; ++k ) out.point(k) = sres.design.C[(size_t)(k - lo)];
		return out;
	};
	auto energyOf = [&](const pipe::ChainDesign& dgn) -> double {
		pipe::Scene s = sc;
		s.bodies[(size_t)mi].design = dgn;
		s.bodies[(size_t)mi].rebuild();
		return evalSceneEnergy(s, mi, cp);
	};

	/* 3) 各「接触フリー区間」を起点に、境界を内側へ貪欲拡張しながら relaxSpan+keep-if-lower。
	 *    終端アーチでは contact-free 区間 [19..25] から lo を内へ伸ばし、山ピーク(17,18)を取り込む。
	 *    更に内側(コイル本体)へ伸ばすと dMin 違反で energy が上がり自動停止する。 */
	pipe::ChainDesign cur = design;
	double curE = energyOf(cur);
	const double EPS = 1e-9;
	const int MAXEXT = 8;

	int i = 1;
	bool any = false;
	while ( i <= numDOF - 2 ) {
		if ( blocked[(size_t)i] ) { i++; continue; }
		int lo0 = i;
		while ( i <= numDOF - 2 && ! blocked[(size_t)i] ) i++;
		int hi0 = i - 1;                                       /* [lo0..hi0] = 接触フリー内部区間 */
		any = true;

		int blo = lo0, bhi = hi0;
		pipe::ChainDesign bD = relaxSpan(cur, blo, bhi);
		double bE = energyOf(bD);

		/* lo を内側(小さい方)へ貪欲拡張。★blocked(端点・固定 DOF・接触 DOF・硬ピン)は
		 *   跨がない: 跨ぐと relaxSpan がその点を可動化して固定/ピン拘束を破る(#3408: 外力 fZ 下では
		 *   点を下げるほど energy が下がるので keep-if-lower ガードも効かず固定点が落下していた)。 */
		for ( int ext = 0 ; ext < MAXEXT && blo - 1 >= 1 && ! blocked[(size_t)(blo - 1)] ; ++ext ) {
			pipe::ChainDesign D = relaxSpan(cur, blo - 1, bhi);
			double e = energyOf(D);
			if ( e < bE - EPS ) { bE = e; bD = D; blo = blo - 1; }
			else break;
		}
		/* hi を内側(大きい方)へ貪欲拡張(同様に blocked は跨がない)。 */
		for ( int ext = 0 ; ext < MAXEXT && bhi + 1 <= numDOF - 2 && ! blocked[(size_t)(bhi + 1)] ; ++ext ) {
			pipe::ChainDesign D = relaxSpan(cur, blo, bhi + 1);
			double e = energyOf(D);
			if ( e < bE - EPS ) { bE = e; bD = D; bhi = bhi + 1; }
			else break;
		}
		/* 両端拡張を合わせた最終形(blo,bhi 確定) */
		if ( blo != lo0 || bhi != hi0 ) { bD = relaxSpan(cur, blo, bhi); bE = energyOf(bD); }

		if ( dbg ) ::fprintf(dbg, "  span seed[%d..%d] -> final[%d..%d] curE=%g bE=%g\n",
		                     lo0, hi0, blo, bhi, curE, bE);
		if ( bE < curE - EPS ) { cur = bD; curE = bE; }       /* この区間のポリッシュを採用 */
	}

	double e0 = energyOf(design);
	bool accept = ( any && curE < e0 - EPS );
	if ( accept ) {
		design = cur;
		sc.bodies[(size_t)mi].rebuild();
	}
	if ( dbg ) {
		::fprintf(dbg, "END any=%d e0=%g curE=%g accept=%d\n", (int)any, e0, curE, (int)accept);
		::fclose(dbg);
	}
	std::vector<pipe::Contact> cs2 = pipe::findSceneProximities(sc, det);
	return ppMaxViol(cs2, cp.dMin);
}

std::vector<PPContact>
pipe_proximity_run(const std::vector<double>& ctrl_xyz, int npts,
                   double r0, double m, const std::vector<double>& radial_sr,
                   double reportGap,
                   double clampS0, double clampS1, double clampR)
{
	pipe::ChainDesign design = makeDesign(ctrl_xyz, npts);
	pipe::Chain chain = design.build();
	pipe::RadiusFn R = makeRadiusFn(r0, m, radial_sr, /*gateUpper=*/true, chain.totalLen(),
	                                clampS0, clampS1, clampR);

	pipe::Params pr;
	pr.reportGap = reportGap;

	std::vector<pipe::Contact> contacts = pipe::findSelfProximities(chain, R, pr);
	std::vector<PPContact> out;
	out.reserve(contacts.size());
	for ( size_t i = 0 ; i < contacts.size() ; ++i ) out.push_back(toPP(contacts[i]));
	return out;
}

PPAdjustResult
pipe_adjust_run(const std::vector<double>& ctrl_xyz, int npts,
                double r0, double m, const std::vector<double>& radial_sr,
                const PPAdjustParams& p,
                double clampS0, double clampS1, double clampR)
{
	pipe::ChainDesign design = makeDesign(ctrl_xyz, npts);
	/* ★ adjust 中はチェーン弧長が変わるので上限 Smax で切らない(gateUpper=false)。 */
	pipe::RadiusFn R = makeRadiusFn(r0, m, radial_sr, /*gateUpper=*/false, 0.0,
	                                clampS0, clampS1, clampR);
	pipe::CtrlParams cp = makeCtrlParams(p, (int)design.C.size());
	pipe::CtrlResult res = pipe::adjust(design, R, cp);

	/* energy 法の後に射影的分離パス(押し広げ)→ 接触フリー区間ポリッシュで仕上げる。
	 * 1 体 Scene に包んで共通処理。 */
	if ( ! p.sepEnable && ! p.polishEnable ) return toAdjustResult(res);
	pipe::Scene sc;
	pipe::Body b;
	b.design = res.design;
	b.radius = R;
	b.movable = true;
	b.rebuild();
	sc.bodies.push_back(b);
	double mv = res.maxClearViolation;
	if ( p.sepEnable )
		mv = separateScene(sc, 0, cp, p.sepIter, p.sepGain, p.sepLambda, p.sepTension);
	if ( p.polishEnable ) {
		double pv = polishScene(sc, 0, cp);
		if ( pv > mv ) mv = pv;
	}
	return designToResult(sc.bodies[0].design, res.iters, res.energy, mv,
	                      res.constraintsFeasible ? 1 : 0);
}

/* PPBody 群 → pipe::Scene。movableIdx の Body だけ半径ゲートを外す(弧長が変わるため)。
 * movableIdx < 0(検出時)は全 Body 静的扱い(gateUpper=true)。 */
static pipe::Scene
buildScene(const std::vector<PPBody>& bodies, int movableIdx)
{
	pipe::Scene sc;
	for ( size_t i = 0 ; i < bodies.size() ; ++i ) {
		const PPBody& b = bodies[i];
		pipe::Body body;
		body.design = makeDesign(b.ctrl_xyz, b.npts);
		double Smax = body.design.build().totalLen();
		bool gateUpper = ( (int)i != movableIdx );
		body.radius  = makeRadiusFn(b.r0, b.m, b.radial_sr, gateUpper, Smax,
		                            b.clampS0, b.clampS1, b.clampR);
		body.movable = ( b.movable != 0 );
		body.rebuild();
		sc.bodies.push_back(body);
	}
	return sc;
}

std::vector<PPContact>
pipe_scene_proximity_run(const std::vector<PPBody>& bodies, double reportGap)
{
	pipe::Scene sc = buildScene(bodies, /*movableIdx=*/-1);
	pipe::Params pr;
	pr.reportGap = reportGap;
	std::vector<pipe::Contact> contacts = pipe::findSceneProximities(sc, pr);
	std::vector<PPContact> out;
	out.reserve(contacts.size());
	for ( size_t i = 0 ; i < contacts.size() ; ++i ) out.push_back(toPP(contacts[i]));
	return out;
}

PPAdjustResult
pipe_scene_adjust_run(const std::vector<PPBody>& bodies, int movableIdx,
                      const PPAdjustParams& p)
{
	pipe::Scene sc = buildScene(bodies, movableIdx);
	int numC = bodies[movableIdx].npts - 2;
	pipe::CtrlParams cp = makeCtrlParams(p, numC);
	pipe::CtrlResult res = pipe::adjustScene(sc, movableIdx, cp);

	if ( ! p.sepEnable && ! p.polishEnable ) return toAdjustResult(res);
	/* energy 法の後に射影的分離パス(押し広げ)→ 接触フリー区間ポリッシュ(固定 body は障害物のまま)。 */
	sc.bodies[(size_t)movableIdx].design = res.design;
	sc.bodies[(size_t)movableIdx].rebuild();
	double mv = res.maxClearViolation;
	if ( p.sepEnable )
		mv = separateScene(sc, movableIdx, cp, p.sepIter, p.sepGain, p.sepLambda, p.sepTension);
	if ( p.polishEnable ) {
		double pv = polishScene(sc, movableIdx, cp);
		if ( pv > mv ) mv = pv;
	}
	return designToResult(sc.bodies[(size_t)movableIdx].design, res.iters, res.energy, mv,
	                      res.constraintsFeasible ? 1 : 0);
}

std::vector<PPSample>
pipe_sample_run(const std::vector<double>& ctrl_xyz, int npts,
                double r0, double m, const std::vector<double>& radial_sr,
                double pitch,
                double clampS0, double clampS1, double clampR)
{
	std::vector<PPSample> out;
	pipe::ChainDesign design = makeDesign(ctrl_xyz, npts);
	pipe::Chain chain = design.build();
	if ( chain.segs.empty() ) return out;          /* 制御点なし(npts<3)=区間なし → 空 */
	double Smax = chain.totalLen();
	pipe::RadiusFn R = makeRadiusFn(r0, m, radial_sr, /*gateUpper=*/true, Smax,
	                                clampS0, clampS1, clampR);

	if ( pitch <= 0.0 ) pitch = Smax / 64.0;
	if ( pitch <= 0.0 ) pitch = 1.0;               /* Smax==0 退化の保険 */

	/* サンプルする弧長値: 等間隔 + 端点(0,Smax) + 半径キーポイント s。 */
	std::vector<double> svals;
	svals.push_back(0.0);
	for ( double s = pitch ; s < Smax ; s += pitch ) svals.push_back(s);
	svals.push_back(Smax);
	for ( size_t i = 0 ; i + 1 < radial_sr.size() ; i += 2 ) {
		double sk = radial_sr[i];
		if ( sk > 0.0 && sk < Smax ) svals.push_back(sk);
	}
	std::sort(svals.begin(), svals.end());
	/* 近接重複を間引く。 */
	double eps = ( Smax > 0.0 ? Smax : 1.0 ) * 1e-9;
	std::vector<double> uniq;
	for ( size_t i = 0 ; i < svals.size() ; ++i )
		if ( uniq.empty() || svals[i] - uniq.back() > eps ) uniq.push_back(svals[i]);

	out.reserve(uniq.size());
	for ( size_t k = 0 ; k < uniq.size() ; ++k ) {
		double s = uniq[k];
		/* s → (seg, t): segStartS でバケット選択 + arcAt 単調を二分法で逆引き。 */
		int seg = 0;
		for ( int i = (int)chain.segs.size() - 1 ; i >= 0 ; --i )
			if ( s >= chain.segStartS[i] ) { seg = i; break; }
		double sLocal = s - chain.segStartS[seg];
		double lo = 0.0, hi = 1.0;
		for ( int it = 0 ; it < 50 ; ++it ) {
			double mid = 0.5 * ( lo + hi );
			double a = chain.arcAt(seg, mid) - chain.segStartS[seg];
			if ( a < sLocal ) lo = mid; else hi = mid;
		}
		double t = 0.5 * ( lo + hi );
		pipe::Vec3 p = chain.segs[seg].at(t);
		std::optional<double> r = R(s);
		PPSample smp;
		smp.pos[0] = p.x; smp.pos[1] = p.y; smp.pos[2] = p.z;
		smp.r = r ? *r : 0.0;
		out.push_back(smp);
	}
	return out;
}
