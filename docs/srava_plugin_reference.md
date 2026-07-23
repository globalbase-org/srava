# srava プラグインリファレンス

srava は、本体を再ビルドせずに機能を足せる **プラグイン機構**を持つ。プラグインは独立した実行体
（プラグインエージェント）で、登録した **op 名**を srava プログラムから普通の関数のように呼べる。
この機構は pig 層の機能で、値（数・配列・文字列・ハッシュ）だけをやり取りする（CGAL もメッシュも
知らなくてよい）。設計の詳細は内部ドラフト `docs/plugin_spec.md` を参照。

---

## 1. プラグインの構造

| 層 | 役割 |
|---|---|
| **pig**（framework） | プラグインレジストリ・エージェントノード・値コーデック・SDK |
| **srava**（言語） | パーサが登録 op 名を内部のプラグインエージェントノードへ繋ぐ |
| **plugin**（3rd party） | 便利機能の実体。自分の op を提供。host を知らない |

- プラグイン = 独立プロセス。`pigwire` プロトコルで planner と 1 往復する。
- 値の受け渡しは pig 値（null / 整数 / 小数 / 文字列 / 配列 `[...]` / ハッシュ `{"k":v,...}`）。
- 各呼び出しは自己完結（グローバル状態なし）。結果は op 名 + 引数ハッシュで **内容アドレスキャッシュ**に乗る。

### マニフェスト（`*.plugin`）

プラグインは記述子ファイルを置いて登録する（**1 ファイル 1 op**・行ベース・`#` でコメント）:

```
op   = pipe_proximity                          # srava から呼ぶ名前
bin  = /usr/local/libexec/srava/plugins/...    # 起動する実行体(絶対パス)
out  = value                                   # 出力種(v0 は value のみ)
args = 1..3                                     # arity(検証用・任意。"N" / "N.." / "N..M")
```

探索順（先に見つかったものを採用）:

1. `$PIG_PLUGIN_PATH`（`:` 区切りで複数ディレクトリ可）
2. `~/.config/srava/plugins/`
3. `$PREFIX/share/srava/plugins/`（`cmake --install` の配置先 = 既定の sysdir）

1 つの実行体が **複数の op** を serve してもよい（op ごとにマニフェスト 1 ファイル・`bin` は同じ）。
pipe_proximity プラグインは 1 つの bin で 4 op（後述）を提供する。

---

## 2. インストール（同梱 pipe_proximity プラグイン）

pipe_proximity（可変太さ配管の自己接近検出・別 repo・MIT・CGAL 非依存）は **opt-in** で同梱できる:

```sh
cmake -S . -B build -DSRAVA_PLUGIN_PIPEPROX=ON   # 既定 OFF。ON で FetchContent 取得
cmake --build build -j
sudo cmake --install build                        # srava/agent + プラグイン一式を $PREFIX へ
```

- `cmake --install` が配置するもの:
  - エージェント → `$PREFIX/libexec/srava/plugins/pipe_proximity_agent`
  - マニフェスト → `$PREFIX/share/srava/plugins/{pipe_proximity,pipe_adjust,pipe_scene_proximity,pipe_scene_adjust}.plugin`
    （installed bin の絶対パスを指して自動生成）
- → install 後は**環境変数なしで** `pipe_proximity(...)` 等が使える。
- 開発中（install せず）は、マニフェストを作って `PIG_PLUGIN_PATH` で指す:
  ```sh
  D=$(mktemp -d)
  for op in pipe_proximity pipe_adjust pipe_scene_proximity pipe_scene_adjust; do
    printf 'op=%s\nbin=%s\nout=value\n' "$op" "$PWD/build/pipe_proximity_agent" > "$D/$op.plugin"
  done
  PIG_PLUGIN_PATH="$D" srava my.sra
  ```

---

## 3. pipe_proximity プラグインの op

中心線は **制御点列**で与える: `[[x,y,z], ...]`（先頭 = 始点 S / 末尾 = 終点 E / 中間 = off-curve
制御点 C）。内部では中点法 2 次ベジエ鎖（通過点 = S, mid(Cᵢ,Cᵢ₊₁)…, E）として扱う。

### 3.1. 半径プロファイル（`radius`）

すべての op で `radius` 引数は**形で自動判別**される（弧長 s → 半径）:

