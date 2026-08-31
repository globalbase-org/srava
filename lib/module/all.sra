// module/all.sra — 実カーネル一式を明示ロードする便宜スクリプト (#3452)。
//
// #3452 で module() が「未ロードなら読み込む」唯一の入口になった (起動時の全モジュール
// eager-load は廃止)。個々の測定・単発実験では module("manifold.so",{}) のように
// 必要なものだけを明示する方が、他モジュールのロードコストが乗らず速い。
//
// このファイルは「とりあえず全部使いたい」場合の便宜品。include "module/all.sra"; と書けば
// 旧来 (#3452 以前) の「起動時に全カーネルが使える」挙動に近い状態になる。
//
// ★ nef は nef_snc.so / nef_hybrid.so の 2 変種があり、同一プロセスに両方読み込める設計だが
//   通常用途では衝突を避けるため nef_hybrid.so のみをここに含める。nef_snc.so が必要な場合は
//   呼び出し側で個別に module("nef_snc.so", {}); を追加すること。
//
// openvdb と cgal/geogram/manifold を跨ぐ橋渡しモジュール (openvdb_cg.so/openvdb_gg.so/
// openvdb_mf.so) はここには含めない (openvdb_cg は CGAL を巻き込むため GPL になる・
// 用途が voxelize/isosurface の橋渡しに限定される特殊モジュールのため個別 opt-in が妥当)。
// pipe_proximity.so (サードパーティ・自己接近検出プラグイン) も同様に個別 opt-in。
//
// ★ optional:1 — ビルド構成によっては 6 本全部が揃っているとは限らない
//   (例: 依存ライブラリ未導入で SRAVA_MODULE_GEOGRAM=OFF 等)。無いものは黙ってスキップし、
//   在るものだけ使う。「6 本揃っていないと即エラー」は便宜スクリプトとして厳しすぎる。

module("cgal.so",       {optional:1});
module("geogram.so",    {optional:1});
module("manifold.so",   {optional:1});
module("occt.so",       {optional:1});
module("openvdb.so",    {optional:1});
module("nef_hybrid.so", {optional:1});
