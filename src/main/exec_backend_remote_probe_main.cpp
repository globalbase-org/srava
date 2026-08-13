/*
 * exec_backend_remote_probe — 起動方式 (execution backend) registry に **remote 枠**を 1 つ足せる
 *   ことの確認 (docs §7 Phase 6・実装は別)。
 *
 * pigExecBackend は "thread"/"process" を静的登録しているが、レジストリ自体は名前→生成子の汎用
 * テーブル (docs §2.3)。ここではダミーの "remote" backend を register_backend し:
 *   ① make("remote", …) がその生成子へ dispatch すること (= 枠を足せる)
 *   ② 未登録名は dispatch されないこと (= 名前解決が効いている)
 * を確認する。実際の ptsMediatorRemote 実装は別チケット (今回は枠の存在確認のみ)。
 */
#include "pig/c++/pigExecBackend.h"
#include "pig/c++/ptsMediator.h"    /* sPtr<ptsMediator> の完全型 (空 sPtr 破棄で relref) */
#include "pig/c++/ptsObject.h"      /* sPtr<ptsObject> の完全型 */
#include "ts2/c++/stdString.h"      /* sPtr<stdString> の完全型 */
#include "ts2/c++/sPtr.h"

#include <cstdio>

/* ★ #3427 ③ NB: pigExecFactory は素の関数ポインタ (キャプチャ不可) なので、「呼ばれた」印は
 * この probe main 専用の file-static に残している。単発実行のテストプローブ (ライブラリ非リンク・
 * 単一スレッド・プロセス 1 回きり) なのでリエントラント性の例外とする。 */
static int g_remote_called = 0;

/* ダミー生成子: 実 Mediator は作らない (枠の確認なので thNULL で十分)。呼ばれたことだけ記録。 */
static sPtr<ptsMediator>
fake_remote(sPtr<ptsObject>, sPtr<stdString>)
{
	g_remote_called = 1;
	return sPtr<ptsMediator>();
}

int
main(void)
{
	/* ★ #3427 ③: レジストリは値クラス化 (可変 static 全廃)。probe はローカルインスタンスで検証
	 * (ctor が thread/process を組込登録するのも同時に確認できる)。 */
	pigExecBackend backends;
	backends.register_backend("remote", &fake_remote);

	/* ① remote 枠へ dispatch されるか。 */
	backends.make("remote", sPtr<ptsObject>(), sPtr<stdString>());
	if ( ! g_remote_called ) {
		::printf("REMOTE_SLOT_FAIL: \"remote\" backend was not dispatched\n");
		return 1;
	}

	/* ② 未登録名は dispatch されない (名前解決が効いている)。 */
	g_remote_called = 0;
	backends.make("nonexistent_backend_xyz", sPtr<ptsObject>(), sPtr<stdString>());
	if ( g_remote_called ) {
		::printf("REMOTE_SLOT_FAIL: unknown backend name dispatched\n");
		return 1;
	}

	::printf("REMOTE_SLOT_OK: register_backend(\"remote\") + make() dispatch confirmed\n");
	return 0;
}