| `radius` | プロファイル |
|---|---|
| スカラ `r` | 一定半径 `r` |
| フラット `[r0, m]` | 指数 `r(s) = r0·exp(m·s)`（`m>0` 太る / `m<0` テーパ・**無限に伸びる**） |
| フラット `[r0, m, p1, r1]` | 指数を**弧長 p1 以降は `r1` に固定**（p1 で不連続）。指数の伸びを途中で止める |
| フラット `[r0, m, p1, p2, r2]` | 指数を `p1`→`p2` で**線形に `r2` へ移行**し、`p2` 以降 `r2` 固定（不連続を避けたい時） |
| ネスト `[[s,r],…]` | 弧長キーポイントの**線形補間**（s 昇順に整列・範囲外は端値クランプ） |

- 4/5 要素の弧長クランプは**指数形にだけ**効く（ネスト線形補間形には効かない）。用途: コイルの**尾＝引っ張り部**は太さが邪魔になるので、途中から細く固定する等。スカラやネスト形で同じことをしたいときは、`r1`/`r2` を細くした区間をネストで直接書けばよい。
- `p1`/`p2` は**弧長**（始点からの距離）。`[r0, m, p1, r1]` は p1 で指数値 `r0·exp(m·p1)` から `r1` へ飛ぶ。連続にしたいなら `[r0, m, p1, p2, r2]` で `p1`→`p2` の傾斜区間を設ける。

### 3.2. `pipe_proximity(ctrl_pts, radius, report_gap)` — 自己接近検出

`report_gap` 以下の自己接近を gap 昇順で返す。

```
var pts  = [[0,0,0],[12,0,0],[10,2,0],[12,4,0],[0,4,0]];
var hits = pipe_proximity(pts, 0.8, 1.0);
// hits = [[gap, pA, pB, normal, sA, sB, rA, rB], ...]   (無ければ [])
//   gap=表面間隙, pA/pB=両壁の接近点, normal=法線, sA/sB=弧長, rA/rB=その点の半径
```

### 3.3. `pipe_adjust(ctrl_pts, radius, params)` — 距離調整コントローラ

自己接近する設計を、一様クリアランス `gap >= dMin` を満たすよう制御点を動かす
（ペナルティ/拡張ラグランジュ法の勾配降下）。

```
var res = pipe_adjust(pts, 0.8, { dMin: 0.6, maxIter: 400, fixEnds: 1 });
// res = { ctrl:[[x,y,z],...], iters, energy, clearViolation, feasible }
//   ctrl = 調整後の制御点(入力と同じ並び)。そのまま tube / pipe_proximity に渡せる。
//   clearViolation = max(0, dMin-gap) 残差(0 に近いほど達成)。feasible = 硬拘束が両立したか。
var moved = pipe_adjust(pts, 0.8, {dMin:0.6}).ctrl;
```

`params` ハッシュのキー（すべて任意・省略時は既定値）:

| キー | 既定 | 意味 |
|---|---|---|
| `dMin` | 0.5 | 目標クリアランス（`gap >= dMin`） |
| `maxIter` | 200 | 反復上限 |
| `solver` | `"grad"` | ソルバ選択。`"grad"`=勾配降下（全点同時にフル勾配方向）/ `"cd"`（または `cd:1`）=**座標降下**（各制御点を軸並行に1点ずつ line search）。下記 |
| `cdPitch0` | 8.0 | 座標降下の初期 pitch（1点1軸の試行移動量。半減しながら探索） |
| `cdPitchMin` | 0.01 | 座標降下の最小 pitch（これ未満で各点を打ち切り） |
| `sweep` | `"forward"` | 座標降下の点スイープ方向。`"forward"`=DOF 0→末尾（固定根 c0 側から）/ `"reverse"`（または `"tail"` / `cdReverse:1`）=末尾→0（**可動な尾側から**）。`solver:"cd"` 時のみ有効。下記 |
| `parallel` | `0` | 座標降下の並列化。`0`=直列 Gauss-Seidel（既定）/ `1`（または `"jacobi"`）=**接触グラフ彩色のブロック Jacobi**（非干渉な制御点を同時更新）。`solver:"cd"` 時のみ。下記 |
| `threads` | `0` | スレッド上限。`0`=自動（コア数−2）/ `1`=直列（スレッド不使用）。`parallel:0` でも `threads>1` なら各点の試行評価を並列化（結果は直列と一致） |
| `fixEnds` | 1 | 端点 S,E を固定（配管の取り合いを保つ） |
| `wBend` | 0.1 | 曲げ正則化の重み（大きいほど滑らか） |
| `fixed` | `[]` | 固定する**制御点**の設計点 index（`0=S, 1..m=C, m+1=E`） |
| `pins` | `[]` | **通過点**ピン。各 `{joint, at:[x,y,z], hard}`（中点 Mⱼ=(Cⱼ+Cⱼ₊₁)/2 を `at` へ。`hard`=厳密 / 省略=ソフト） |
| `fZ` | 0 | 外力: z 並行（**負で重力下向き**）。`U=-fZ·z` |
| `fAxis` | 0 | 外力: z 軸へ（正で束ねる）。`U=fAxis·ρ` |
| `fOrigin` | 0 | 外力: 原点へ（正で集める）。`U=fOrigin·r` |
| `separate` | 1 | energy 後段の**射影的分離パス**を行うか（重なりを押し広げる。下記） |
| `sepGain` | 0.25 | 分離の押し離しゲイン |
| `sepIter` | 1000 | 分離反復上限 |
| `sepLambda` | 0.15 | 分離の移動量平滑化（ジグザグ抑制。0=切る） |

