#ifndef PIGWIRE_H
#define PIGWIRE_H
/*
 * pigwire — srava ワイヤ文法(共有ヘッダ)
 *
 * プランナープロセス(srava) <-> エージェントプロセス(srava-agent) のパイプと、
 * キャッシュファイルが byte 一致するための「単一の真実」。
 *
 * ここがやるのは「バイト列 <-> フィールド変換」と「両者が一致すべき定数」だけ。
 * I/O も状態も死活監視も一切持たない純粋関数群。pig / tinyState 非依存。
 *
 * 設計: docs/codec_design.txt, docs/cache_format_catalog.txt
 */
#include <stdint.h>
#include <stddef.h>

/* ---- レコード type (u16) ---- */
enum {
  W_END        = 0,   /* 番兵 (payload_len=0, type=W_END) */

  /* 制御プレーン (pigPipe: srava <-> agent) */
  C_OP         = 1,   /* 演算子選択 + arg_count(u32) */
  C_ARG_PATH   = 2,   /* [arg_index(u32)][入力キャッシュのパス] */
  C_ARG_INLINE = 3,   /* [arg_index(u32)][スカラ/パラメタ srava 文法テキスト] */
  C_ARG_END    = 4,   /* 全引数送信完了 + 目標キャッシュパス(planner が必ず指定。欠落は agent 側 A_ERROR) */
  A_SAVE_BEGIN = 5,   /* 保存開始。pipeline トリガ。★payload は「値なら serialize テキスト /
                       *   ストリーム系(mesh 等)なら空」= **パスは載らない**。よって planner は
                       *   空を見たら自分が pl_write_end で渡した outCache を返す(照合はできない) */
  A_SAVE_DONE  = 6,   /* 保存完了(ファイルに W_END を書いた) */
  A_ERROR      = 7,   /* エラー(payload=メッセージ) */
  A_BYE        = 8,   /* 正常終了通知(END_NORMAL の検知点) */
  C_ARG_DATA   = 9,   /* ★ Internal 専用(ワイヤに乗らない): 引数 pigData 直渡し(#3406 4.3。
                       *   PATH/INLINE の弁別が消えた形。ptsMediatorPacket の type にのみ現れる) */

  /* データプレーン (pigCacheStream: キャッシュファイル本体) */
  D_META       = 64,  /* メタ(repr_type で分岐) */
  D_CHUNK      = 65,  /* 幾何本体 */
  D_TEXT       = 66,  /* データキャッシュ本文(srava 文法テキスト) */
  D_REF        = 67   /* 参照レコード(入力/出力) */
};

/* 値キャッシュの D_META 形式タグ = "TEXT" (WriterText が書き ReaderText の METADATA gate が検証する対)。
 * 値か型付きかの file 判別はこの 4CC で行う — 型レジストリ/codec 表は per-binary に偏るので
 * 判別には使えない (agent process は他 module の型を登録しないが file は読めることがある)。 */
static inline int wire_tag_is_text(const unsigned char t[4]) {
  return t[0]=='T' && t[1]=='E' && t[2]=='X' && t[3]=='T';
}

/* マジック: バイト列で 'P','W','I','G'。LE u32 で 0x47495750 */
static const uint32_t WIRE_MAGIC   = 0x47495750u;
static const uint16_t WIRE_VERSION = 1;

enum {
  WIRE_STREAMHDR_SIZE = 12, /* magic4 ver2 endian1 hflags1 pid4 */
  WIRE_RECHDR_SIZE    = 8,  /* len4 type2 rflags2            */
  WIRE_VARINT_MAX     = 10  /* 64bit LEB128 の最大バイト数    */
};

/* endian byte: 同一機械キャッシュ前提。世代/ビルド差の静かな誤読検出用 */
enum { WIRE_ENDIAN_LE = 1, WIRE_ENDIAN_BE = 2 };

/* wire_check_streamhdr の戻り値 */
enum {
  WIRE_OK          =  0,
  WIRE_ERR_MAGIC   = -1,
  WIRE_ERR_VERSION = -2,
  WIRE_ERR_ENDIAN  = -3
};

/* ---- varint (unsigned LEB128) ---- */

