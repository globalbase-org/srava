#pragma once
// 距離調整コントローラ（ペナルティ法）
//   エネルギー  Phi = wLen*L + wBend*∫κ²ds + wPin*Σ|M_i-target|²
//                    + wPenalty*Σ_k [dMin - gap_k]_+²
//   を勾配降下で最小化。gap>=dMin（不等式・一様クリアランス）をペナルティで近似する。
//   固定点は DOF を凍結、内部通過点(中点)はソフトピンで指定。端点は自動固定しない。
#include "pipe/vec3.hpp"
#include "pipe/bezier.hpp"
#include "pipe/radius.hpp"
#include "pipe/proximity.hpp"
#include "pipe/scene.hpp"
#include <vector>

namespace pipe {

// ChainDesign は bezier.hpp に移動（Scene と controller で共用）。

// 内部通過点(中点 M_joint = (C[joint]+C[joint+1])/2) を target へ留めるピン
//   hard=false : ソフト（ペナルティ。他の力と妥協、過拘束でも破綻しない）
//   hard=true  : 硬拘束（零空間射影で厳密に M_joint=target。過拘束だと実行不能）
struct Pin { int joint; Vec3 target; bool hard = false; };

struct CtrlParams {
    double dMin     = 0.2;   // 一様クリアランス（gap >= dMin）
    double wLen     = 1.0;   // テンション（弧長）重み
    double wBend    = 0.1;   // 曲げ（曲率）重み
    double wPin     = 1e3;   // 通過点ピン重み
    double wPenalty = 1e3;   // 接触ペナルティ重み
    // 制御点間隔の均一化（接線方向スライドのヌルモード抑制）。隣接区間長の差 (|e_i|-|e_{i-1}|)² を罰する。
    //   長さ/曲げ項は点を曲線に沿ってスライドさせる変形にほぼ不感なので、接触の無い区間（空中の尾部など）で
    //   制御点が密集/崩壊しやすい。この項が「間隔を揃えるばね」として働き均一化する。既定 0（従来と完全一致）。
    double wSpace   = 0.0;
    // 外力（単位長あたり一定強度の保存力。ほどけ防止・束ね用）
    double fZ       = 0.0;   // z 並行の力（符号で向き。負で重力下向き）  U=-fZ*z
    double fAxis    = 0.0;   // z 軸へ向かう力（正=内向き、束ねる）        U= fAxis*rho
    double fOrigin  = 0.0;   // 原点へ向かう力（正=内向き、集める）        U= fOrigin*r
    bool   radiusCoupling = true;  // 半径の弧長カップリング項を入れる（Eulerian r(s)）
    // CCD（連続衝突：ステップ途中の貫通＝トポロジー破壊を禁止）
    bool   ccd        = false; // 既定 off（コスト増）。結び目のトポロジー保存時に on
    bool   ccdConservative = true; // true=保守的前進(見逃しなし)、false=部分ステップ標本
    int    ccdSubsteps= 8;     // 標本法のときの分割数
    double ccdMargin  = 0.0;   // 許容下限（centerDist-rA-rB がこの値を割ったら貫通とみなす）
    std::vector<int> fixedDOF;   // 固定する設計点 index（端点も明示指定。自動固定なし）
    std::vector<Pin> pins;       // 内部通過点ピン
    int    maxIter  = 200;
    double stepMax  = 0.05;  // 1ステップの最大移動量（κr<1 維持・安定化）
    double tol      = 1e-6;  // 勾配ノルム停止閾値
    // ソルバ選択: 0=勾配降下（従来・全点同時にフル勾配方向へ）, 1=座標降下（単点・軸並行 line search）
    int    solver     = 0;
    double cdPitch0   = 8.0;   // 座標降下の初期 pitch（1点1軸の試行移動量）
    double cdPitchMin = 0.01;  // 座標降下の最小 pitch（これ未満で各点打ち切り）
    // 座標降下の点スイープ方向: 0=前方（DOF 0→末尾・固定根 c0 側から）, 1=後方（末尾→0・可動な尾側から）。
    // 勾配降下(solver=0)には影響しない（全点同時更新のため順序非依存）。
    int    cdReverse  = 0;
    // 座標降下の並列化: 0=直列 Gauss-Seidel(既定・順序依存・厳密) / 1=接触グラフ彩色のブロック Jacobi。
    //   1 は非干渉(隣接でも接触でもない)制御点を同時更新。長距離の弧長カップリングは無視する近似なので、
    //   位置が固まった微調整段で有効(弧長が大きく動く初期は 0 推奨)。スクリプトで段階的に使い分ける。
    int    cdParallel = 0;
    // スレッド上限。0=自動(コア数-2) / 1=直列(スレッド不使用)。cdParallel=0 でも各点の 6 試行を並列化(結果は直列と一致)。
    int    cdThreads  = 0;
    Params det;              // 検出パラメータ（reportGap は内部で dMin 以上へ）
    // 拡張ラグランジュ（クリアランス gap>=dMin を厳密化）
    int    alOuter  = 1;     // 外ループ回数。1=純ペナルティ（従来）、>1 で乗数更新
    double alWindow = 0.25;  // 乗数の対応付け窓（(seg対, t) で照合。t 空間の許容）
};

struct CtrlResult {
    ChainDesign design;
    Chain       chain;
    int         iters;
    double      energy;
    std::vector<Contact> contacts;
    bool        constraintsFeasible = true; // 硬拘束（固定＋硬ピン）が両立したか
    double      pinResidual = 0.0;          // 硬拘束の残差ノルム（>tol で実行不能）
    double      maxClearViolation = 0.0;    // max(0, dMin - gap)（拡張ラグランジュで →0）
};

// エネルギー部品（公開：テスト・可視化用）
double lengthEnergy(const Chain& ch);   // = totalLen
double bendingEnergy(const Chain& ch);  // = ∫κ²ds
double externalPotentialEnergy(const Chain& ch, const CtrlParams& cp); // = ∫U ds

// 調整本体（単一チェーン）：初期設計から平衡へ。
CtrlResult adjust(ChainDesign init, const RadiusFn& R, CtrlParams cp);

// 調整本体（シーン）：movableIdx の Body を、他の固定 Body 群を障害物として平衡へ。
// 接触は「movable 自己」＋「movable と各固定 Body の交差」。返り値 design/chain は movable。
CtrlResult adjustScene(Scene sc, int movableIdx, CtrlParams cp);

// CCD: from→to の直線運動の途中で貫通（centerDist-rA-rB < margin）が起きないか。
// true = 安全（すり抜けなし）。substeps で運動を分割サンプルして検査する。
bool motionSafe(const ChainDesign& from, const ChainDesign& to, const RadiusFn& R,
                const Params& det, int substeps, double margin);
bool motionSafeScene(Scene sc, int movableIdx,
                     const ChainDesign& from, const ChainDesign& to,
                     const Params& det, int substeps, double margin);
// 保守的前進版（見逃しなし）。距離÷最大速度で安全刻みに前進し最初の TOI を捕まえる。
bool motionSafeCA(const ChainDesign& from, const ChainDesign& to, const RadiusFn& R,
                  const Params& det, double margin);
bool motionSafeSceneCA(Scene sc, int movableIdx,
                       const ChainDesign& from, const ChainDesign& to,
                       const Params& det, double margin);

} // namespace pipe
