# srava `async` / `sync:` 設計メモ

非同期・並列の言語プリミティブを **`async` 文 + `sync:` ラベル** の 1 組に統一する設計。
`print_async` / `export_async` / `par` を個別の組込から退役させ、共通の下回り
(発行順チェーン + drain + エラー集積)へ畳む。

- 起票: 設計会話 2026-06-30(Akira ↔ ひさ)
- 関連: `pigfPrintAsync`(発行順チェーン)・`pigcgOperatorExportAsync`/`asyncExports`(drain)・
  `pigDataOperatorPar`・`pigfGate`・`pigfSequence`

---

## 1. 背景と動機

### 問題

1. **`xxx_async` 系が際限なく増える**。`print_async`・`export_async` と来て、機能ごとに
   非同期版を生やすのは設計として筋が悪い。
2. **`export_async` の完了をキャッチして表示する手段が弱い**。`gate` を噛ませても

   ```
   export_async("a.stl", gate(box(1,1,1), print(1)));
   ```

   の `print(1)` は **box の計算完了直後**であって **a.stl の書き込み完了**ではない。
   また複数 `export_async` 間で表示順が保証されない(box の計算速度依存)。

3. 一方で素の直列版

   ```
   export("a.stl", box(1,1,1)); print("Saved a.stl");
   ```

   は「Saved」表示時に保存完了が保証され順序も確定するが、`box(1,1,1)` と
   `box(2,2,2)` が **並列に走らない**。

### 解きたいこと

**並列に走る部分**と**直列化・順序保証される部分**を、1 つの構文の中で明示的に書き分ける。

---

## 2. 言語仕様

### 2.1 実行モデルの約束(全体の土台)

| 種別 | 実行 |
|---|---|
| 制御文(sequence `{}` / `if` / `while` / `for`) | **直列**(従来どおり。順に compact) |
| 関数呼び出しの引数 | **並列**(全 trigger → compact) |
| 並列な制御 | **`async` 文** |

これに伴い:

- `print(volume, bbox)` の `volume` / `bbox` は **並列評価**(従来は観測で直列化していた)。
- **`par` は撤去**。`par(a,b,c) ≡ [a,b,c]`。配列リテラル `[..]` を「全要素を並列起動して
  から値配列にする」(= 現 `par` と同じ trigger-all-first)に変更すれば `par` は不要。

### 2.2 `async` 文

```
async { STATEMENT...
        sync: STATEMENT }      // sync: は省略可・必ずブロックの最終文・1 つだけ
async { STATEMENT... }          // sync: 無し
```

- ブロックは **1 スコープ共有**(sequence と同じ)。body で `var` した変数は `sync:` 文から見える。
- ブロック内部は **直列実行**(sequence と同じく順に compact)。
- 複数の `async` ブロックは **互いに並列**に走る。
- `sync:` 文の **実行(=副作用・出力)だけ**が、全 `async` 文を跨いで **ソース出現順**に整列する。
  body の重い計算は並列のまま。

### 2.3 例

```
async {
    export("a.stl", box(1,1,1));
    print("Saved a.stl");          // sync: 無し
}
async {
    export("b.stl", box(2,2,2));
    print("Saved b.stl");
}
```
→ 各ブロック内は `export`→`print` の直列(Saved 表示時に保存完了は保証)。
   ただし a/b どちらの Saved が先に出るかは export 速度依存。

```
async {
    export("a.stl", box(1,1,1));
    sync: print("Saved a.stl");
}
async {
    export("b.stl", box(2,2,2));
    sync: print("Saved b.stl");
}
```
→ export は並列。`Saved a.stl` → `Saved b.stl` の順で必ず表示。
   しかも `sync:` 文は body の後なので、表示時に当該 .stl は **本当に保存完了**している
   (`export_async` + `gate` の「計算完了 ≠ 書込完了」問題が解消)。

並列計算した値を順番に表示したい場合:

```
async { var v = volume; sync: print(v); }
async { var b = bbox;   sync: print(b); }
```
→ `volume` / `bbox` は body で並列計算、表示だけ順序付け。

---

## 3. 意味論(詳細)

### 3.1 `sync:` の正確な意味 — 「順序付き出力」であって「順序付き実行」ではない

`sync:` 文 S は次の 2 条件が揃って初めて実行される:

1. **自分の body が完了**している(S はブロック最終文なので自動的に成立)。
2. **直前の `async` 文の `sync:` が実行済み**(発行順チェーン)。

body の重い計算は (1)(2) と無関係に並列で進む。整列されるのは S の **副作用(出力)** だけ。

### 3.2 drain とエラー集積(`export_async` 互換の continue-and-collect)

- 各 `async` 文は **非ブロッキング**に起動され、planner の drain に登録される。
- プログラム末尾(全 agent 完了後)で drain がチェーン末尾を待ち、全 body + 全 `sync:` の
  完了を保証する。
- body / `sync:` でエラーが出ても **中断しない**。エラーは drain のエラーリストに積み、
  **末尾でまとめて表示**(終了コードに反映)。これは現 `asyncExports` のエラー集積と同じ。

