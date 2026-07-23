# cgal-processor プラグインエージェント規約 v0 (draft)

便利な機能を**ユーザ/サードパーティが本体を再ビルドせずに追加**できる仕組み。
最初の事例は [pipe_proximity](ssh://git@project.globalbase.org/git_repo/proj/gs/pipeProximity.git)。

## 設計の核

- **プラグイン = 独立した実行体（プラグインエージェント）**。`pigwire` プロトコルで planner と
  1 往復する。`srava_agent` と同列だが、値しか扱わないプラグインは **CGAL も cgMesh も知らなくてよい**。
- **プラグイン機構は pig 層の機能**（特定言語 srava ではない）。env・レジストリ・agent ノード・値
  コーデックはすべて pig。だから接頭辞は `PIG_*`、agent ノードは薄い pig 層 `pigfPluginAgent`。
- **依存は一方向**: cgal-processor(host) → プラグイン。アダプタ（プラグイン型 ↔ pig 値）と登録は
  host 側に置き、プラグイン本体は「規約を知らない」まま保つ。

## レイヤ

| 層 | 役割 | このプラグイン機構での担当 |
|---|---|---|
| **pig** (framework) | プロセスエージェント・wire・キャッシュ・ゲート | プラグインレジストリ / `pigfPluginAgent` / 値コーデック / SDK |
| **srava** (言語) | パーサ・プランナ | parser が登録 op 名を `pigfPluginAgent` ノードへ繋ぐ（最小の接続のみ） |
| **plugin** (3rd party) | 便利機能の実体 | 自分の op を提供。host を知らない |

## 1. 登録（マニフェスト）

プラグインは記述子ファイル `*.plugin` を以下のいずれかに置く（探索順）:

1. `$PIG_PLUGIN_PATH`（`:` 区切りの複数ディレクトリ可）
2. `~/.config/srava/plugins/`
3. `$PREFIX/share/srava/plugins/`（`cmake --install` で配置）

形式（最小・行ベース・`#` でコメント・1 ファイル 1 op）:

```
op   = pipe_proximity        # srava プログラムから呼ぶ名前
bin  = /usr/local/bin/pipe_proximity_agent   # 起動する実行体（絶対パス）
out  = value                 # value | mesh （出力種。v0 は value のみ）
args = 1..3                  # arity（検証用・任意。"N" / "N.." / "N..M"）
```

planner は起動時にマニフェスト群を読み、**pig プラグインレジストリ**（プロセス global・
immutable = read-once config なので低リスク）へ登録する。

## 2. 呼び出しの流れ

```
srava ソース:  pipe_proximity(ctrl_pts, radius, gap)
  parser(mk_call): builtin 名でない → pig レジストリ照会 → 登録 op
                   → pigDataFunction<pigfPluginAgent>(op_name, args) を構築
  planner(評価):  pigfPluginAgent が manifest の bin を起動（op/file/line 付き）
  agent(plugin):  pigwire で引数受信 → SDK が値デコード → compute(args) → 結果を値エンコード
  planner:        結果テキストを pig 値パーサで pigData(入れ子配列等)へ復元 → srava が添字アクセス
  cache:          op_name + 引数ハッシュで content-addressed cache（決定的なら初回のみ計算）
```

`pigfPluginAgent`（pig・薄い）は `pigfAgent` を継承し:
- `agent_cmd()` → pig プラグインレジストリで op→bin 解決。
- `make_value_parser()` → **pig 値パーサ**（`pig_value_parse`・srava の言語パーサ不使用）。
- `try_shortcircuit()` を override しない（`{}` 単位元短絡は mesh ブール固有 = srava の話）。

## 3. データ受け渡し（値の wire 形式）

`pigData::serialize()`（pig）の出力サブセット。エンコード/デコードとも pig 層で完結
（[`pigValueCodec`](../src/h/pig/c++/pigValueCodec.h)）:

| 型 | 形式 | 例 |
|---|---|---|
| null | `null` | |
| 整数 | `-?[0-9]+` | `42` |
| 小数 | 小数点/指数つき | `3.14`, `1.5e10` |
| 文字列 | `"..."`（`\\ \" \n \t` エスケープ） | `"red"` |
| 配列 | `[v,v,...]` | `[[0,0,0],[2,3,0]]` |
| ハッシュ | `{"k":v,...}`（キーソート） | `{"n":5,"name":"part"}` |

srava 言語の式・変数参照・厳密有理数は**扱わない**（それは srava の仕事）。プラグインは
「数・配列・文字列」だけで自己完結する。

## 4. プラグイン SDK（host 提供・作者が使う）

- `pigwire.h`（ヘッダオンリー・依存ゼロ）＋ pig 値コーデック ＋ `serve(compute)` ヘルパ。
- プラグイン作者は **`compute(args) -> result` だけ**書く（args/result は数・配列・文字列の単純型）。
  tinyState も CGAL も pig 状態機械もリンク不要。
- 各呼び出しは自己完結（グローバル状態なし）。複数呼び出し/スレッドで独立。

## 5. 参照プラグイン: pipe_proximity

- `pipe_proximity_agent` = SDK ＋ アダプタ（配列 ↔ `ChainDesign` / `Contact` ↔ 配列）＋ `pipeprox`(MIT) リンク。
- アダプタ（host 側 = この repo）が「点配列 → `ChainDesign`」「半径パラメータ → `RadiusFn`」
  「`Contact` → 配列レコード」を変換。pipe_proximity 本体は無垢のまま。
- 例（srava）:
  ```
  var pts = [[0,0,0],[2,3,0],[5,4,0.3],[8,2,0.6],[10,0,1]];
  var hits = pipe_proximity(pts, [0.4, 0.015], 0.5);   // ctrl点列, [r0, m], reportGap
  // hits = [[gap, [pax,pay,paz], [pbx,pby,pbz], [nx,ny,nz], sA, sB, rA, rB], ...]
  ```
- **半径ラムダ（`RadiusFn`）**: アダプタが `radius` 引数を弧長 `s` → 半径のラムダ
  （`std::function<optional<double>(double)>`）へ変換する。`nullopt` = 「そこにパイプは無い」= 管端。
  引数の**形**で 3 種を自動判別する（srava の flat=ベクトル / nest=コンテナ慣習に沿う）:

  | `radius` 引数 | プロファイル |
  |---|---|
  | スカラ `r` | 一定半径 `r` |
  | フラット `[r0, m]` | 指数 `r(s) = r0·exp(m·s)`（`m>0` 太る / `m<0` テーパ） |
  | ネスト `[[s,r],…]` | 弧長キーポイントの**線形補間**（s 昇順に整列・端の外側は端値クランプ） |

  - 検出（`pipe_proximity`）はチェーンが静的なので `s<0 || s>Smax` で `nullopt`（両端をキャップ）。
  - 調整（`pipe_adjust`）は**最適化中に弧長が変わる**ので上限 `Smax` で切らない（`s<0` のみ `nullopt`）。
    固定 `Smax` で切ると伸びた先が管端と誤認される（ライブラリの `adjust` テストと同じ作法）。
  - 生成は両 op 共用の `makeRadiusFn(r0, m, sr, gateUpper, Smax)`（アダプタ TU）。`sr` 空=指数 / 非空=線形補間。

### 5.1. 追加 op（同一 bin が `compute(op,…)` の分岐で serve）

同一 `pipe_proximity_agent` バイナリが 5 op を serve する（規約 = 1 ファイル 1 op なので**マニフェストは
op ごとに別ファイル**・bin は同じ）。利用者向けの詳細は公開ドキュメント `srava_plugin_reference.md`。

| op | ライブラリ | 体数 |
|---|---|---|
| `pipe_proximity` | `findSelfProximities` | 単一（自己接近） |
| `pipe_adjust` | `adjust` | 単一 |
| `pipe_scene_proximity` | `findSceneProximities` | N 体（自己 + 異 body 交差・`bodyA/bodyB` 付き） |
| `pipe_scene_adjust` | `adjustScene` | N 体（可動 1 本 `movableIdx`・他は固定障害物） |
| `pipe_sample` | `Chain::arcAt`(逆引き)+`RadiusFn` | — 弧長等間隔で `[pos,r]` を返す(tube 化) |

**`pipe_sample(ctrl, radius, pitch)`**: 中心線を弧長等間隔ピッチ `pitch`(<=0 で Smax/64)でサンプルし
`[[ [x,y,z], r ], ...]` を返す。`s→(seg,t)` は `segStartS` バケット + `arcAt` 単調の二分法で逆引き
(ライブラリの正確な弧長)。端点(0,Smax)と半径キーポイント(`[[s,r]]` の s)を強制併合 → tube に直結すると
可変太さ管が解析と一致。可変半径の管化で「弧長↔半径」を srava 側に二重実装せずに済む。

**params ハッシュ**（`pipe_adjust` / `pipe_scene_adjust` 共通・`parse_params` でデコード）:

```
{ dMin, maxIter, fixEnds, wBend,            // 基本
  fixed: [int,...],                         // 固定する設計点 index → cp.fixedDOF
  pins:  [{joint, at:[x,y,z], hard}, ...],  // 通過点ピン → cp.pins(hard=零空間射影 / soft=ペナルティ)
  fZ, fAxis, fOrigin,                       // 外力 → cp.fZ/fAxis/fOrigin
  separate, sepGain, sepIter, sepLambda }   // 射影的分離パス(下記)
```

**射影的分離パス**(`separateScene`・既定 ON): energy 法(`adjust`/`adjustScene`)は重なりの**押し広げ**が苦手
(接触再検出で energy が非平滑 → line search 即停止、gap≈0 で接触法線 `(pA−pB)/gap` が不安定)。そこで
energy の後段に、検出接触を**中心線点 X,Y の差 `(X−Y)/|X−Y|`(gap≈0 でも安定)** 方向へ `dMin−gap` だけ
押し離す射影的緩和を回す。接触点 B(seg,t) の移動は `segDesignWeights`×Bernstein 基底で設計点へ最小ノルム
分配。**移動量(増分)を制御点列で平滑化**(`sepLambda`)してジグザグ(局所自己交差の元)を抑える(位置でなく
増分を平滑化するので曲線が縮まずオーバーシュートしない)。固定 DOF は除外。違反が無ければ即 no-op。
→ ピッチ≈2r のコイル等、energy では取れない重なりを設定そのままで gap≥dMin の有効メッシュに開く。
ctest `srava_plugin_separate`。

```
var res = pipe_adjust(pts, 0.8, {dMin:0.6, maxIter:400, fixEnds:1, pins:[{joint:1,at:[11,2,0],hard:1}], fZ:-0.3});
// → {"ctrl":[[x,y,z],...], "iters", "energy", "clearViolation", "feasible"}
var rs  = pipe_scene_adjust(bodies, 0, {dMin:0.5});   // bodies=[{ctrl,radius,movable},...]
```

実装の要点:
- 既定: `dMin=0.5, maxIter=200, fixEnds=真, wBend=0.1`、外力 0、`fixed/pins` 空。
- `pipe_adjust` は**後方互換**として位置引数 `(ctrl, radius, dMin, maxIter, fixEnds, wBend)` も受ける
  （第 3 引数がハッシュか数値かで分岐）。
- Scene の半径ゲート（[半径ラムダ](#参照プラグイン-pipe_proximity)参照）は **body ごと**: 可動 body は
  `gateUpper=false`、固定 body は各自 `Smax` で `gateUpper=true`。可動 body は自動的に `movable=true` 扱い。
- 勾配降下なので、初期形が「クリアランス違反だが非フック」のとき素直に開く（自己交差レベルのフックは
  局所最適に落ちうる）。
- デモ: `pipe_clearance.sra`（検出+可視化）・`pipe_adjust.sra`（単一調整）・`pipe_scene.sra`（N 体）。

## 5.5. ビルド & インストール（pipe_proximity を一緒に使う）

```sh
git pull
cmake -S . -B build -DSRAVA_PLUGIN_PIPEPROX=ON   # プラグインを opt-in(既定 OFF)
cmake --build build -j
sudo cmake --install build                        # srava/agent + プラグイン一式を $PREFIX へ
```

- `-DSRAVA_PLUGIN_PIPEPROX=ON` で FetchContent が pipe_proximity v0.1.1 を自動取得しビルド。
  **省略時(既定 OFF)は一切 fetch しない**ので core はネット非依存。
- `cmake --install` が配置するもの:
  - エージェント → `$PREFIX/libexec/srava/plugins/pipe_proximity_agent`
  - マニフェスト → `$PREFIX/share/srava/plugins/pipe_proximity.plugin`（**installed bin の絶対パスを
    指して自動生成**。`$PREFIX/share/srava/plugins` は registry の既定 sysdir）
- → install 後は**環境変数なしで** `pipe_proximity(...)` が使える。
- 開発中（install せず）に使うなら、マニフェストを作って `PIG_PLUGIN_PATH` で指す:
  ```sh
  printf 'op=pipe_proximity\nbin=%s\nout=value\n' "$PWD/build/pipe_proximity_agent" > /tmp/pp/pp.plugin
  PIG_PLUGIN_PATH=/tmp/pp srava my.sra
  ```

## 6. キャッシュ / 版ゲート

- プラグイン op も content-addressed cache に乗る（決定的なら自動）。
- 版ゲートの指紋にプラグインバイナリの size/mtime も混ぜる（プラグイン再ビルドで該当キャッシュ無効化）。

## 実装ステータス

- [x] pig 値コーデック（`pigValueCodec` / 往復テスト `pigvaluecodec`）
- [x] プラグイン SDK（`pigPluginSDK` / pigwire serve・CGAL 非依存。`pigwire.h` のハンドシェイク
      = 起動直後に双方が streamhdr 交換 → C_OP/C_ARG/C_ARG_END → A_SAVE_BEGIN/DONE/BYE/W_END）
- [x] pig プラグインレジストリ（`pigPluginRegistry` / マニフェスト読取・op→bin）
- [x] `pigfPluginAgent`（薄い pig agent・agent_cmd→bin / parse_value_text→pig_value_parse）
      + parser フック（`mk_call` で登録 op を委ね）+ planner INI で `registry_load`
- [x] echo プラグイン（`plugins/echo/`）で end-to-end 疎通 + ctest `srava_plugin_echo`
      （別プロセス起動・値往復・キャッシュ HIT も確認）
- [x] `pipe_proximity_agent`（`plugins/pipe_proximity/`）: FetchContent v0.1.1 + アダプタ
      + 統合テスト `srava_plugin_pipeprox`（U 字パイプの自己接近検出 end-to-end）。
      `-DSRAVA_PLUGIN_PIPEPROX=ON` で opt-in（OFF 既定＝core は network 非依存）。

## 参照プラグイン pipe_proximity の実装メモ

- **TU 分離**（namespace 衝突回避）: pipe_proximity は `namespace pipe` を持ち、POSIX `::pipe()`
  （SDK が unistd 経由で引く）とグローバルスコープで衝突する。そこで:
  - `pipe_proximity_adapter.cpp` — pipe ヘッダ（`namespace pipe`）を include。SDK/pigData は触らない。
  - `pipe_proximity_agent.cpp` — SDK（pigData/`::pipe`）を include。pipe ヘッダは触らない。
  - 境界は plain 型 `pipe_proximity_adapter.h`（pipe も pigData も登場しない）。
- **上流へ戻す事項**（fork せず Issue で報告）:
  1. `include/pipe/bvh.hpp` が `std::max({...})` を `<algorithm>` 無しで使う（新しい libstdc++ で
     コンパイル不能）。host 側で `-include algorithm` を pipeprox の TU に注入して回避中。
  2. `namespace pipe` が POSIX `::pipe()` と衝突しやすい（namespace 名の再考 or 含意の文書化）。
- 呼び出し例:
  ```
  var pts = [[0,0,0],[10,0.5,0],[10,3,0],[0,3.5,0]];   // 折り返すパイプ
  var hits = pipe_proximity(pts, [1.0, 0.0], 4.0);     // 半径 r(s)=1.0, reportGap=4.0
  // hits[i] = [gap, [pax,pay,paz], [pbx,pby,pbz], [nx,ny,nz], sA, sB, rA, rB]
  ```
