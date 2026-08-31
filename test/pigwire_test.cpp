/* pigwire 単体テスト。フレームワーク無し、assert 風マクロのみ。 */
#include "pig/c++/pigwire.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
  /* --- varint ラウンドトリップ --- */
  uint64_t vals[] = {
    0, 1, 127, 128, 255, 256, 16383, 16384,
    0xFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull
  };
  for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
    uint8_t buf[WIRE_VARINT_MAX];
    size_t n = wire_put_varint(buf, vals[i]);
    uint64_t out = 0; size_t used = 0;
    CHECK(wire_get_varint(buf, n, &out, &used) == 1);
    CHECK(out == vals[i]);
    CHECK(used == n);
  }

  /* --- varint NEED_MORE: 2バイト varint(128) を 1 バイトだけ渡す --- */
  {
    uint8_t buf[WIRE_VARINT_MAX];
    size_t n = wire_put_varint(buf, 128);
    CHECK(n == 2);
    uint64_t o; size_t u;
    CHECK(wire_get_varint(buf, 1, &o, &u) == 0);
  }

  /* --- レコードヘッダ ラウンドトリップ --- */
  {
    uint8_t h[WIRE_RECHDR_SIZE];
    wire_put_rechdr(h, D_CHUNK, 0x1234, 0xDEADBEEFu);
    uint16_t t, f; uint32_t l;
    wire_get_rechdr(h, &t, &f, &l);
    CHECK(t == D_CHUNK);
    CHECK(f == 0x1234);
    CHECK(l == 0xDEADBEEFu);
  }

  /* --- 番兵: len=0, type=W_END --- */
  {
    uint8_t h[WIRE_RECHDR_SIZE];
    wire_put_rechdr(h, W_END, 0, 0);
    uint16_t t, f; uint32_t l;
    wire_get_rechdr(h, &t, &f, &l);
    CHECK(l == 0);
    CHECK(t == W_END);
  }

  /* --- ストリームヘッダ ラウンドトリップ + 検証 --- */
  {
    uint8_t h[WIRE_STREAMHDR_SIZE];
    /* ★ v2 (2026-08-26): pid に加えて writer プロセスの **起動時刻** を載せる。
     *   pid だけでは同一プロセスを名指せない (OS が使い回す) ため。 */
    wire_put_streamhdr(h, 4242, 0x0123456789abcdefULL);
    /* 先頭4バイトが 'P','W','I','G' であること */
    CHECK(h[0] == 'P' && h[1] == 'W' && h[2] == 'I' && h[3] == 'G');
    uint32_t pid = 0;
    uint64_t start = 0;
    CHECK(wire_check_streamhdr(h, &pid, &start) == WIRE_OK);
    CHECK(pid == 4242);
    CHECK(start == 0x0123456789abcdefULL);
    /* start を省いても読めること (呼び手が要らない場合) */
    CHECK(wire_check_streamhdr(h, &pid) == WIRE_OK);
    /* magic を壊すと弾く */
    h[0] ^= 0xff;
    CHECK(wire_check_streamhdr(h, 0) == WIRE_ERR_MAGIC);
  }

  printf(fails ? "PIGWIRE TEST: %d FAIL\n" : "PIGWIRE TEST: all pass\n", fails);
  return fails ? 1 : 0;
}
