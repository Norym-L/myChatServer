#ifndef GATESERVER_MESSAGENODE_H
#define GATESERVER_MESSAGENODE_H

#include <cstdint>

#include "const.h"

class MessageNode
{
public:
    explicit MessageNode(std::uint8_t len = HEAD_TOTAL_LEN);
};


#endif //GATESERVER_MESSAGENODE_H