> **射影的分離パス**: energy 勾配法は「ほぼ足りた隙間を詰める」のは得意だが「重なりを**押し広げる**」のは
> 苦手（接触の再検出で非平滑になり局所平衡で停止、gap≈0 では接触法線が不安定）。そこで energy の後に、
> 検出した接触を**中心線間の安定な方向**へ**食い込み量 `dMin−gap` だけ**押し離す射影的緩和を回す
> （`segDesignWeights` で制御点へ厳密分配・移動量を平滑化してキンク抑制・固定 DOF は除外）。
> これにより**ピッチ≈2r で隣接ターンが接触するコイル等でも、設定そのままで gap≥dMin の有効メッシュに開く**。
> 既定で ON。energy 法の挙動が良いケースでは違反ゼロなので即 no-op。`separate:0` で切れる。
> 後方互換として位置引数 `pipe_adjust(ctrl, radius, dMin, maxIter, fixEnds, wBend)` も受け付ける。

> **ソルバ選択（`solver`）**: 同じエネルギー `wLen·L + wBend·∫κ² + wPenalty·[dMin−gap]₊²` を最小化するが、
> 探索の進め方が違う。
>
> - `"grad"`（既定）= **勾配降下**。全制御点を一度にフル勾配方向へ動かし、グローバルな1ステップ
>   `α=stepMax/|g|` を line search。滑らかで速いが、**全点同時の歩幅が最大勾配に支配される**ため、
>   深く重なった自己接触（太い管が斜めに交差・密巻きコイル等）では局所平衡に嵌り、`clearViolation`
>   が残ることがある。
> - `"cd"` = **座標降下**（非線形 Gauss-Seidel）。各制御点を**1点ずつ・軸並行に** `cdPitch0→cdPitchMin`
>   と pitch を半減しながら個別に line search する。1点ずつなので**他点の巨大勾配に歩幅を奪われず**、
>   勾配降下が解けない**深い重なりを `gap≥dMin` まで押し切れる**ことが多い（クリアランスを厳密化したい
>   ケースで有効）。代償として**計算は重い**（1 sweep で全点×全軸を試行評価）。
>
> 迷ったら既定の `"grad"` で試し、`clearViolation` が残る／重なりが解けないときに `solver:"cd"` へ。
> なお N 体 `pipe_scene_adjust` も同じ `solver` キーを受ける。
>
> **スイープ方向（`sweep`・cd 専用）**: 座標降下は Gauss-Seidel なので**点を回す順序で収束経路が変わる**。
> 既定 `"forward"` は DOF 0（＝固定根 `c0`）側から末尾へ。固定端の隣＝最も動きにくい側から始まるため、
> 全体が動き出すまでが遅いことがある。`sweep:"reverse"` は**自由な尾側から**回す。コイルを根本固定で
> 引っ張るような構成では、可動端から順に動かす方が**1 sweep あたりの全体移動が大きく**、同じ反復数で
> より低いエネルギー／違反へ届くことがある（固定点指定 `fixed`/`pins` はどちらの向きでもそのまま尊重される。
> 配列を反転させる必要はない）。勾配降下（`solver:"grad"`）は全点同時更新なので順序非依存＝この指定は無視。
>
> **並列化（`parallel`/`threads`・cd 専用）**: cd は各点で「6方向×pitch段」の全エネルギー評価を回すため重い。
> 2 段の並列化を選べる（既定は完全直列で従来と厳密一致）:
 - **`threads>1`（`parallel:0` のまま）= L1**: 各点の **6 試行を並列評価**。点の更新順は直列のままなので**結果は直列と完全一致**（安全な高速化）。ただし並列度は**最大 6**（軸×符号の試行数）で頭打ち。
