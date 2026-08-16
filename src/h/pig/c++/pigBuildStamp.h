#ifndef ___pigBuildStamp_H___
#define ___pigBuildStamp_H___
/* planner と agent が同じビルドかを突き合わせる識別子(実装は pigBuildStamp.cpp)。
 * 版が違う組み合わせは沈黙ハングや誤ったエラーになるため、起動時にここで弾く。 */
const char* srava_build_stamp();
#endif
