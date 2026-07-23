#ifndef PTS_WIRE_PACKET_H
#define PTS_WIRE_PACKET_H
/*
 * ptsWirePacket — ptsWirePipe が parent に TSE_PACKET で届けるパケットオブジェクト。
 * type=受信レコード種別, flags=rflags, payload=生バイト列。
 * step 5 で ptsWirePipe を実装する際に利用。
 */
#include "ts2/c++/stdObject.h"
#include "ts2/c++/sArray.h"
#include <stdint.h>

class ptsWirePacket : public stdObject {
public:
    ptsWirePacket(uint16_t _type, uint16_t _flags,
                  const uint8_t *data, uint32_t len);
    ~ptsWirePacket() {}

    uint16_t type;
    uint16_t flags;
    sArray<uint8_t> payload;
};

#endif /* PTS_WIRE_PACKET_H */
