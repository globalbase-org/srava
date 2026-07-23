#ifndef ___pipe_proximity_adapter_H___
#define ___pipe_proximity_adapter_H___

/* pipe_proximity アダプタの境界(plain 型のみ)。
 *
 * pipe_proximity は namespace `pipe` を持ち、これは POSIX の `::pipe()`(SDK が unistd 経由で引く)と
 * グローバルスコープで衝突する。そこで TU を分離する:
 *   - pipe_proximity_adapter.cpp: pipe ヘッダ(namespace pipe)を include。SDK/pigData は触らない。
 *   - pipe_proximity_agent.cpp  : SDK(pigData/::pipe)を include。pipe ヘッダは触らない。
 * 両者の境界が **この plain 型ヘッダ**(pipe も pigData も登場しない)。 */

#include <vector>

struct PPContact {
	double gap;
	double pA[3], pB[3], normal[3];
	double sA, sB, rA, rB;
	int    bodyA, bodyB;   /* Scene 用の Body 番号(単一チェーンでは 0)。 */
};

/* 半径プロファイル指定(各 run / Body 共通):
 *   radial_sr が空      → 指数 r(s) = r0 * exp(m * s)
 *   radial_sr が非空    → [s0,r0,s1,r1,...] キーポイントの線形補間(端の外側は端値クランプ)。
 *                         この場合 r0/m は無視。s は弧長。
 * 指数(exp)形には弧長クランプを併用可(clampS0>=0 で有効):
 *   clampS0=clampS1=p1, clampR=r1  → s>=p1 で r1 に固定([r,m,p1,r1]・p1 で不連続)
 *   clampS0=p1<clampS1=p2, clampR=r2 → p1..p2 で指数値から r2 へ線形、s>=p2 で r2([r,m,p1,p2,r2])
 *   コイルの尾(引っ張り部)の太さを途中から細く/固定したいとき用。線形補間形には効かない。 */

/* 自己接近検出(単一チェーン)。ctrl_xyz = 制御点を [x0,y0,z0,...] と平坦化(npts 点)。
 * reportGap 以下の接近を gap 昇順で返す。 */
std::vector<PPContact>
pipe_proximity_run(const std::vector<double>& ctrl_xyz, int npts,
                   double r0, double m, const std::vector<double>& radial_sr,
                   double reportGap,
                   double clampS0 = -1, double clampS1 = -1, double clampR = 0);

/* 通過点ピン: 内部通過点 M_joint = (C[joint]+C[joint+1])/2 を target へ留める。
 * hard != 0 = 厳密(零空間射影) / hard == 0 = ソフト(ペナルティ)。 */
struct PPPin { int joint; double target[3]; int hard; };

/* 距離調整コントローラ(pipe::adjust / adjustScene)の全パラメータ。 */
struct PPAdjustParams {
	double dMin;                  /* 一様クリアランス目標 gap >= dMin */
	int    maxIter;               /* 反復上限(<=0 で 200) */
	int    fixEnds;               /* 端点(S,E)を固定 DOF に加える(利便) */
	double wLen;                  /* 張力(弧長)重み(既定 1.0)。テール張力 ∝ wLen */
	double wBend;                 /* 曲げ正則化重み */
	double wPenalty;              /* 接触ペナルティ重み(既定 1e3)。dMin 復元力 ∝ wPenalty */
	double stepMax;               /* 1 ステップ最大移動量(既定 0.05・κr<1 安定化) */
	int    alOuter;               /* 拡張ラグランジュ外ループ回数(既定 4・1=純ペナルティ) */
	double fZ;                    /* 外力: z 並行(負で重力下向き)  U=-fZ*z */
	double fAxis;                 /* 外力: z 軸へ向かう(正で束ねる) U= fAxis*rho */
	double fOrigin;               /* 外力: 原点へ向かう(正で集める) U= fOrigin*r */
	std::vector<int>   fixed;     /* 明示的に固定する設計点 index(0=S, 1..m=C, m+1=E) */
	std::vector<PPPin> pins;      /* 通過点ピン */
	/* 射影的分離パス(energy 後段の「押し広げ」)。energy 法が苦手な重なり解消を担う。 */
	int    sepEnable;             /* 分離パスを行うか(既定 1) */
	double sepGain;               /* 押し離しゲイン(既定 0.5) */
	int    sepIter;               /* 分離反復上限(既定 600) */
	double sepLambda;             /* 押し増分の平滑化係数(ジグザグ抑制・既定 0.15) */
	double sepTension;            /* 位置テンション(実験・既定 0)。>0 は曲線短縮でコイル径まで縮むので注意 */
	/* 接触フリー区間ポリッシュ(energy 後段)。アクティブ接触に関与しない制御点の極大区間を、
	 * 接触域をピン留めしたまま再緩和する。ステップ飢餓で本解では動けず劣る局所最小(終端アーチ等)
	 * に取り残された区間を、剛な接触ペナルティ勾配の支配を外して幾何最小へ落とす。 */
	int    polishEnable;          /* ポリッシュを行うか(既定 1) */
	/* ソルバ選択: 0=勾配降下(従来), 1=座標降下(単点・軸並行 line search)。
	 * cd は cd.cpp の座標降下アルゴリズムを本番へ移植したもの。様々なパターンで cd/協調を評価する用。 */
	int    solver;                /* 0=grad(既定), 1=cd */
	double cdPitch0;              /* 座標降下 初期 pitch(既定 8.0) */
	double cdPitchMin;            /* 座標降下 最小 pitch(既定 0.01) */
	int    cdReverse;             /* 座標降下スイープ方向: 0=前方(根 c0 側から), 1=後方(尾側から) */
	int    cdParallel;            /* 座標降下: 0=直列 Gauss-Seidel(既定), 1=接触グラフ彩色ブロック Jacobi */
	int    cdThreads;             /* スレッド上限: 0=自動(コア数-2), 1=直列 */
};

