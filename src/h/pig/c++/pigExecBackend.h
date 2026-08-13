#ifndef ___pigExecBackend_H___
#define ___pigExecBackend_H___
/*
 * pigExecBackend — 「起動方式 (execution backend) → Mediator 生成」の抽象レジストリ。
 *   docs/agent_so_design.md §2.3 / .so 化 Phase1-3 (2026-08-08)。
 *
 * 現状の 2 方式を名前で選べるようにし、pigfAgent から具体 Mediator クラス
 * (ptsMediatorInternal / ptsMediatorExternal) の名指し thNEW を消す:
 *   "thread"  → ptsMediatorInternal (planner 内 thread。param = カーネル名 = pigAgentRegistry キー)
 *   "process" → ptsMediatorExternal (agent プロセス。param = 起動コマンド)
 * 将来 "remote" 等を register_backend で足すだけで pigfAgent は無改修 (ひさ指示・起動方式拡張)。
 *
 * ★ Phase 1 は機能不変: pigfAgent の「thread 試行 → 失敗で process フォールバック」の**選択
 *   ロジックは pigfAgent 側に残す**。ここが抽象化するのは **生成 (どの Mediator を thNEW するか)**
 *   だけ。exec_caps/exec_default による方式決定は Phase 2 以降。
 *
 * ★ #3427 ③: 旧 namespace + 可変 static テーブルを廃し、**素の値クラス**にした。
 *   実体は pigModuleRegistry (ハブ) が直接メンバ `backends` として所有する。
 *   組込 2 方式 (thread/process) は **ctor が登録する** (pigExecBackend.cpp = 「起動方式 →
 *   具体 Mediator クラス」を知る唯一の TU、は従来どおり)。
 * param の型は両方式とも sPtr<stdString> で共通。
 */
#include "ts2/c++/sPtr.h"

#include <string>
#include <vector>

class ptsMediator;
class ptsObject;
class stdString;

typedef sPtr<ptsMediator> (*pigExecFactory)(sPtr<ptsObject> parent, sPtr<stdString> param);

class pigExecBackend {
public:
	pigExecBackend();   /* 組込 backend (thread/process) を登録する */

	/* 方式名 → Mediator 生成子を登録 (先勝ち)。 */
	void              register_backend(const char *name, pigExecFactory f);
	/* 方式名で Mediator を生成 (未登録 = thNULL)。 */
	sPtr<ptsMediator> make(const char *name, sPtr<ptsObject> parent, sPtr<stdString> param) const;

private:
	struct Entry { std::string name; pigExecFactory make; };
	std::vector<Entry> entries_v;
};

#endif
