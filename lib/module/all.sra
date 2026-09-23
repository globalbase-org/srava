// module/all.sra — 同梱モジュール一式を明示ロードする便宜スクリプト (#3452)。
//
// #3452 で module() が「未ロードなら読み込む」唯一の入口になった (起動時の全モジュール
// eager-load は廃止)。個々の測定・単発実験では module("manifold.so",{}) のように
// 必要なものだけを明示する方が、他モジュールのロードコストが乗らず速い。
//
// このファイルは「とりあえず全部使いたい」場合の便宜品。include "module/all.sra"; と書けば
// 旧来 (#3452 以前) の「起動時に全カーネルが使える」挙動に近い状態になる。
//
// ★ 収録範囲 (2026-09-01・ひさ指示 / 2026-09-07 に橋 2 本を追加): モジュールリファレンス
//   掲載の全 22 本のうち、**nef_snc.so と デモ／テスト (demo/d2/d3/d4/d5) を除く 16 本**。
//   デモ／テストの 5 本は module/demo.sra に分けてある (include "module/demo.sra"; で読める)。
//   nef_snc.so はどちらにも入れない — **実カーネルの変種**だから。nef_hybrid.so と同じ op を
//   出すので、両方読ませると「どちらが答えるか」が priority 任せになる。
//   ⚠ 2026-09-19 訂正: ここは以前「**同じ型名**で出すので」と書いていたが、**型名は違う**
//     (nef_snc = nf-mesh3d / nef_hybrid = nfb-mesh3d ・ srava_module_probe で実測)。
//     CMakeLists 側も「型名は変種ごとに違う = 同居できる根拠」と書いており、そちらが正しい。
//     除外の理由は型名の衝突ではなく **同じ op を出す変種が 2 つ居ること** の一点。
//   ⇒ 使うときは module("nef_snc.so", {}); と明示的に書く。
//   ⚠ 2026-09-16 に **ビルドの既定は ON へ戻った** (SRAVA_MODULE_NEF_SNC)。以前は
//     「既定でビルドされないから入れない」とも書いていたが、除外の理由は **変種であること**
//     の一点で、ビルド既定とは無関係。⇒ 既定が ON になっても all.sra は変わらない。
//
//   ⚠ 以前は 6 本しか並べておらず、cherchi / occt_mf / openvdb_* の橋渡し / pipe_proximity が
//     漏れていた (橋渡しは「特殊モジュールなので個別 opt-in」と書いてあったが、GPL を理由に
//     openvdb_cg を外す説明は、この一覧に既に cgal.so (GPL) が入っている以上、成り立っていなかった)。
//
// ★ optional:1 — ビルド構成によっては全部が揃っているとは限らない
//   (例: 依存ライブラリ未導入で SRAVA_MODULE_GEOGRAM=OFF / Cygwin で TBB 系が自動 OFF 等)。
//   無いものは黙ってスキップし、在るものだけ使う。
//   「全部揃っていないと即エラー」は便宜スクリプトとして厳しすぎる。
//
// ⚠ ロードは無料ではない。モジュール本数は srava の起動固定費に比例して効くので、
//   使う .so が決まっているなら all.sra ではなく個別に module() するほうが速い。

// ---- 幾何カーネル ----
module("cgal.so",           {optional:1});
module("manifold.so",       {optional:1});
module("nef_hybrid.so",     {optional:1});   // nef_snc.so は明示ロード (同じ op を出す変種のため)
module("geogram.so",        {optional:1});
module("cherchi.so",        {optional:1});

// ---- ボリューム ----
module("openvdb.so",        {optional:1});

// ---- 点群 (#3528: カーネル中立。外部ライブラリを持たないので必ず在る) ----
module("points.so",         {optional:1});

// ---- メッシュ共通 (#3527/#3545: カーネル中立。mf / gg / ch の計測・位相・片取り・頂点読み) ----
//   ⚠⚠ これが無いと valid(mfMesh) / genus / nparts / part / vert が
//     「no module can execute op」になる (2026-09-19 に抜けていたのを docs 点検で発見)。
module("geomutils.so",      {optional:1});

// ---- ボリューム橋渡し (voxelize / isosurface。openvdb_cg は export_vox も持つ = #3468) ----
module("openvdb_mf.so",     {optional:1});
module("openvdb_cg.so",     {optional:1});
module("openvdb_gg.so",     {optional:1});

// ---- Nef 橋渡し (#3499: nef_snc は常に SNC を書くので、他カーネルへ渡す cast はここが持つ) ----
//   ★ nef_snc.so 自体は上のとおり明示ロードだが、橋は入れておく — 変換専用 (priority 0) で
//     nf-mesh3d を入力に取るだけなので、nef_snc を使わない限り何も起きない。
//   ★ 2026-09-16 に SRAVA_MODULE_NEF_SNC の既定が ON になったので、この 2 本は
//     **素の構成でも建つ** ようになった (以前は optional:1 が実際に効いて黙って飛んでいた)。
module("nef_cg.so",         {optional:1});
module("nef_mf.so",         {optional:1});

// ---- B-rep ----
module("occt.so",           {optional:1});
module("occt_mf.so",        {optional:1});

// ---- 解析 ----
module("pipe_proximity.so", {optional:1});