struct PPAdjustResult {
	std::vector<double> ctrl_xyz;   /* 調整後の制御点(入力と同じ [x,y,z,...] 並び・S..C..E) */
	int    npts;
	int    iters;                   /* 反復回数 */
	double energy;                  /* 最終エネルギー */
	double maxClearViolation;       /* max(0, dMin - gap) 残差(0 に近いほどクリアランス達成) */
	int    feasible;                /* 硬拘束(固定 DOF / hard ピン)が両立したか(1/0) */
};

/* 自己接近する初期設計を、gap >= dMin を満たすよう制御点を動かす(単一チェーン)。 */
PPAdjustResult
pipe_adjust_run(const std::vector<double>& ctrl_xyz, int npts,
                double r0, double m, const std::vector<double>& radial_sr,
                const PPAdjustParams& p,
                double clampS0 = -1, double clampS1 = -1, double clampR = 0);

/* N 体(Scene)の 1 Body。中心線 + 半径プロファイル + 可動フラグ。 */
struct PPBody {
	std::vector<double> ctrl_xyz;
	int    npts;
	double r0, m;
	std::vector<double> radial_sr;
	int    movable;
	double clampS0 = -1, clampS1 = -1, clampR = 0;   /* exp 形の弧長クランプ(clampS0<0=無効) */
};

/* N 体近接検出(可動 body の自己接近 + 異 body 間の交差)。各 PPContact に bodyA/bodyB が入る。 */
std::vector<PPContact>
pipe_scene_proximity_run(const std::vector<PPBody>& bodies, double reportGap);

/* N 体調整: movableIdx の Body を、他の固定 Body 群を障害物として平衡へ。
 * 返りは可動 Body の調整後 ctrl(PPAdjustResult)。fixed/pins は可動 Body の DOF に効く。 */
PPAdjustResult
pipe_scene_adjust_run(const std::vector<PPBody>& bodies, int movableIdx,
                      const PPAdjustParams& p);

/* 中心線サンプル点(可視化・tube 用)。pos = 中心線上の点、r = その弧長 s での半径 R(s)。 */
struct PPSample { double pos[3]; double r; };

/* 中心線を**弧長等間隔ピッチ** pitch(<=0 で Smax/64)でサンプルし、各点に R(s) を付けて返す。
 * 端点(s=0, Smax)と半径キーポイント([[s,r]] の各 s)は強制的に含める(テーパの折れをクッキリ出す)。
 * 弧長 s → (seg,t) は segStartS バケット + arcAt 単調の二分法で逆引き(ライブラリの正確な弧長)。 */
std::vector<PPSample>
pipe_sample_run(const std::vector<double>& ctrl_xyz, int npts,
                double r0, double m, const std::vector<double>& radial_sr,
                double pitch,
                double clampS0 = -1, double clampS1 = -1, double clampR = 0);

#endif
