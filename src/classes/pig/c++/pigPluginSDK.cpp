/*
 * pigPluginSDK — serve() の実装。pigwire を blocking read/write(fd 0/1)で 1 往復だけ話す。
 * tinyState の状態機械(ts2IO/ptsWirePipe)は使わず、pigwire.h のフレーミングヘルパ + 素の
 * read()/write() で完結する(プラグインが我々の状態機械に縛られない最薄構成)。
 *
 * 受信(planner→agent): streamhdr, C_OP(op名), [C_ARG_INLINE(idx,text)]*, C_ARG_END(cachePath), W_END
 * 送信(agent→planner): streamhdr, A_SAVE_BEGIN(結果テキスト相乗り), A_SAVE_DONE, A_BYE, W_END
 * 加えて結果をキャッシュファイル(cachePath)へ: streamhdr, D_META("TEXT"), D_TEXT(結果), W_END
 *   → 次回同一リクエストはプランナが file HIT で読む(agent 起動なし)。
 */
#include "pig/c++/pigPluginSDK.h"
#include "pig/c++/pigValueCodec.h"
#include "pig/c++/pigwire.h"
#include "ts2/c++/stdString.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <string>
#include <vector>

namespace pigplugin {

/* ---- blocking IO ヘルパ ---- */

/* n バイトきっちり読む。EOF/エラーは 0、成功 1。 */
static int read_full(int fd, uint8_t *buf, size_t n) {
	size_t got = 0;
	while ( got < n ) {
		ssize_t r = ::read(fd, buf + got, n - got);
		if ( r == 0 ) return 0;                 /* EOF */
		if ( r < 0 ) { if ( errno == EINTR ) continue; return 0; }
		got += (size_t)r;
	}
	return 1;
}

static int write_full(int fd, const uint8_t *buf, size_t n) {
	size_t put = 0;
	while ( put < n ) {
		ssize_t w = ::write(fd, buf + put, n - put);
		if ( w < 0 ) { if ( errno == EINTR ) continue; return 0; }
		put += (size_t)w;
	}
	return 1;
}

/* 1 レコードを std::vector へ書き足す(rechdr + payload)。 */
static void put_record(std::vector<uint8_t>& out, uint16_t type, const uint8_t *payload, uint32_t len) {
	uint8_t h[WIRE_RECHDR_SIZE];
	wire_put_rechdr(h, type, 0, len);
	out.insert(out.end(), h, h + WIRE_RECHDR_SIZE);
	if ( len > 0 && payload != 0 ) out.insert(out.end(), payload, payload + len);
}

/* fd へ 1 レコードを直接書く(stdout 応答用)。 */
static int write_record(int fd, uint16_t type, const uint8_t *payload, uint32_t len) {
	uint8_t h[WIRE_RECHDR_SIZE];
	wire_put_rechdr(h, type, 0, len);
	if ( ! write_full(fd, h, WIRE_RECHDR_SIZE) ) return 0;
	if ( len > 0 && payload != 0 ) return write_full(fd, payload, len);
	return 1;
}

/* stdin から 1 レコードを読む。type と payload(vector)を返す。0=EOF/エラー。 */
static int read_record(int fd, uint16_t *type, std::vector<uint8_t>& payload) {
	uint8_t h[WIRE_RECHDR_SIZE];
	if ( ! read_full(fd, h, WIRE_RECHDR_SIZE) ) return 0;
	uint16_t t, flags; uint32_t len;
	wire_get_rechdr(h, &t, &flags, &len);
	*type = t;
	payload.resize(len);
	if ( len > 0 && ! read_full(fd, &payload[0], len) ) return 0;
	return 1;
}

/* 結果をキャッシュファイルへ WriterText 形式で書く(streamhdr + D_META"TEXT" + D_TEXT + W_END)。 */
static void write_cache_file(const char *path, const char *text) {
	if ( path == 0 || path[0] == 0 ) return;
	FILE *f = ::fopen(path, "wb");
	if ( f == 0 ) return;
	std::vector<uint8_t> out;
	uint8_t sh[WIRE_STREAMHDR_SIZE];
	wire_put_streamhdr(sh, (uint32_t)::getpid());
	out.insert(out.end(), sh, sh + WIRE_STREAMHDR_SIZE);
	put_record(out, D_META, (const uint8_t*)"TEXT", 4);
	put_record(out, D_TEXT, (const uint8_t*)text, (uint32_t)::strlen(text));
	put_record(out, W_END, 0, 0);
	::fwrite(out.empty() ? (const void*)"" : (const void*)&out[0], 1, out.size(), f);
	::fclose(f);
}

/* A_ERROR + W_END を stdout に出して 1 を返す(streamhdr はハンドシェイクで送信済み)。 */
static int reply_error(int fd, const char *msg) {
	write_record(fd, A_ERROR, (const uint8_t*)msg, (uint32_t)::strlen(msg));
	write_record(fd, W_END, 0, 0);
	return 1;
}

int serve(ComputeFn compute)
{
	const int FD_IN = 0, FD_OUT = 1;

	/* 0) ★ハンドシェイク: 起動直後に自分の streamhdr を送る。planner は agent の streamhdr 受信
	 *    (TSE_ASSERT)を待ってから C_OP を送るので、これを先に出さないと相手が永久に待つ。 */
	uint8_t osh[WIRE_STREAMHDR_SIZE];
	wire_put_streamhdr(osh, (uint32_t)::getpid());
	if ( ! write_full(FD_OUT, osh, WIRE_STREAMHDR_SIZE) ) return 1;

	/* 1) planner の streamhdr を読む。 */
	uint8_t sh[WIRE_STREAMHDR_SIZE];
	if ( ! read_full(FD_IN, sh, WIRE_STREAMHDR_SIZE) ) return 1;
	if ( wire_check_streamhdr(sh, 0) != WIRE_OK )   return reply_error(FD_OUT, "plugin: bad stream header");

	/* 2) C_OP, C_ARG_INLINE*, C_ARG_END を読む。 */
	std::string op;
	std::string cachePath;
	sArray<sPtr<pigData> > args;
	int gotEnd = 0;
	for ( ;; ) {
		uint16_t type; std::vector<uint8_t> pl;
		if ( ! read_record(FD_IN, &type, pl) ) break;
		if ( type == W_END ) break;
		if ( type == C_OP ) {
			op.assign((const char*)(pl.empty()?(const uint8_t*)"":&pl[0]), pl.size());
		} else if ( type == C_ARG_INLINE ) {
			if ( pl.size() < 4 ) return reply_error(FD_OUT, "plugin: arg missing index");
			uint32_t idx = (uint32_t)pl[0] | ((uint32_t)pl[1]<<8) | ((uint32_t)pl[2]<<16) | ((uint32_t)pl[3]<<24);
			std::string text((const char*)&pl[4], pl.size() - 4);
			while ( (uint32_t)args.length() <= idx ) args.push(sPtr<pigData>());   /* idx まで伸長 */
			sPtr<pigData> v = pig_value_parse(text.c_str());
			if ( v->is_error() ) return reply_error(FD_OUT, "plugin: cannot parse inline argument value");
			args[(int)idx] = v;
		} else if ( type == C_ARG_PATH ) {
			return reply_error(FD_OUT, "plugin: this plugin takes value arguments only (got a cache/mesh handle)");
		} else if ( type == C_ARG_END ) {
			cachePath.assign((const char*)(pl.empty()?(const uint8_t*)"":&pl[0]), pl.size());
			gotEnd = 1;
			break;
		}
		/* 他の type は無視 */
	}
	if ( ! gotEnd ) return reply_error(FD_OUT, "plugin: request ended before C_ARG_END");

	/* 欠けた引数(idx 飛び)は null 埋め → エラーにせず compute に委ねる(arity は plugin が検証)。 */
	for ( int i = 0 ; i < args.length() ; ++i )
		if ( ! args[i].is_notNull() ) args[i] = thNEW(pigDataNull,());

	/* 3) 計算。 */
	sPtr<pigData> result = compute(op.c_str(), args);
	if ( result.is_notNull() == 0 )   /* null = plugin の不備 */
		return reply_error(FD_OUT, "plugin: compute returned null");
	if ( result->is_error() ) {
		sPtr<stdString> m = result->get_str();
		return reply_error(FD_OUT, m != thNULL ? m->get_str() : "plugin: error");
	}

	/* 4) 結果を serialize(値の wire 形式)。 */
	sPtr<stdString> rs = result->serialize();
	const char *body = ( rs != thNULL ) ? rs->get_str() : "null";

	/* 5) キャッシュファイルへ保存(次回 HIT 用)。 */
	write_cache_file(cachePath.c_str(), body);

	/* 6) stdout 応答(streamhdr はハンドシェイクで送信済み): A_SAVE_BEGIN(本文相乗り) +
	 *    A_SAVE_DONE + A_BYE + W_END。 */
	write_record(FD_OUT, A_SAVE_BEGIN, (const uint8_t*)body, (uint32_t)::strlen(body));
	write_record(FD_OUT, A_SAVE_DONE, 0, 0);
	write_record(FD_OUT, A_BYE, 0, 0);
	write_record(FD_OUT, W_END, 0, 0);
	return 0;
}

} /* namespace pigplugin */