/* dst(最低 WIRE_VARINT_MAX byte)へ書き、書いた byte 数を返す */
static inline size_t wire_put_varint(uint8_t *dst, uint64_t v)
{
  size_t n = 0;
  do {
    uint8_t b = (uint8_t)(v & 0x7f);
    v >>= 7;
    if (v) b |= 0x80;
    dst[n++] = b;
  } while (v);
  return n;
}

/* src/avail から 1 個読む。1=ok(*used に消費 byte 数), 0=NEED_MORE / overlong */
static inline int wire_get_varint(const uint8_t *src, size_t avail,
                                  uint64_t *out, size_t *used)
{
  uint64_t v = 0;
  int shift = 0;
  size_t i;
  for (i = 0; i < avail && i < WIRE_VARINT_MAX; i++) {
    uint8_t b = src[i];
    v |= (uint64_t)(b & 0x7f) << shift;
    if (!(b & 0x80)) { *out = v; *used = i + 1; return 1; }
    shift += 7;
  }
  return 0; /* バイト不足 or 終端ビットが立たない(overlong) */
}

/* ---- レコードヘッダ (明示 LE パック。struct blit はパディングで死ぬので不可) ---- */

static inline void wire_put_rechdr(uint8_t *d, uint16_t type,
                                   uint16_t flags, uint32_t len)
{
  d[0] = (uint8_t)len;        d[1] = (uint8_t)(len >> 8);
  d[2] = (uint8_t)(len >> 16);d[3] = (uint8_t)(len >> 24);
  d[4] = (uint8_t)type;       d[5] = (uint8_t)(type >> 8);
  d[6] = (uint8_t)flags;      d[7] = (uint8_t)(flags >> 8);
}

static inline void wire_get_rechdr(const uint8_t *s, uint16_t *type,
                                   uint16_t *flags, uint32_t *len)
{
  *len   = (uint32_t)s[0]        | ((uint32_t)s[1] << 8)
         | ((uint32_t)s[2] << 16)| ((uint32_t)s[3] << 24);
  *type  = (uint16_t)((uint16_t)s[4] | ((uint16_t)s[5] << 8));
  *flags = (uint16_t)((uint16_t)s[6] | ((uint16_t)s[7] << 8));
}

/* ---- ストリームヘッダ ---- */

static inline void wire_put_streamhdr(uint8_t *d, uint32_t pid)
{
  d[0]  = (uint8_t)WIRE_MAGIC;        d[1]  = (uint8_t)(WIRE_MAGIC >> 8);
  d[2]  = (uint8_t)(WIRE_MAGIC >> 16);d[3]  = (uint8_t)(WIRE_MAGIC >> 24);
  d[4]  = (uint8_t)WIRE_VERSION;      d[5]  = (uint8_t)(WIRE_VERSION >> 8);
  d[6]  = WIRE_ENDIAN_LE;             /* 当面 LE 機械のみ */
  d[7]  = 0;                          /* hflags 予約 */
  d[8]  = (uint8_t)pid;               d[9]  = (uint8_t)(pid >> 8);
  d[10] = (uint8_t)(pid >> 16);       d[11] = (uint8_t)(pid >> 24);
}

/* magic/version/endian を検証。0=ok, <0=理由。pid!=NULL なら *pid に格納 */
static inline int wire_check_streamhdr(const uint8_t *s, uint32_t *pid)
{
  uint32_t magic = (uint32_t)s[0]        | ((uint32_t)s[1] << 8)
                 | ((uint32_t)s[2] << 16)| ((uint32_t)s[3] << 24);
  uint16_t ver   = (uint16_t)((uint16_t)s[4] | ((uint16_t)s[5] << 8));
  if (magic != WIRE_MAGIC)      return WIRE_ERR_MAGIC;
  if (ver   != WIRE_VERSION)    return WIRE_ERR_VERSION;
  if (s[6]  != WIRE_ENDIAN_LE)  return WIRE_ERR_ENDIAN;
  if (pid)
    *pid = (uint32_t)s[8]         | ((uint32_t)s[9] << 8)
         | ((uint32_t)s[10] << 16)| ((uint32_t)s[11] << 24);
  return WIRE_OK;
}

#endif /* PIGWIRE_H */
