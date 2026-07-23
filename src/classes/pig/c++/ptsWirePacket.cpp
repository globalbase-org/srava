#include "pig/c++/ptsWirePacket.h"

ptsWirePacket::ptsWirePacket(uint16_t _type, uint16_t _flags,
                              const uint8_t *data, uint32_t len)
    : type(_type), flags(_flags)
{
    if ( data && len > 0 ) {
        payload.length((int)len);
        for (uint32_t i = 0; i < len; ++i) payload[i] = data[i];
    }
}
