
#ifndef ___ptsObject_cpp_H___
#define ___ptsObject_cpp_H___

#include	"pig/c++/pigData.h"        // get_env が返す pigEnvironment(完全型)
#include	"_ts2/c++/ptsObject_pb.h"
/* NB(#3406 4.2): 以前ここで ptsApplication.h を include していた(ptsApp 値メンバの完全型の便宜)が、
 * ptsApplication が ptsMediator 派生になったことで循環
 *   ptsMediator_pb.h → ptsObject.h → ptsApplication.h → (guard skip) → 不完全な ptsMediator を基底に
 * が成立してしまうため外した。ptsObject 派生クラスの TU は各自で
 * #include "pig/c++/ptsApplication.h" すること(ptsObject.h の直後でよい)。 */

#endif

