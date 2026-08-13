/*
 * pigCacheCodec — キャッシュ本文の reader/writer テーブル (#3406, 2026-07-29 メモ 3.)。
 *
 * ★ #3427 ③: 旧 namespace + 可変 static テーブルを廃し、**素の値クラス**にした。
 *   実体は pigModuleRegistry (ハブ) が直接メンバ `codecs` として所有し、ハブは
 *   ptsApplication が INI で thNEW する。登録は pigModuleRegistry::register_descriptor
 *   (記述子 codecs 表・#3427 ①) と pig-ref (ハブ ctor の組込登録) の経路のみ。
 *
 *   - reader 選択: キャッシュファイル先頭の D_META 4CC タグ ("MESH"/"PLY2"=cg, "MFM3"/"MFC2"=mf)。
 *     D_TEXT レコード (値キャッシュ) はコーデック外の既定 = ptsDataCache が ReaderText を使う。
 *   - writer 選択: set_body された本文 pigData の型 (match)。どれにも合わなければ既定 =
 *     WriterText (serialize) = 値キャッシュ。
 */
#ifndef ___pigCacheCodec_H___
#define ___pigCacheCodec_H___

#include	"ts2/c++/sPtr.h"
#include	"ts2/c++/stdString.h"

#include	<string>
#include	<vector>

class tinyState;
class ptsObject;
class pigData;
class pigTypeRegistry;

/* reader/writer 生成子。parent = ptsDataCache (イベントの受け手)。
 * 生成された reader は TSE_RETURN の msg_obj で本文 pigData を返す約束 (既存 ReaderMesh と同じ)。
 * writer は TSE_ASSERT (メタ書込済) → TSE_RETURN (全書込完了) を上げる約束 (既存 WriterMesh と同じ)。 */
typedef sPtr<tinyState> (*pigCacheReaderFn)(sPtr<ptsObject> parent, sPtr<stdString> path);
typedef sPtr<tinyState> (*pigCacheWriterFn)(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body);
typedef int             (*pigCacheMatchFn)(sPtr<pigData> body);

class pigCacheCodec {
public:
	/* コーデック 1 件を登録する。tags = D_META 4CC の並び (例 "MESH,PLY2")。
	 * out_types = tags と **位置対応**した出力型名の CSV (例 "cg-mesh3d,cg-cross2d")。i 番目のタグを
	 *   読んだとき reader が生成する型。reader_for(tag, target_type) の型軸選択に使う。無型 (REF 等) は "" 可。
	 * match = set_body 本文がこのコーデックのものか (d_cast で判定)。
	 * ★ P2 (⑤ 型変換): 旧 owner 引数 (module id) は撤去。planner が全モジュールの codec を同居させても、
	 *   reader は「タグ × 出力型」で一意に引ける (自型は out_type がタグの自型・foreign 昇格読みは
	 *   別の出力型) ので、どのモジュールが登録したか (owner) は不要になった。二重登録は name で先勝ち。 */
	void register_codec(const char *name, const char *tags, const char *out_types,
	                    pigCacheMatchFn match,
	                    pigCacheReaderFn mkReader, pigCacheWriterFn mkWriter);

	/* D_META 4CC タグから **canonical** (= そのタグの自型) の reader 生成子を引く (無ければ 0)。
	 * ★ get_body() 無指定 (自型読み) 用。実体は reader_for(tag, type_of_tag(tag)) + 「タグを読める任意
	 *   codec」フォールバック (単一モジュール agent で foreign 昇格読み codec しか無い場合や REF 等の無型)。
	 * types = タグの自型を引く型軸レジストリ (ハブの直接メンバ。呼び出しはハブの forwarder 経由が普通)。 */
	pigCacheReaderFn reader_for_tag(const unsigned char tag[4], const pigTypeRegistry &types) const;

	/* ★ P2 (⑤ 型変換): 「file の 4CC タグ」を読めて「出力型が target_type」の reader を引く (無ければ 0)。
	 *   cross-module 型変換 (get_body(type)) の中核: 消費者が欲しい型 target_type を渡すと、その形式
	 *   (file_tag) からその型を作れる codec を選ぶ。owner 概念に代わる 2 キー (tag, 出力型) の reader 選択。 */
	pigCacheReaderFn reader_for(const unsigned char file_tag[4], const char *target_type) const;
	/* 本文 pigData から writer 生成子を引く (無ければ 0 = 既定の WriterText へ)。 */
	pigCacheWriterFn writer_for_body(sPtr<pigData> body) const;

	/* ★ この本文は「ストリーム系」(codec が D_CHUNK/D_REF で書くもの) か、「値」(既定の D_TEXT) か。
	 *   1=ストリーム系 / 0=値。
	 *   判定は **保存時にどの writer が選ばれたか** と同一 — 別基準を立てると
	 *   「ディスクに書かれた形」と食い違い、読み手が誤読するため。
	 *   用途: agent が A_SAVE_BEGIN の payload を決める (値ならテキストを相乗り / ストリーム系は空)。
	 *   ⚠ 現在は「テーブルにマッチしない = TEXT = 値」という **消去法** に依存している
	 *     (TEXT はコーデック外の既定なので登録されていない)。将来 TEXT を codec として登録するなら、
	 *     ここを「マッチした codec のタグが TEXT か」の判定に **必ず** 差し替えること。 */
	int is_stream_body(sPtr<pigData> body) const;

private:
	struct Entry {
		std::string      name;
		std::string      tags;      /* "MESH,PLY2" 形式 (4CC のカンマ並び) */
		std::string      outTypes;  /* ★ P2: tags と位置対応した出力型名 CSV ("cg-mesh3d,cg-cross2d") */
		pigCacheMatchFn  match;
		pigCacheReaderFn mkReader;
		pigCacheWriterFn mkWriter;
	};
	std::vector<Entry> entries_v;
};

#endif
