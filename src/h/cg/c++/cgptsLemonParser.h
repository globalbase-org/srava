
#ifndef ___cgptsLemonParser_cpp_H___
#define ___cgptsLemonParser_cpp_H___

#include	"_ts2/c++/cgptsLemonParser_pb.h"

/* ★ #3427 ③: VALUE モード値パーサの生成子 (pigValueParserFn 形)。
 * cgptsPlanner の INI / テスト fixture が app 所有レジストリ (module_registry->vparser) へ登録する
 * (旧: この TU の静的初期化がグローバルスロットへ自己登録していた)。 */
class ptsObject;
class stdString;
class tinyState;
sPtr<tinyState> cg_mk_value_parser(sPtr<ptsObject> parent, sPtr<stdString> text);

#endif
