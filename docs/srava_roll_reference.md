---
title: srava roll ライブラリ リファレンス
---

# srava roll ライブラリ — 螺旋巻きつけ（BLH ホーン生成）

> **配置**: 標準ライブラリ `std/roll.sra`（インストールすると `$PREFIX/share/srava/lib/std/roll.sra`）。
> 利用側は `include "std/roll.sra";` と書く。ドライバ例は `examples/roll_sample.sra`。
>
> **必須モジュール: `pipe_proximity`** — 本ライブラリは **std で唯一、モジュールに依存する**
> （他の std ライブラリは幾何カーネルだけで動く）。ロードされているかは `srava --modules` の
> `loaded:` に `pipe_proximity` が出るかで確認できる。無ければ `-DSRAVA_MODULE_PIPEPROX=ON`
> （既定 ON）でビルドし直す。
>
> **推奨 params**（知らないと事故る 2 つ）:
>
> | キー | 推奨 | 理由 |
> |---|---|---|
> | `parallel` | `1` | 事実上必須。`0` だと 1 solve が ~150 秒かかり、実用サイズでは 1 時間級になる |
> | `wSpace` | `0.1` | 区間長の正則化。入れないと自由尾の制御点が 1 点に寄り、gap が 1mm 級まで潰れる |
>
> 例: `{maxIter:100, threads:21, parallel:1, wSpace:0.1}`
>
> **実行時間の目安**（実測・threads:21 / parallel:1）: 弧長 3000mm 到達（15 手）で
> **real 8m48s / user 75m17s**。短時間の回帰用に 2 手だけ回す縮小版が `test/roll_min.sra`（約 80 秒）
> にあり、`cmake -DSRAVA_SLOW_TESTS=ON` で ctest に登録される（既定は非登録）。


芯パイプ（マンドレル）に**太さの変化するパイプを密接に沿わせて巻きつけ**、螺旋バックロードホーン
（BLH）などを生成する継続法アルゴリズムのライブラリ。`pipe_proximity` / `pipe_scene_adjust`
（[**プラグインリファレンス**](srava_plugin_reference.html)）を土台に、`.sra` で実装する。

```
include "std/roll.sra";     // 標準ライブラリに追加した場合
include "roll.sra";         // 利用ディレクトリに置いた場合
```

エントリ形式・型の凡例は [**関数リファレンス**](srava_function_reference.html) に準拠。
本ライブラリは角度 `alpha`/`beta` を**度**で受ける（内部の `gamma` はラジアン）。実行例は `roll_sample.sra`。

## 概要 — 巻きつけプロセス

**始点を固定し、終点を距離 `L` の方向へ引っ張りつつ `pipe_scene_adjust` を逐次適用**し、少しずつ位置を
進めることで、`pipe_proximity` 単独では不可能な緻密な巻きを作る（＝継続法）。

1. `roll_initial` で **1周目＋引っ張り尾**を生成し、**無重力**で最適化。
2. `roll_step` を繰り返し、1手ごとに角度 `beta` だけ巻きを進める（重力 `fz`・締込 `fax` を掛けられる）。
3. 目標（弧長／巻き数／尾リザーブ枯渇）で停止。

幾何: 螺旋半径 `rho = rp + rc + d`、ピッチ（z/周）`Pz = 2*rp + d`（`rp`=巻き管基準半径, `rc`=芯半径）。
テーパは `pipe.radius = [r, m]`（指数ホーン: 半径`(s) = r·exp(m·弧長)`）。

## body の形式

- `core = {ctrl: 点列, radius: スカラ, movable: 0}` … z 軸に沿った芯（マンドレル）。半径はスカラ。
- `pipe = {radius: [r, m], movable: 1}` … 巻き管。`roll_initial` では `ctrl` を与えず本関数が生成。
  `roll_step` には**巻き済みの `ctrl` を持つ** `pipe` を渡す。

## 関数

### `roll_initial(core, pipe, d, N, L, params)` — 1周目＋引っ張り尾を生成し無重力で最適化  〔stdlib: roll〕
`3D` · → `ハッシュ`

1周分の螺旋制御点（角度 `2π/N` ずつ・高さ `Pz·i/N` ずつ）と、そこから z 軸距離が `L` を超えるまで
弧上等間隔に伸ばす引っ張り尾を生成し、無重力（`fz=fax=0`）で `pipe_scene_adjust` して返す。

**入力**
- `core` 芯 — `ハッシュ`（`{ctrl, radius:スカラ, movable:0}`）
- `pipe` 巻き管 — `ハッシュ`（`{radius:[r,m], movable:1}`。`ctrl` は不要）
- `d` マージン — `スカラ`
- `N` 一周分割数 — `整数`
- `L` 引っ張り距離 — `スカラ`（`> rho` 必須。3m ホーンなら 500 程度）
- `params` 緩和設定 — `ハッシュ`（`pipe_scene_adjust` 用。`maxIter`/`wSpace`/`parallel` 等。`fixEnds`/`dMin`/`solver`/`fZ`/`fAxis` は本関数が強制）

**出力** 最適化後の巻き管 — `ハッシュ`（`{ctrl, radius, movable:1, raw:<solve結果>, pp0:<最適化前 ctrl>}`）

- 例: `var st = roll_initial(core, pipe, 8, 12, 500, params);`
- 関連: `roll_step`

### `roll_step(core, pipe, d, N, L, beta, alpha, params, fz, fax)` — beta だけ巻きを進める  〔stdlib: roll〕
`3D` · → `ハッシュ`

