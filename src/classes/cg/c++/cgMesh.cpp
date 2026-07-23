/*
 * cgMesh — get_str() の out-of-line 定義(vtable/typeinfo の anchor。sPtr<cgMesh>::d_cast=
 * dynamic_cast が typeinfo を要するため、ヘッダ全 inline ではなくここに置く)。CGAL リンク側でのみ compile。
 */
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"

sPtr<stdString>
cgMesh::get_str()
{
	return thNEW(stdString,("<cgMesh>"));
}
