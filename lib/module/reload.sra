// module/reload.sra — モジュールの入れ替えヘルパ (2026-08-28)。
//   include "module/reload.sra"; で取り込む。
//
// #3452 以降、module(so,{...}) は「未ロードなら読み込む + 記述子の設定を上書き」だが、
// **既にロード済みの .so 自体を差し替えることはできない** (1 モジュール名につき dlopen は 1 回)。
// module(so,"off") が実アンロード (dlclose) になったので、落としてから読み直せば差し替えられる。
//
// ★ このファイルは all.sra には入れていない。all.sra は「6 本まとめてロードする」便宜品なので、
//   ヘルパを使うためだけに全モジュールがロードされてしまうため。

// module_reload(path, opts): その .so を **落としてから読み直す**。
//   未ロードなら単に読み込む (module(path, opts) と同じ)。
//     module_reload("cgal.so", {priority:99});
//     module_reload("/tmp/experimental/cgal.so", {});   // 開発中の .so に差し替える
//
//   ★ **一度でも使われたモジュールは落とせない** — その .so 由来のオブジェクト
//     (メッシュ本体・agent) が生きている可能性があるため。module(path,"off") が
//     "cannot unload ... already used by this program" で明示エラーになるので、
//     差し替えはそのモジュールで op を実行する **前** に行うこと。
//
//   ★ 同じファイル名で別の実ファイルに差し替える用途にも使える。落とさずに
//     module("/tmp/experimental/cgal.so",{}) とすると「同名で別ファイル」の明示エラーになる。
var module_reload = \(path, opts) {
    if ( module_loaded(path) ) { module(path, "off"); }
    module(path, opts);
};
