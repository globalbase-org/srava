#ifndef PTS_MEDIATOR_PACKET_H
#define PTS_MEDIATOR_PACKET_H
/*
 * ptsMediatorPacket — ptsMediatorInternal 経路の TSE_PACKET payload (#3406 4.3)。
 * ptsWirePacket (生バイト列) の対になる **pigData 直渡し** 形。ワイヤには乗らない。
 * type は pigwire の cmd 値を流用 (C_OP / C_ARG_DATA / C_ARG_END / A_SAVE_* / A_ERROR)。
 *   C_OP        : str = 演算子名
 *   C_ARG_DATA  : idx = 引数番号, data = 引数 pigData (cache は pigDataCache ハンドルそのもの
 *                 = planner と共有。body 在中なら受け手の get_body が即返り = 再構築なし)
 *   C_ARG_END   : data = 出力 pigDataCache (agent はここへ set_body する)。目標パスは
 *                 data->get_path() から取る (§5.2: str には載せない)。Internal では planner が
 *                 先に作ったオブジェクトそのもの = 共有、External では受信パスから組んだ新品
 *   A_SAVE_BEGIN: data = 値返し op の結果 (構造化 pigData 直渡し・パース不要)。mesh は thNULL
 *   A_ERROR     : str = エラーメッセージ
 * 受け手は msg_obj を d_cast して ptsWirePacket (External) / ptsMediatorPacket (Internal) を弁別する。
 */
#include "ts2/c++/stdObject.h"
#include "ts2/c++/sPtr.h"
#include "ts2/c++/stdString.h"
#include "pig/c++/pigData.h"   /* sPtr<pigData> 値メンバの完全型 */
#include <stdint.h>

class ptsMediatorPacket : public stdObject {
public:
    ptsMediatorPacket(int _type, uint32_t _idx,
                      sPtr<pigData> _data, sPtr<stdString> _str)
        : type(_type), idx(_idx), data(_data), str(_str) {}
    ~ptsMediatorPacket() {}

    int             type;   /* pigwire cmd 値 */
    uint32_t        idx;    /* C_ARG_DATA の引数番号 (他は 0) */
    sPtr<pigData>   data;
    sPtr<stdString> str;
};

#endif /* PTS_MEDIATOR_PACKET_H */
