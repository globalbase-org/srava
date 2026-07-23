#ifndef ___cgaBoolError_h___
#define ___cgaBoolError_h___
/*
 * ブール演算(union/intersection/difference/combine)の "missing operand" 用の詳細エラー生成。
 * 非決定的(キャッシュ競合等)に被演算子が mesh にならない事象を、ユーザがソース上で特定できるよう
 * 「どの被演算子が・何だったか(null=キャッシュ読込失敗 / 値ならその中身)」を文字列に起こす。
 */
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>

/* 被演算子 1 個の説明。mesh なら "<3D mesh>"、null なら読込失敗、それ以外は値の repr(切詰め)。 */
static inline sPtr<stdString> cga_operand_desc(sPtr<pigData> a) {
	if ( a == thNULL )
		return thNEW(stdString,("null (cache read/decode failed — possible cache race?)"));
	sPtr<cgMesh> m = sPtr<cgMesh>::d_cast(a);
	if ( m.is_notNull() ) {
		char b[32]; ::snprintf(b, sizeof b, "<%dD mesh ok>", m->dim());
		return thNEW(stdString,(b));
	}
	if ( a->is_error() )
		return thNEW(stdString,("error: "))->add(a->get_str());
	sPtr<stdString> s = a->get_str();
	if ( s->length() > 60 ) {   /* 巨大値は切詰め([0,60)) */
		sPtr<stdString> t = thNEW(stdString,(s->get_str(), 0, 60));
		return thNEW(stdString,("value: "))->add(t)->add("...");
	}
	return thNEW(stdString,("value: "))->add(s);
}

/* "<op>: operand not a mesh [operand1=…, operand2=…]" を作る(na も付ける)。 */
static inline sPtr<stdString> cga_missing_operand_msg(const char *op,
                                                      sPtr<pigData> a, sPtr<pigData> b, int na) {
	char head[96]; ::snprintf(head, sizeof head, "%s: operand is not a mesh (got %d operand(s)) [", op, na);
	return thNEW(stdString,(head))
	    ->add("operand1=")->add(cga_operand_desc(a))
	    ->add(", operand2=")->add(cga_operand_desc(b))
	    ->add("]");
}

#endif
