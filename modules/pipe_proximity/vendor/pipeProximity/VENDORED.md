# pipeProximity — srava 側で管理する

pipe/ 系の幾何コアライブラリ (MIT・GLOBALBASE UMUT / Hirohisa Mori)。

**この木が正本。上流追随はしない** (2026-08-12 方針決定)。別 repo を上流として維持しても結局
同じ担当 (あきら×ひさ) が二重に面倒を見るだけなので、srava の一部として管理する。

- 改修はこのディレクトリで直接行う。上流へ還元する義務はない。
- `git pull` 相当の同期作業は無い。下の「由来」は履歴の記録であって、追随先ではない。
- ファイル名 `VENDORED.md` は git 履歴を切らない為に据置。中身は上記の通り「srava 管理」。

## 由来 (履歴・追随先ではない)

- 元 repo: ssh://git@project.globalbase.org/git_repo/proj/gs/pipeProximity.git
- 分岐点: tag v0.1.6 / commit 3802313d18d323e4ea07b2ae2334ce4ea7c0713c
- 取り込み日: 2026-08-12
- 後追い取り込み (一度だけ・2026-08-12): 上流 v0.1.7 の `44c36e3`
  「feat(controller): 制御点間隔の均一化項 wSpace を追加」を `include/pipe/controller.hpp` /
  `src/controller.cpp` へ取り込んだ (純追加 18 行)。分岐前に上流にあった改良の回収であって、
  上流追随を再開したわけではない。**以後の同期は無い**。
- 取り込んだもの: include/pipe/*.hpp, src/*.cpp, tests/test_pipe.cpp, LICENSE,
  README.md, INTEGRATION.md (apps/ と doc/ は除外)
  - README.md が参照する `doc/pipe_proximity.pdf` (数学的背景 17 ページ) はここには無い。
    元 repo 側にある。
  - INTEGRATION.md は cgal-processor へ組み込んだ当時の手引きで、srava の現構成
    (modules/pipe_proximity) とは対応しない。歴史資料として残置。

## ライセンス

MIT のまま。`LICENSE` と著作権表示は削除しないこと (srava 本体が別ライセンスでも MIT は互換)。

## ビルド・テスト

FetchContent は廃止済み。ルート `CMakeLists.txt` の `pipeprox` ターゲットがこの木のソースを
直接コンパイルする (`PIPEPROX_DIR`)。外部依存は C++17 + Threads のみ。

- `pipeprox` (STATIC・PIC) → `pipe_proximity_module` (.so) がリンク
- `tests/test_pipe.cpp` は ctest `srava_pipeprox_vendored_test` として常設

## 改修時に守る制約

- **`namespace pipe` と POSIX `pipe()` の衝突**: `<thread>` は `<unistd.h>` を引き込み、
  グローバル `pipe()` が `namespace pipe` と衝突する。そのため並列プリミティブの実装は
  `namespace pipe` を開かない別 TU (`src/cd_parallel.cpp`) に隔離してある。この分離を崩さない。
- **API 境界**: `include/pipe/*.hpp` は host 側アダプタ (`../../pipe_proximity_adapter.*`) が
  直接使う。シグネチャを変えるときは両側を同時に直す。
- **決定性**: 座標降下の並列化は「結果が直列と一致する」ことを前提に書かれている
  (試行結果を添字ごとに書き分け、最良選択は直列)。並列機構を変えてもこの性質を壊さない。

## 既知の課題

- `src/cd_parallel.cpp` の `parallel_for` が呼び出しごとに `std::thread` を生成して join する
  (spawn-per-call・プール無し)。座標降下のホットループから呼ばれるため実測で
  **毎秒 1,500〜7,300 本**のスレッドが生成・破棄される (dof=6 で 7,251 本/秒、dof=12 で 1,570 本/秒)。
  バリアコストは常駐プール比で 81.5µs/call 対 16.9µs/call。in-proc (既定 EXEC_THREAD) では
  planner プロセス内で churn するため、可観測性 (agentwatch の tid 基準計測を実際に壊した=`753bf8b`)・
  他プロセスとの mm ロック競合・中断不能性の点で良くない。常駐プール化が課題。
  なお内製化したので修正はこの木で行う。