巻き済み `pipe` の末尾から解放帯 `[i1..N0]` を検出（`ctrl[i]` の z 軸内向き垂線と `ctrl[N0]-ctrl[i]`
のなす角を `90±alpha` で切る）、`0..i1` を固定して、方位を `beta` 進めた終点 `P`（平面 S ∩ 鉛直線）へ
向け尾を再敷設し、外力 `fz`（重力）/`fax`（締込）付きで再緩和する。

**入力**
- `core` 芯 — `ハッシュ`（`movable:0`）
- `pipe` 巻き済み巻き管 — `ハッシュ`（`{ctrl, radius:[r,m], movable:1}`）
- `d` マージン — `スカラ`
- `N` 一周分割数 — `整数`
- `L` 引っ張り距離 — `スカラ`
- `beta` 1手の巻き進み角 — `スカラ`（**度**。`N`と整合させ 360/N 目安）
- `alpha` 解放帯角 — `スカラ`（**度**）
- `params` 緩和設定 — `ハッシュ`
- `fz` 重力（z 平行力, 負=下向き） — `スカラ`（`0` で無重力）
- `fax` 締込（z 軸へ寄せる力） — `スカラ`（重力併用時の発散防止。`0` で無し）

**出力** 次状態 — `ハッシュ`
- 成功: `{ok:1, ctrl, pre:<solve前 ctrl>, radius, movable:1, raw, diag:{i0,i1,gamma,P,N0}}`
- 縮退/枯渇: `{ok:0, err:<"reserve_depleted"|"planeS_degenerate"|"P_parallel">, ctrl:<直前>}`

- 例: `var nx = roll_step(core, st, 8, 12, 500, 30, 20, params, -0.003, 0.05);`
- 注: 外力を掛けるなら `fax`（締込）併用が必須。`fz` 単独は自由尾が累積ドリフトで発散する
- 関連: `roll_initial`

### `respace(ctrl, radius, npts)` — 制御点を今の中心線上で等弧長に取り直す  〔stdlib: roll〕
`3D` · → `点列`

現在の中心線（連続 3 点ベジエ＝`pipe_sample` が忠実再現）上で、`npts` 点を等弧長・端点保持・重複なしに
取り直す整形ツール。曲線をほぼ変えずに寄った制御点をほどく。最終形の整形にも単体で使える。

**入力**
- `ctrl` 制御点列 — `点列`
- `radius` 半径 — `スカラ` or `[r,m]`（弧長は半径非依存だが `pipe_sample` に渡す）
- `npts` 取り直し点数 — `整数`

**出力** 等弧長化した制御点列 — `点列`（`npts` 点・端点保持）

- 例: `var cc = respace(st.ctrl, [25, 0.0008], length(st.ctrl));`
- 関連: `respace_range`, `arclen`(std/curve), `pipe_sample`

### `respace_range(ctrl, radius, lo, hi)` — 部分区間だけ等弧長化  〔stdlib: roll〕
`3D` · → `点列`

`ctrl[lo..hi]` だけを等弧長化（同数・両端 `lo,hi` は不動）。範囲外は不変。「凍結する区間は触らず、
自由部だけほどく」用途に。

**入力**
- `ctrl` 制御点列 — `点列`
- `radius` 半径 — `スカラ` or `[r,m]`
- `lo` `hi` 区間端 index — `整数`

**出力** 区間を等弧長化した制御点列 — `点列`（全長不変）

- 例: `var cc = respace_range(st.ctrl, [25,0.0008], i1, N0);`
- 関連: `respace`

## パラメータ早見

| 記号 | 意味 | 目安 |
|---|---|---|
| `a` | 芯（マンドレル）半径 | 100 |
| `r` | 巻き管 基準半径（`radius[0]`） | 25 |
| `m` | テーパ増加レート（指数） | 0（等径）〜0.0008（3m ホーン）。0.001 で約1.7周が限界 |
| `d` | マージン | 8 |
| `N` | 一周分割数 | 12 |
| `L` | 引っ張り距離 | 500（`> rho=r+a+d`） |
| `beta` | 1手の巻き進み角 [度] | 30（=360/N） |
| `alpha` | 解放帯角 [度] | 20 |
| `fz` | 重力（z, 負=下向き） | 0（無重力）／-0.003（`fax` 併用時） |
| `fax` | 締込（z 軸へ） | 0／0.05（重力併用時の安定化） |
| `params.wSpace` | 制御点均一化の重み | 0.1 |
| `params.parallel` | 並列 | 1（threads と併用。0 は激遅） |

## 注意・限界

- **重力を掛けるなら `fax`（締込）併用**。`fz` 単独は自由尾（`i1〜P` の長い引張区間）が緩和のたび垂れ、
  `i1` 前進で凍結され累積ドリフト→発散する。`fax` が尾を芯へ引き戻して有界化する。
  動作確認済み: `fz=-0.003, fax=0.05` で 3m ホーン完走。
- **テーパ `m` の上限**は芯・初期半径とクリアランスで決まる。`a=100,r=25,d=8` では
  `m≈0.0004` でフル3周、`m≈0.0008` で弧長 3m 級 BLH ホーン、`m=0.001` は約1.7周で
  巻き管が芯を大きく上回りクリアランス破綻。
- 内部ピン `params.fixed`（index 配列）は外力下でも固定点を保持する（host 修正 2026-07-30, Redmine #3408）。
- 巻き工程は `params.parallel:1`（`threads` 併用）で回す。`parallel:0` は本問題で ~150s/solve と激遅。
