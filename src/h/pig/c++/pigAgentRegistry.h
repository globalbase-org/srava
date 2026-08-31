/*
 * pigAgentRegistry — 「KERNEL 名 → ptsAgent 生成子」のテーブル (#3406 段階 4.2)。
 *
 * agent process の root (ptsApplication) が enable() で「どの実行体 (ptsAgent 派生) を起こすか」を
 * 決めるための表。記述子 (srava_module_descriptor.make_agent) が登録し、root は名前で引くだけ =
 * root が具体クラスを知らない。
 *
 * ★ #3427 ③: 旧 namespace + 可変 static テーブルを廃し、**素の値クラス**にした。
 *   実体は pigModuleRegistry (ハブ) が直接メンバ `agents` として所有し、ハブは
 *   ptsApplication が INI で thNEW する。登録は pigModuleRegistry::register_descriptor
 *   (記述子駆動・#3427 ①) の 1 経路のみ = 静的初期化の登録は廃止済み。
 */
#ifndef ___pigAgentRegistry_H___
#define ___pigAgentRegistry_H___

#include	"pig/c++/ptsAgent.h"
#include	"pig/c++/ptsObject.h"

#include	<string>
#include	<vector>

/* 生成子: parent = 自分を起こした親 (agent process では ptsAgentApplication、
 * planner 内 thread では ptsMediatorInternal・4.3)。
 * ★ 型が ptsObject なのは 2026-08-02 メモ §1 — 実行体は親が Mediator かどうかを知らない
 * (結果は set_result → FIN の TSE_RETURN で返すので、親の型に依存する呼び出しが無い)。 */
typedef sPtr<ptsAgent> (*pigAgentFactory)(sPtr<ptsObject> med);

/* ★ #3439 ⑤: class pigAgentRegistry (派生テーブル) は削除した。生成子の検索は
 * pigModuleRegistry::agent_factory() が **記述子 (make_agent) を is_enabled を見ながら走査**する。
 * このヘッダは上の pigAgentFactory 型だけを提供する。 */

#endif