> - **`parallel:1` = L2（接触グラフ彩色ブロック Jacobi）**: 「隣接でも接触でもない＝非干渉」な制御点を**点×6試行の細粒度で一括並列**更新する（並列度 = 6×活性点・最低 6・6 の倍数）。L1 の 6 倍上限を超えて多コアを使える。
>   **結果は直列とは異なる**（色順で点を回す＝順序依存の別最適化＋長距離の弧長カップリング無視。`sweep` の向き違いと同様に別の最適点に到達する）。deterministic（再現性あり）。
>   弧長が大きく動く初期や厳密一致が要る場面は L1/直列、位置が固まった**微調整段や接触が疎な構成で L2** が効く。彩色は外ループ毎に再計算。
>
> 実測（密巻きコイル 27 点・maxIter 25）: 直列 **351s** → L1 約 5〜6 コア / **L2 50.7s（約6.9倍）**。この例では L2 が energy/clearViol とも直列より低い値に到達（2768/0.0016 vs 2820/0.0052）。スレッド数は `threads` で制御（密コイルの L2 彩色は 7 色 size[6,6,6,2,2,2,1] なので `threads` を増やしても色サイズ×6 が上限）。

### 3.4. `pipe_scene_proximity(bodies, report_gap)` — N 体近接検出

複数配管をまとめて検出する。`bodies` は body の配列、各 body = `{ctrl, radius, movable}`。

```
var bodies = [
  {ctrl: [[0,2,0],[14,2,0],[0,2.2,0]], radius: 0.8, movable: 1},
  {ctrl: [[0,4,0],[14,4,0]],           radius: 0.8, movable: 0}
];
var hits = pipe_scene_proximity(bodies, 8.0);
// hits = [[gap, pA, pB, normal, sA, sB, rA, rB, bodyA, bodyB], ...]
//   単一 op より末尾に bodyA/bodyB(Body 番号) が付く。
//   可動 body の自己接近 + 異 body 間の交差を返す(固定–固定ペアはスキップ)。
```

### 3.5. `pipe_scene_adjust(bodies, movableIdx, params)` — N 体距離調整

`movableIdx` の body を、他の**固定 body 群を障害物**として `gap >= dMin` へ調整する（adjustScene・
可動 1 本モデル）。`params` は `pipe_adjust` と同じハッシュ。`fixed` / `pins` は可動 body の DOF に効く。

```
var res = pipe_scene_adjust(bodies, 0, { dMin: 0.5, maxIter: 400, fixEnds: 1, fZ: -0.3 });
// res = { ctrl, iters, energy, clearViolation, feasible }  (= 可動 body の調整後 ctrl)
```

### 3.6. `pipe_sample(ctrl_pts, radius, pitch)` — 弧長等間隔サンプル(tube 化用)

中心線を**弧長等間隔ピッチ** `pitch`(mm)でサンプルし、各点に半径 `R(s)` を付けて返す。可変太さ管を
`tube` で可視化するときに使う(弧長 s と半径の対応をライブラリの正確な弧長で評価するので、srava 側で
弧長計算も r(s) 評価も不要)。

```
var samples = pipe_sample(ctrl, [[0,1.0],[26,0.35]], 0.6);  // テーパ管・ピッチ 0.6mm
// samples = [[ [x,y,z], r ], ...]   → そのまま tube に渡せる
var solid = tube(samples, 24);
```

- `pitch` 省略/`<=0` で `Smax/64`。点列は**弧長等間隔**(`arcAt` の単調逆引き)。
- **端点(s=0, Smax)と半径キーポイント(`[[s,r]]` の各 s)を強制的に含める** → 管端とテーパの折れがクッキリ出る。
- `radius` は他の op と同じ 3 形態。`pipe_adjust` の戻り `ctrl` をそのまま渡せば調整後の管が描ける。

---

## 4. 例（同梱）

| ファイル | 内容 |
|---|---|
| `examples/pipe_clearance.sra` | 自己接近の検出 + 接近点の球マーカ可視化（検出のみ） |
| `examples/pipe_adjust.sra` | 詰まった折り返しを `pipe_adjust` で開く（調整前/後を 3MF に） |
| `examples/pipe_scene.sra` | 固定障害物配管を避けて可動配管を `pipe_scene_adjust` で調整（N 体） |
| `examples/pipe_taper.sra` | 可変太さ(テーパ)管を調整し `pipe_sample` で per-vertex 半径つき tube 化 |
| `examples/pipe_variable.sra` | 太さが弧長に沿って変わる管(指数フレア / キーポイント紡錘形)を `pipe_sample` で生成 |

可視化は 3MF（面色保持）に出力。定数太さのデモは中心線を srava 側でサンプルして `tube` するが、
**可変太さは `pipe_sample`** を使うと弧長↔半径の対応をライブラリの正確な弧長で評価でき、解析と一致する。