### 3.3 エラー時もチェーンは前進する(デッドロック回避)

`async#2` の `sync:` が待つのは **`async#1` の結果が settle すること**(値でもエラーでも)で
あって「`async#1` がどの文に到達したか」ではない。future は値 or エラーのどちらでも settle
するので、`async#1` が body のどこで失敗しても `async#2` は即座に解放され、全体は終了できる。

**実装上の唯一の不変条件**: `async` の完了シグナル(チェーンのトークン)を、
**正常・例外・teardown のどの経路でも必ず settle(release)する**。
(参照: `tinystate_teardown_deadlock` — refCond を signal し損ねて両者永眠した前例。)

**生存条件**: 各 `async` の body が無限ループしないこと(これはユーザ側のバグ)。

---

## 4. 実装設計(lowering)

### 4.1 下回りの統一

現状ある 2 つの planner 機構を一般化して 1 本化する:

| 現 | 一般化後 |
|---|---|
| `printTail`(print_async の発行順チェーン末尾 front) | `syncTail`(全 `async` の `sync:` チェーン末尾) |
| `asyncExports`(export_async の未完了 promise 配列)+ `asyncExportErrors` | drain(全 `async` 完了待ち)+ 累積エラー数 |
| `drain_async_prints()` + `drain_async_exports()` | `drain_async()`(syncTail を compact + エラー報告) |

`syncTail` を「**すべての** `async` 文(sync 有無を問わず)」のチェーンにすれば、末尾で
`syncTail` を compact するだけで **全 body + 全 sync の完了**が保証され、drain も 1 本化できる。

### 4.2 新ヘルパ `pigfAsync`(tinyState)

`pigfPrintAsync` を「body 付き・任意文の sync」へ一般化したもの。**env を自前で 1 つ作り、
body 文と sync 文を同じ env で評価する**(スコープ共有の肝)。

```
args = [ prev, stmt0, stmt1, ..., stmtK ]
       prev      … 直前 async の done 信号(syncTail。初回は解決済み null)
       stmt0..   … body 文(hasSync なら最後の stmtK が sync 文)
```

状態機械:

```
INI       : env = child env(pigfSequence と同じ)
ACT_BODY  : body 文を順に compact(env 内・直列)。
            ・is_error なら err を捕捉し以降の body をスキップ → ACT_WAITPREV
            ・hasSync の場合、最終 sync 文は ACT_BODY では評価しない(取り置く)
ACT_WAITPREV : prev を compact(直前 async の sync 実行完了を待つ。yield→再走)→ ACT_SYNC
ACT_SYNC  : !err かつ hasSync のとき sync 文を env 内で compact(出現順に出力)→ ACT_DONE
ACT_DONE  : front->set_result(null)(= 次 async の prev 待ちを起こす)
            err があれば planner の drain エラーリストへ deposit → FIN
```

- body は `async` 文の起動と同時に走り出す(各 `async` ヘルパは別コルーチンとして
  非ブロック trigger される)→ **body 間は並列**。
- `prev` 待ちは body 完了の **後**。`sync:` 文だけが整列される。
- どの経路でも `ACT_DONE` で `set_result` するので **チェーンは必ず前進**(§3.3)。

### 4.3 `async` 演算子 `pigcgOperatorAsync` の `_start`(ソース順で発火)

トップレベルのプログラムは `pigfSequence` で **直列評価**されるため、各 `async` 文の `_start` は
**ソース順**に走る(`print_async` が `printTail` を読むのと同じ仕掛け)。

```c++
void pigcgOperatorAsync::_start() {
  pl = caller_planner();
  prev = pl->sync_tail();                         // 直前 async の done 信号
  f = thNEW(pigDataFunction<pigfAsync>,());
  f->pushArg(prev);
  for (each body stmt)  f->pushArg(stmt);          // hasSync なら sync 文も最後に積む
  f->set_has_sync(hasSync);
  f->set_info(...);
  f->trigger();                                    // 非ブロック起動(body が並列に走り出す)
  pl->register_async(f);                           // drain 登録(エラー集積の対象)
  pl->set_sync_tail(f);                            // 次の prev = この front
  result = null;
}
```

`pigcgOperatorAsync` は body 文配列 + sync 文 + hasSync フラグを保持する。

### 4.4 drain(プログラム末尾)

```c++
void cgptsPlanner_::drain_async() {
  if (syncTail != null) { syncTail->trigger(); (void) syncTail->compact(); }
  // ↑ チェーン末尾を待つ = 全 body + 全 sync 完了。各ヘルパが ACT_DONE で
  //   エラーを deposit 済みなので、この後 asyncErrors を見て終了コード判定。
}
```

`flush()` は「途中バリア」として残す: syncTail を compact してチェーンを掃き出し、
`syncTail` を解決済み null にリセット(以降の `async` は新チェーン)。

---

## 5. 既存機能の移行

