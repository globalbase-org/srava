/*
 * tsPipeRepro — ts2IO::write_c の「pipe バッファ超えで停止」最小再現(ptsObject 派生)。
 *
 * 現象(cgal-processor #4 で判明): プランナー(ts2IO write_c)→ agent(ts2IO read_c)へ
 *   1 レコードで pipe バッファ(Linux 既定 64KB)を超えるペイロードを送ると、write_c が満杯で
 *   EAGAIN yield(sException)し、その resume が select の write-readiness で駆動されず**停止**
 *   (両プロセスとも do_select)。
 *
 * 本ファイルは pig/wire を介さず、**素の ts2System + ts2IO::write_c だけ**で再現する:
 *   ① ts2System で子(ドレインするだけの reader。既定 `cat >/dev/null`)を起動し wfd(ts2IO)を得る。
 *   ② wfd->write_c(buf, BYTES) で BYTES バイトを 1 回で送る。
 *   ③ 完走したら "[repro] DONE wrote N bytes" を表示して終了。停止したら表示が出ずハングする。
 *
 * 期待: BYTES <= pipe バッファ(~64KB)は完走、超えると停止(resume 不全)。
 *   ts2IO::set_divisible() で改善するか(TSPIPE_DIVISIBLE=1)も切り分けられる。
 *
 * env パラメタ:
 *   TSPIPE_BYTES     (既定 131072 = 128KB)  : write_c で送るバイト数
 *   TSPIPE_DIVISIBLE (既定 0)                : 1 で wfd->set_divisible() を有効化
 *   TSPIPE_CMD       (既定 "cat >/dev/null") : 子(stdin をドレインする reader)
 *   ※ 子の初動を遅らせて pipe を確実に満杯にしたい時は
 *      TSPIPE_CMD="sh -c 'sleep 1; exec cat >/dev/null'" 等。
 *
 * 使い方: `timeout 10 ./repro_pipe` でハング(=再現)を検出。
 */
#include	"pig/c++/ptsObject.h"
#include	"ts2/c++/ts2System.h"
#include	"ts2/c++/ts2IO.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/tsPipeRepro_.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

CLASS_TINYSTATE(pig/c++/tsPipeRepro,pig/c++/ptsObject)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	tsPipeRepro_(
		sPtr<tinyState> parent);

	sRptr<tinyState,tinyState>		parent;
protected:
	sPtr<ts2System>		sys;
	sPtr<ts2IO>		rfd;
	sPtr<ts2IO>		wfd;
	int			ret;
	int			bytes;
	int			divisible;
	const char *		cmd;
	char *			buf;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class tinyState;
class ts2System;
class ts2IO;
TS_END_INTERFACE

#endif


tsPipeRepro_::tsPipeRepro_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    ret = 0;
    const char *b = ::getenv("TSPIPE_BYTES");
    const char *d = ::getenv("TSPIPE_DIVISIBLE");
    cmd = ::getenv("TSPIPE_CMD");
    bytes     = b ? ::atoi(b) : (128 * 1024);
    divisible = d ? ::atoi(d) : 0;
    if ( cmd == 0 ) cmd = "cat >/dev/null";
    buf = 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	return rDO|ACT_tsPipeRepro_SPAWN;
}

TS_STATE(ACT_tsPipeRepro_SPAWN)
{
	::printf("[repro] spawn child=\"%s\"; send %d bytes (divisible=%d, pipe=64KB default)\n",
	         cmd, bytes, divisible);
	::fflush(stdout);

	ret = 0;
	sys = thNEW(ts2System,(ifThis, &ret, cmd, &rfd, (sPtr<ts2IO>*)0, &wfd, 0));
	if ( ret < 0 || wfd == thNULL ) {
		::printf("[repro] FAILED to launch child\n"); ::fflush(stdout);
		return rDO|FIN_START;
	}
	if ( divisible )
		wfd->set_divisible();

	buf = (char *)::malloc(bytes);
	::memset(buf, 'x', bytes);   /* 中身は何でもよい(ドレインされるだけ) */

	return rDO|ACT_tsPipeRepro_WRITE;
}

TS_STATE(ACT_tsPipeRepro_WRITE)
{
	/* write_c は満杯で EAGAIN なら sException で yield → この状態が resume で再走。
	 * buf は member なので yield 跨ぎで安定。完走できなければここでハングする(=再現)。 */
	ret = wfd->write_c(buf, bytes);
	::printf("[repro] DONE wrote %d bytes (write_c ret=%d)\n", bytes, ret);
	::fflush(stdout);

	wfd->destroy();   /* EOF を送って子を終了させる */
	wfd = thNULL;
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	if ( buf ) { ::free(buf); buf = 0; }
	return rDO|FIN_ptsObject_START;
}
