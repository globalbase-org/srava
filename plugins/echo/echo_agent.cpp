/*
 * echo_agent — プラグイン規約の疎通確認用の最小プラグインエージェント。
 *   op "plugin_echo": 受け取った引数をそのまま配列にして返す(計算なし)。
 *   引数 1 個なら その値、2 個以上なら [arg0, arg1, ...]。
 *
 * プラグイン SDK(pig 層)の使い方の手本でもある: compute() を 1 個書いて serve() に渡すだけ。
 * CGAL も srava も知らない。pig(pigData/SDK)+ libtinyState(sPtr)のみリンク。
 */
#include "pig/c++/pigPluginSDK.h"

static sPtr<pigData>
echo_compute(const char *op, sArray<sPtr<pigData> >& args)
{
	(void)op;
	if ( args.length() == 1 )
		return args[0];
	sPtr<pigDataArray> out = thNEW(pigDataArray,());
	for ( int i = 0 ; i < args.length() ; ++i )
		out->push(args[i]);
	return out;
}

int
main(void)
{
	return pigplugin::serve(&echo_compute);
}
