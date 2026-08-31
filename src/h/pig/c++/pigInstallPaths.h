#ifndef ___pigInstallPaths_H___
#define ___pigInstallPaths_H___
/*
 * pigInstallPaths — **install レイアウトを実行体からの相対で解く** (#3431 P0-a・2026-08-18)。
 *
 * ★なぜ要るか: 探索路と agent 既定が「configure 時の CMAKE_INSTALL_PREFIX を焼き込んだ絶対パス」
 *   しか持たなかったため、install ツリーは **configure したときの場所に置いたときだけ**動いていた。
 *   別の場所へ install / DESTDIR で staging / tar で配布 のいずれでも、
 *       modules は $PREFIX/lib/srava/modules ・実行体は $PREFIX/bin
 *   という配置なので「実行体と同じ dir」の探索路にも引っかからず、
 *   **その機械の /usr/local にある別世代の install** を黙って読みに行く (実測で踏んだ)。
 *   版が違えば pigBuildStamp が明示エラーで止めてくれるが、そもそも自分の兄弟を見に行くのが正しい。
 *
 * → install レイアウト
 *       <prefix>/bin/srava, srava_agent
 *       <prefix>/lib/srava/modules/*.so
 *   を **実行体の位置から相対で**解いて、prefix ごと動かしても動くようにする。
 */

/* 実行中バイナリの置かれた dir。取得できなければ "" (空文字列)。プロセス中で 1 回だけ解決してキャッシュ。 */
const char *srava_exe_dir();

/* 実行体から見た install モジュール dir (<exe dir>/../lib/srava/modules)。
 * 実行体 dir が取れなければ ""。**存在確認はしない** (探索路は無い dir を黙って読み飛ばす)。 */
const char *srava_module_dir_relative_to_exe();

/* 起動する srava_agent のパス。次の順で決める:
 *   ① env SRAVA_AGENT          … テスト/差し替え用の明示指定 (最優先)
 *   ② <exe dir>/srava_agent    … **自分の兄弟**。ビルドツリーでも install ツリーでも成立する
 *   ③ SRAVA_AGENT_DEFAULT      … configure 時の $PREFIX/bin/srava_agent (従来の既定)
 * ② のファイル名 (Windows の .exe 付き) は SRAVA_AGENT_DEFAULT の末尾成分から取るので、
 * 実行ファイル拡張子の扱いが CMake 側と自動的に揃う。 */
const char *srava_agent_path();

#endif
