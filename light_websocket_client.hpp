#ifndef __LIGHT_WEBSOCKET_CLIENT_HPP__
#define __LIGHT_WEBSOCKET_CLIENT_HPP__

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <vector>
#include <string>
#ifndef _SOCKET_T_DEFINED
        typedef int socket_t;
        #define _SOCKET_T_DEFINED
#endif
#ifndef INVALID_SOCKET
        #define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
        #define SOCKET_ERROR   (-1)
#endif
#define closesocket(s) ::close(s)
#include <errno.h>
    
#define socketerrno errno
#define SOCKET_EAGAIN_EINPROGRESS EAGAIN
#define SOCKET_EWOULDBLOCK EWOULDBLOCK

using namespace std;

namespace lightws {

#define WS_ADDR_BUF_SIZE    512
#define HTTP_LINE_MAX_SIZE  1024
#define LIGHTWS_LOG         printf

typedef enum ELightWSErrType {
    LIGHTWS_NO_ERR      = 0,
    LIGHTWS_ERR_CONNECT = 1,
    LIGTHWS_ERR_HANDSHAKE=2,
    LIGHTWS_ERR_SEND    = 3,
    LIGHTWS_ERR_RECV    = 4,
    LIGHTWS_ERR_DISPATCH= 5,
    LIGHTWS_ERR_CLOSED  = 9,
    LIGHTWS_ERR_UNKNOWN = 100
}ELightWSErrType, lws_err_type_e;


typedef enum EWebsocketState {
    CLOSING,
    CLOSED,
    CONNECTING,
    OPEN
}EWebsocketState, ws_state_e;



    // http://tools.ietf.org/html/rfc6455#section-5.2  Base Framing Protocol
    //
    //  0                   1                   2                   3
    //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    // +-+-+-+-+-------+-+-------------+-------------------------------+
    // |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
    // |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
    // |N|V|V|V|       |S|             |   (if payload len==126/127)   |
    // | |1|2|3|       |K|             |                               |
    // +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
    // |     Extended payload length continued, if payload len == 127  |
    // + - - - - - - - - - - - - - - - +-------------------------------+
    // |                               |Masking-key, if MASK set to 1  |
    // +-------------------------------+-------------------------------+
    // | Masking-key (continued)       |          Payload Data         |
    // +-------------------------------- - - - - - - - - - - - - - - - +
    // :                     Payload Data continued ...                :
    // + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
    // |                     Payload Data continued ...                |
    // +---------------------------------------------------------------+
typedef struct SWsHeaderType {
    unsigned    header_size;
    bool        fin;
    bool        mask;
    enum  EOpcodeType {
        CONTINUATION    = 0x0,
        TEXT_FRAME      = 0x1,
        BINARY_FRAME    = 0x2,
        CLOSE           = 0x8,
        PING            = 0x9,
        PONG            = 0xa,
    } opcode;
    int         N0;
    uint64_t    N;
    uint8_t     masking_key[4];
}SWsHeaderType, ws_header_type_t;

class CWebsocket {
public:
    CWebsocket();
    CWebsocket(const string& url, bool use_mask);
    virtual ~CWebsocket();

    int connect_hostname();
    int close();
    int send(const string& message);
    int send_ping();
    int send_binary(const string& message);
    int send_binary(const vector<uint8_t>& messages);

    int poll(int timeout=0);
    int dispatch();

    ws_state_e get_websocket_state(); 

private:
    template<class Iterator>
    int send_data(ws_header_type_t::EOpcodeType type, uint64_t message_size,
                    Iterator message_begin, Iterator message_end);

private:
    string      m_url;
    char        m_host[WS_ADDR_BUF_SIZE];
    int         m_port;
    char        m_path[WS_ADDR_BUF_SIZE];

    bool        m_use_mask;
    ws_state_e  m_ws_state;
    socket_t    m_sock_fd;

    std::vector<uint8_t>    m_rxbuf;
    std::vector<uint8_t>    m_txbuf;
    std::vector<uint8_t>    m_recved_data;
    bool        m_is_rx_bad;
};

}

#endif  /* guard for __LIGHT_WEBSOCKET_CLIENT_HPP__ */