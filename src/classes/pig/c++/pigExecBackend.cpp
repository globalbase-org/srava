/*
 * pigExecBackend — 実装。★ #3427 ③: 可変 static テーブルを廃し値クラス化。
 *   実体は pigModuleRegistry (ハブ) が所有し、組込 2 方式は ctor が登録する。
 *   設計: pigExecBackend.h / docs/agent_so_design.md §2.3。
 *
 * ★ この TU が「起動方式 → 具体 Mediator クラス」を知る**唯一の場所**。pigfAgent は方式名で引くだけ。
 *   backend が増えても、新方式の register_backend をここか .so 側に足すだけ。
 */
#include "pig/c++/pigExecBackend.h"
#include "pig/c++/ptsMediator.h"
#include "pig/c++/ptsMediatorInternal.h"
#include "pig/c++/ptsMediatorExternal.h"
#include "pig/c++/ptsObject.h"
#include "ts2/c++/stdString.h"

namespace {

/* "thread" = planner 内 thread。param = カーネル名 (pigAgentRegistry のキー)。 */
sPtr<ptsMediator> make_thread(sPtr<ptsObject> parent, sPtr<stdString> param) {
  return thNEW(ptsMediatorInternal, (parent, param));
}
/* "process" = agent プロセス。param = 起動コマンド。 */
sPtr<ptsMediator> make_process(sPtr<ptsObject> parent, sPtr<stdString> param) {
  return thNEW(ptsMediatorExternal, (parent, param));
}

}   /* anonymous namespace */

/* 組込登録 (Internal/External は libpig に常にリンクされる)。
 * ★ #3427 ③: 旧「静的初期化ラムダでグローバル表へ登録」を ctor 登録へ (per-registry = per-app)。 */
pigExecBackend::pigExecBackend()
{
  register_backend("thread",  &make_thread);
  register_backend("process", &make_process);
}

void
pigExecBackend::register_backend(const char *name, pigExecFactory f)
{
  if (name == 0 || f == 0) return;
  for (size_t i = 0; i < entries_v.size(); ++i)
    if (entries_v[i].name == name) return;         /* 先勝ち */
  Entry e; e.name = name; e.make = f;
  entries_v.push_back(e);
}

sPtr<ptsMediator>
pigExecBackend::make(const char *name, sPtr<ptsObject> parent, sPtr<stdString> param) const
{
  if (name == 0) return sPtr<ptsMediator>();
  for (size_t i = 0; i < entries_v.size(); ++i)
    if (entries_v[i].name == name) return entries_v[i].make(parent, param);
  return sPtr<ptsMediator>();
}
