
#ifndef ___ptsApplication_cpp_H___
#define ___ptsApplication_cpp_H___

/* ★ #3419: ptsApplication は sPtr<ptsLoadControl> をメンバに持つ (§2.4/§13.7)。sPtr のデストラクタが
 * 完全型を要求するので、**公開ヘッダ側でここを閉じる**。消費側 (20 ファイル超) に個別に
 * include を撒くのは筋が悪い。pigModuleRegistry も本来は同じ扱いでよいが、既に消費側が
 * 個別に include しているので現状維持 (触ると差分が広がるだけ)。 */
#include	"pig/c++/ptsLoadControl.h"
#include	"_ts2/c++/ptsApplication_pb.h"

#endif

