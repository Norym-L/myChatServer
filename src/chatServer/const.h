#ifndef GATESERVER_CONST_H
#define GATESERVER_CONST_H

namespace chat
{
#define MAX_LENGTH      (1024 * 2)
#define HEAD_TOTAL_LEN  4 // tcp头部长度
#define HEAD_ID_LEN     2 // tcp头部 message_id 长度
#define HEAD_DATA_LEN   2 // tcp头部 message_len 长度
#define MAX_SEND_QUEUE_LEN 4 // tcp发送队列最大长度

enum MessageType : short
{
    MESSAGE_CHAT_LOGIN
};

}
#endif //GATESERVER_CONST_H
