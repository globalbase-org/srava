// module/demo.sra — デモ／テスト専用モジュールをまとめてロードする (2026-09-01・ひさ指示)。
//
// all.sra から分けた理由: **通常用途では要らない**が、外したままだと「どこで読めるのか」が
// 分からなくなるため、置き場所を 1 つ決めておく。
//
//   include "module/demo.sra";
//
// ⚠ nef_snc.so は **ここにも入れない** (ひさ指示)。デモ／テスト用ではなく実カーネルの変種で、
//   既定でビルドもされないため、使うときは -DSRAVA_MODULE_NEF_SNC=ON でビルドしたうえで
//   module("nef_snc.so", {}); と明示的に書く。
//
// ★ demo / d2 / d3 / d4 / d5 — rev4 の「次元分担」デモとテスト専用モジュール。
//   priority は負値 (demo -1 / d3 -2 / d2 -3 / d4 -4 / d5 -5) なので、
//   ロードしても既定カーネルを奪わない。
//   ⚠ **install されない** (ビルドツリーにしか存在しない)。install 済みの srava から
//     このファイルを include しても、5 本とも optional:1 で黙ってスキップされる。
//
// ★ optional:1 — 上記のとおり「在るとは限らない」ものばかりなので全部 optional。

module("demo.so",    {optional:1});
module("d2.so",      {optional:1});
module("d3.so",      {optional:1});
module("d4.so",      {optional:1});
module("d5.so",      {optional:1});