| 旧 | 新 |
|---|---|
| `export_async("a", m)` | `async { export("a", m); }`(sync 無し)に desugar、または直接 async へ |
| `print_async(a, b, ...)` | **唯一のシュガー**として残す。下記 desugar |
| `par(a, b, c)` | `[a, b, c]`(配列リテラルを並列化)。`par` 撤去 |
| `flush()` | 残す(syncTail バリア) |
| `gate(inp1, inp2)` | 残す(async とは別の完了フック。一般用途) |

### 5.1 `print_async` の desugar(可変長対応・並列性を保つ)

**素朴な直列 `var` 展開は禁物**。`var _t1=a; var _t2=b;` は sequence の直列 compact なので
a→b が直列に force され、`print_async(volume, bbox)` の並列性が死ぬ。**並列プリミティブ
である配列リテラル 1 文**に hoist する:

```
print_async(a, b, c);
  ⇩ desugar
async { var _t = [a, b, c];               // [..] が a,b,c を並列評価して 1 束縛に
        sync: print(_t[0], _t[1], _t[2]); }
```

`print_async` は「`_async` 一族の始まり」ではなく **唯一祝福された 1 個のシュガー**として残す
(順序付き診断出力は実際 9 割のケースで、毎回 `async { var _t=…; sync: … }` は儀式が重いため)。

---

## 6. 文法・字句の変更

### 6.1 トークン

- `async` → `ASYNC`、`sync` → `SYNC` を追加(キーワード化)。
- ⚠️ `sync` を変数名に使っている既存コードがあると壊れる。grep で確認(無ければキーワード化で可)。
  ソフトキーワード化は lemon では面倒なので、まずは予約語にする。

### 6.2 文法規則(`ns_sravaParser.y`)

```
stmt(A) ::= ASYNC LBRACE stmt_list(L) RBRACE.
        { A = mk_async(L, thNULL); }                       // sync 無し
stmt(A) ::= ASYNC LBRACE stmt_list(L) SYNC COLON stmt(S) RBRACE.
        { A = mk_async(L, S); }                            // sync 付き(必ず末尾 1 つ)
```

`SYNC` で始まる stmt 規則は他に無いので、`stmt_list` 収集後に `SYNC COLON stmt` へ確実に分岐する。
`mk_async(stmts, syncStmt)` が `pigcgOperatorAsync` を組む(body 文配列 + sync 文 + hasSync)。

---

## 7. 段階的実装計画 — **実装済み(2026-06-30)**

ブランチ `feat/async-sync-statement`。各段でビルド + ctest、全段グリーン(180/180)。

1. ✅ **`pigfAsync` ヘルパ**(tinyState)。env 自前生成 + body 直列 + prev 待ち + sync + 全経路 set_result。
2. ✅ **`pigcgOperatorAsync`** + planner `syncTail`(発行順チェーン)/`asyncList`(drain)/`register_async`/
   `flush_async`/`drain_async`/`async_error_total`。CLEANUP で drain・終了コード反映。
3. ✅ **文法**: `ASYNC`/`SYNC` トークン(lexer)+ `async{…}` / `async{… sync: S}` 規則 + `mk_async`。lemon 再生成。
4. ✅ **配列リテラルの並列化**: `pigDataOperatorArray::_start` を trigger-all-first に。`par ≡ [..]`。
5. ✅ **`print_async` を §5.1 の desugar に切替**(grammar で `async { var __pa=[..]; sync: print(__pa[..]); }`)。
6. ✅ **`export_async` を async へ吸収**(grammar で `async { export(...); }`。`flush()` は `flush_async` バリアへ)。
7. ✅ **`par` 撤去**(grammar 分岐削除・`pigDataOperatorPar` 削除)。
8. ✅ **退役した機構を削除**: `pigfPrintAsync`(ファイル+CMake)・`pigcgOperatorPrintAsync`/`pigcgOperatorExportAsync`・
   planner `printTail`/`asyncExports`/`print_tail`/`drain_async_prints`/`flush_async_exports`/
   `drain_async_exports`/`register_async_export`/`async_wait_target`。
9. ✅ ドキュメント(`srava_language_reference.md` / `srava_function_reference.md` / 本メモ)更新。
10. ✅ テスト追加: `srava_async`(順序+スコープ)・`srava_async_err`(continue-and-collect)。
   既存 `srava_asyncexport`(export_async+flush)は desugar 後もグリーン。

### 既知の注意点

- **teardown フレーク(別件)**: 高並列 `-j` ctest で稀に無関係なテストが SEGFAULT(毎回違うテスト・再実行で
  必ず pass)。`[..]` 並列化で array 系テストの同時ワーカが増え露出頻度がやや上昇。原因は tinyState の
  teardown 競合([[tinystate_teardown_deadlock]] の残り)で async の正当性とは無関係。要別途対応。
- **async の env 捕捉**: `async` は起動時の囲み env を継承する。ネスト(関数内など)で使う場合、本流が先に
  抜けても drain が front を保持するので env は生存するが、寿命の前提は v1 では「トップレベル/プログラム
  生存中」を想定。
