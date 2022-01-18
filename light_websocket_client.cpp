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

#include "light_websocket_client.hpp"

namespace lightws {

CWebsocket::CWebsocket() {
    memset(m_host, 0, WS_ADDR_BUF_SIZE);
    m_port = 0;
    memset(m_path, 0, WS_ADDR_BUF_SIZE);

    m_use_mask = 0;
    m_ws_state = CLOSED;
    m_sock_fd  = 0;
}


CWebsocket::CWebsocket(const string& url, bool use_mask) {
    if (url.size() >= WS_ADDR_BUF_SIZE) {
        LIGHTWS_LOG("[ERR]url size limit exceeded: %s\n", url.c_str());
        return;
    }
    m_url = url;

    if (sscanf(url.c_str(), "ws://%[^:/]:%d/%s", m_host, &m_port, m_path) == 3) {
    }
    else if (sscanf(url.c_str(), "ws://%[^:/]/%s", m_host, m_path) == 2) {
        m_port = 80;
    }
    else if (sscanf(url.c_str(), "ws://%[^:/]:%d", m_host, &m_port) == 2) {
        m_path[0] = '\0';
    }
    else if (sscanf(url.c_str(), "ws://%[^:/]", m_host) == 1) {
        m_port = 80;
        m_path[0] = '\0';
    } 
    else {
        LIGHTWS_LOG("[ERR]Could not parse WebSocket url: %s\n", url.c_str());
        return;
    }
    LIGHTWS_LOG("[INF]Parsed url: host=%s, port=%d, path=%s\n",
                    m_host, m_port, m_path);
    m_use_mask  = use_mask;
    m_ws_state  = OPEN;
    m_is_rx_bad = false;
}

CWebsocket::~CWebsocket() {
    if (m_ws_state == OPEN) {
        close();
    }
}

int CWebsocket::connect_hostname() {
    struct  addrinfo    hints;
    struct  addrinfo*   result;
    struct  addrinfo*   p;
    int     ret = 0;
    std::string  origin;

    m_sock_fd   = INVALID_SOCKET;
    char    sport[16];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family     = AF_UNSPEC;
    hints.ai_socktype   = SOCK_STREAM;
    snprintf(sport, 16, "%d", m_port);
    ret = getaddrinfo(m_host, sport, &hints, &result);
    if (ret != 0) {
        LIGHTWS_LOG("[ERR]Failed to getaddrinfo:%s\n", gai_strerror(ret));
        return LIGHTWS_ERR_CONNECT;
    }

    for (p = result; p != NULL; p = p->ai_next) {
        m_sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (m_sock_fd == INVALID_SOCKET) {
            continue;
        }
        ret = connect(m_sock_fd, p->ai_addr, p->ai_addrlen);
        if (ret != SOCKET_ERROR) {
            break;
        }
        closesocket(m_sock_fd);
        m_sock_fd   = INVALID_SOCKET;
    }
    freeaddrinfo(result);
    
    if (m_sock_fd == INVALID_SOCKET) {
        LIGHTWS_LOG("[ERR]Unable to connect to %s:%d\n", m_host, m_port);
        return LIGHTWS_ERR_CONNECT;
    }

    // Websocket handshake
    {
        char line[1024];
        int status;
        int i;
        snprintf(line, 1024, "GET /%s HTTP/1.1\r\n", m_path); 
        ::send(m_sock_fd, line, strlen(line), 0);
        if (m_port == 80) {
            snprintf(line, 1024, "Host: %s\r\n", m_host); 
            ::send(m_sock_fd, line, strlen(line), 0);
        }
        else {
            snprintf(line, 1024, "Host: %s:%d\r\n", m_host, m_port); 
            ::send(m_sock_fd, line, strlen(line), 0);
        }
        snprintf(line, 1024, "Upgrade: websocket\r\n"); 
        ::send(m_sock_fd, line, strlen(line), 0);
        snprintf(line, 1024, "Connection: Upgrade\r\n"); 
        ::send(m_sock_fd, line, strlen(line), 0);

        if (!origin.empty()) {
            snprintf(line, 1024, "Origin: %s\r\n", origin.c_str()); 
            ::send(m_sock_fd, line, strlen(line), 0);
        }
        snprintf(line, 1024, "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"); 
        ::send(m_sock_fd, line, strlen(line), 0);
        snprintf(line, 1024, "Sec-WebSocket-Version: 13\r\n"); 
        ::send(m_sock_fd, line, strlen(line), 0);
        snprintf(line, 1024, "\r\n"); 
        ::send(m_sock_fd, line, strlen(line), 0);

        for (i = 0; i < 2 || (i < 1023 && line[i-2] != '\r' && line[i-1] != '\n'); ++i) { 
            if (recv(m_sock_fd, line+i, 1, 0) == 0) { 
                return LIGTHWS_ERR_HANDSHAKE; 
            } 
        }
        line[i] = 0;
        if (i == 1023) { 
            LIGHTWS_LOG("ERROR: Got invalid status line connecting to: %s\n", m_url.c_str()); 
            return LIGTHWS_ERR_HANDSHAKE; 
        }
        if (sscanf(line, "HTTP/1.1 %d", &status) != 1 || status != 101) { 
            LIGHTWS_LOG("ERROR: Got bad status connecting to %s: %s", m_url.c_str(), line); 
            return LIGTHWS_ERR_HANDSHAKE; 
        }
        // TODO: verify response headers,
        while (true) {
            for (i = 0; i < 2 || (i < 1023 && line[i-2] != '\r' && line[i-1] != '\n'); ++i) { 
                if (recv(m_sock_fd, line+i, 1, 0) == 0) { 
                    return LIGTHWS_ERR_HANDSHAKE; 
                }
            }
            if (line[0] == '\r' && line[1] == '\n') { 
                break; 
            }
        }
    }

    int flag = 1;
    // Disable Nagle's algorithm
    setsockopt(m_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char*) &flag, sizeof(flag)); 
    fcntl(m_sock_fd, F_SETFL, O_NONBLOCK);

    LIGHTWS_LOG("Connected to: %s\n", m_url.c_str());
    return LIGHTWS_NO_ERR;
}

int CWebsocket::close() {
    int ret = LIGHTWS_NO_ERR;

    if ((m_ws_state == CLOSING) || (m_ws_state == CLOSED)) {
        return LIGHTWS_NO_ERR;
    }
    m_ws_state = CLOSING;
    // last 4 bytes are a masking key
    uint8_t closeFrame[6] = {0x88, 0x80, 0x00, 0x00, 0x00, 0x00}; 
    std::vector<uint8_t> header(closeFrame, closeFrame+6);
    m_txbuf.insert(m_txbuf.end(), header.begin(), header.end());   

    return ret;
}


int CWebsocket::send(const std::string& message) {
    int ret = LIGHTWS_NO_ERR;
    ret = send_data(ws_header_type_t::EOpcodeType::TEXT_FRAME, message.size(), 
                    message.begin(), message.end());
    return ret;
}

int CWebsocket::send_ping() {
    int ret = LIGHTWS_NO_ERR;
    std::string     empty;
    ret = send_data(ws_header_type_t::PING, empty.size(),
                    empty.begin(), empty.end());
    return ret;
}

int CWebsocket::send_binary(const std::string& message) {
    int ret = LIGHTWS_NO_ERR;
    ret = send_data(ws_header_type_t::BINARY_FRAME, message.size(),
                    message.begin(), message.end());
    return ret;
}

int CWebsocket::send_binary(const std::vector<uint8_t>& message) {
    int ret = LIGHTWS_NO_ERR;
    ret = send_data(ws_header_type_t::BINARY_FRAME, message.size(),
                    message.begin(), message.end());
    return ret;
}


template<class Iterator>
int CWebsocket::send_data(ws_header_type_t::EOpcodeType type, uint64_t message_size, 
                Iterator message_begin, Iterator message_end) {
    int ret = LIGHTWS_NO_ERR;
    // TODO:
    // Masking key should (must) be derived from a high quality random
    // number generator, to mitigate attacks on non-WebSocket friendly
    // middleware:
    const uint8_t masking_key[4] = { 0x12, 0x34, 0x56, 0x78 };

    // TODO: consider acquiring a lock on txbuf...
    if (m_ws_state == CLOSING || m_ws_state == CLOSED) { 
        return LIGHTWS_ERR_CLOSED; 
    }

    std::vector<uint8_t> header;
    header.assign(2 + (message_size >= 126 ? 2 : 0) + (message_size >= 65536 ? 6 : 0) + (m_use_mask ? 4 : 0), 0);
    header[0] = 0x80 | type;
    if (message_size < 126) {
        header[1] = (message_size & 0xff) | (m_use_mask ? 0x80 : 0);
        if (m_use_mask) {
            header[2] = masking_key[0];
            header[3] = masking_key[1];
            header[4] = masking_key[2];
            header[5] = masking_key[3];
        }
    }
    else if (message_size < 65536) {
        header[1] = 126 | (m_use_mask ? 0x80 : 0);
        header[2] = (message_size >> 8) & 0xff;
        header[3] = (message_size >> 0) & 0xff;
        if (m_use_mask) {
            header[4] = masking_key[0];
            header[5] = masking_key[1];
            header[6] = masking_key[2];
            header[7] = masking_key[3];
        }
    }
    else { // TODO: run coverage testing here
        header[1] = 127 | (m_use_mask ? 0x80 : 0);
        header[2] = (message_size >> 56) & 0xff;
        header[3] = (message_size >> 48) & 0xff;
        header[4] = (message_size >> 40) & 0xff;
        header[5] = (message_size >> 32) & 0xff;
        header[6] = (message_size >> 24) & 0xff;
        header[7] = (message_size >> 16) & 0xff;
        header[8] = (message_size >>  8) & 0xff;
        header[9] = (message_size >>  0) & 0xff;
        if (m_use_mask) {
            header[10] = masking_key[0];
            header[11] = masking_key[1];
            header[12] = masking_key[2];
            header[13] = masking_key[3];
        }
    }
    
    // N.B. - txbuf will keep growing until it can be transmitted over the socket:
    m_txbuf.insert(m_txbuf.end(), header.begin(), header.end());
    m_txbuf.insert(m_txbuf.end(), message_begin, message_end);
    if (m_use_mask) {
        size_t message_offset = m_txbuf.size() - message_size;
        for (size_t i = 0; i != message_size; ++i) {
            m_txbuf[message_offset + i] ^= masking_key[i&0x3];
        }
    }

    return ret;
}


int CWebsocket::poll(int timeout) {
    int ret = LIGHTWS_NO_ERR;    

    if (m_ws_state == CLOSED) {
        if (timeout > 0) {
            timeval tv = { timeout/1000, (timeout%1000) * 1000 };
            select(0, NULL, NULL, NULL, &tv);    
        }
    }

    if (timeout != 0) {
        fd_set rfds;
        fd_set wfds;
        timeval tv = { timeout/1000, (timeout%1000) * 1000 };
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_SET(m_sock_fd, &rfds);
        if (m_txbuf.size()) { FD_SET(m_sock_fd, &wfds); }
        select(m_sock_fd + 1, &rfds, &wfds, 0, timeout > 0 ? &tv : 0);
    }

    // 接收服务端的response
    while (true) {
        // FD_ISSET(0, &rfds) will be true
        int N = m_rxbuf.size();
        ssize_t ret;
        m_rxbuf.resize(N + 1500);
        ret = recv(m_sock_fd, (char*)&m_rxbuf[0] + N, 1500, 0);
        if (false) { }
        else if (ret < 0 && (socketerrno == SOCKET_EWOULDBLOCK || socketerrno == SOCKET_EAGAIN_EINPROGRESS)) {
            m_rxbuf.resize(N);
            break;
        }
        else if (ret <= 0) {
            m_rxbuf.resize(N);
            closesocket(m_sock_fd);
            m_ws_state = CLOSED;
            fputs(ret < 0 ? "Connection error!\n" : "Connection closed!\n", stderr);
            break;
        }
        else {
            m_rxbuf.resize(N + ret);
        }
    }

    // 发送request
    while (m_txbuf.size()) {
        int ret = ::send(m_sock_fd, (char*)&m_txbuf[0], m_txbuf.size(), 0);
        if (false) { } //
        else if (ret < 0 && (socketerrno == SOCKET_EWOULDBLOCK || socketerrno == SOCKET_EAGAIN_EINPROGRESS)) {
            break;
        }
        else if (ret <= 0) {
            closesocket(m_sock_fd);
            m_ws_state = CLOSED;
            fputs(ret < 0 ? "Connection error!\n" : "Connection closed!\n", stderr);
            break;
        }
        else {
            m_txbuf.erase(m_txbuf.begin(), m_txbuf.begin() + ret);
        }
    }
    if (!m_txbuf.size() && m_ws_state == CLOSING) {
        closesocket(m_sock_fd);
        m_ws_state = CLOSED;
    }

    return ret;
}


int CWebsocket::dispatch() {
    // TODO: consider acquiring a lock on rxbuf...
    if (m_is_rx_bad) {
        return LIGHTWS_NO_ERR;
    }
    while (true) {
        ws_header_type_t ws;
        if (m_rxbuf.size() < 2) { 
            return LIGHTWS_ERR_DISPATCH; /* Need at least 2 */ 
        }

        const uint8_t * data = (uint8_t *) &m_rxbuf[0]; // peek, but don't consume
        ws.fin = (data[0] & 0x80) == 0x80;
        ws.opcode = (ws_header_type_t::EOpcodeType) (data[0] & 0x0f);
        ws.mask = (data[1] & 0x80) == 0x80;
        ws.N0 = (data[1] & 0x7f);
        ws.header_size = 2 + (ws.N0 == 126? 2 : 0) + (ws.N0 == 127? 8 : 0) + (ws.mask? 4 : 0);
        if (m_rxbuf.size() < ws.header_size) { 
            return LIGHTWS_ERR_DISPATCH; /* Need: ws.header_size - rxbuf.size() */ 
        }
        int i = 0;
        if (ws.N0 < 126) {
            ws.N = ws.N0;
            i = 2;
        }
        else if (ws.N0 == 126) {
            ws.N = 0;
            ws.N |= ((uint64_t) data[2]) << 8;
            ws.N |= ((uint64_t) data[3]) << 0;
            i = 4;
        }
        else if (ws.N0 == 127) {
            ws.N = 0;
            ws.N |= ((uint64_t) data[2]) << 56;
            ws.N |= ((uint64_t) data[3]) << 48;
            ws.N |= ((uint64_t) data[4]) << 40;
            ws.N |= ((uint64_t) data[5]) << 32;
            ws.N |= ((uint64_t) data[6]) << 24;
            ws.N |= ((uint64_t) data[7]) << 16;
            ws.N |= ((uint64_t) data[8]) << 8;
            ws.N |= ((uint64_t) data[9]) << 0;
            i = 10;
            if (ws.N & 0x8000000000000000ull) {
                // https://tools.ietf.org/html/rfc6455 writes the "the most
                // significant bit MUST be 0."
                //
                // We can't drop the frame, because (1) we don't we don't
                // know how much data to skip over to find the next header,
                // and (2) this would be an impractically long length, even
                // if it were valid. So just close() and return immediately
                // for now.
                m_is_rx_bad = true;
                LIGHTWS_LOG("ERROR: Frame has invalid frame length. Closing.\n");
                close();
                return LIGHTWS_ERR_DISPATCH;
            }
        }
        if (ws.mask) {
            ws.masking_key[0] = ((uint8_t) data[i+0]) << 0;
            ws.masking_key[1] = ((uint8_t) data[i+1]) << 0;
            ws.masking_key[2] = ((uint8_t) data[i+2]) << 0;
            ws.masking_key[3] = ((uint8_t) data[i+3]) << 0;
        }
        else {
            ws.masking_key[0] = 0;
            ws.masking_key[1] = 0;
            ws.masking_key[2] = 0;
            ws.masking_key[3] = 0;
        }

        // Note: The checks above should hopefully ensure this addition
        //       cannot overflow:
        if (m_rxbuf.size() < ws.header_size+ws.N) { 
            return LIGHTWS_ERR_DISPATCH; /* Need: ws.header_size+ws.N - rxbuf.size() */ 
        }

        // We got a whole message, now do something with it:
        if (ws.opcode == ws_header_type_t::EOpcodeType::TEXT_FRAME 
            || ws.opcode == ws_header_type_t::EOpcodeType::BINARY_FRAME
            || ws.opcode == ws_header_type_t::EOpcodeType::CONTINUATION) {
            if (ws.mask) { 
                for (size_t i = 0; i != ws.N; ++i) { 
                    m_rxbuf[i+ws.header_size] ^= ws.masking_key[i&0x3]; 
                } 
            }
            m_recved_data.insert(m_recved_data.end(),   m_rxbuf.begin()+ws.header_size, 
                                m_rxbuf.begin()+ws.header_size+(size_t)ws.N);// just feed
            if (ws.fin) {
                //callable((const std::vector<uint8_t>) m_recved_data);
                std::string stringMessage(m_recved_data.begin(), m_recved_data.end());
                printf(">>> %s\n", stringMessage.c_str());
                
                m_recved_data.erase(m_recved_data.begin(), m_recved_data.end());
                std::vector<uint8_t> ().swap(m_recved_data);// free memory
            }
        }
        else if (ws.opcode == ws_header_type_t::EOpcodeType::PING) {
            if (ws.mask) { 
                for (size_t i = 0; i != ws.N; ++i) { 
                    m_rxbuf[i+ws.header_size] ^= ws.masking_key[i&0x3]; 
                } 
            }
            std::string data(m_rxbuf.begin()+ws.header_size, m_rxbuf.begin()+ws.header_size+(size_t)ws.N);
            send_data(ws_header_type_t::PONG, data.size(), data.begin(), data.end());
        }
        else if (ws.opcode == ws_header_type_t::EOpcodeType::PONG) { 

        }
        else if (ws.opcode == ws_header_type_t::EOpcodeType::CLOSE) { 
            close(); 
        }
        else { 
            fprintf(stderr, "ERROR: Got unexpected WebSocket message.\n"); 
            close(); 
        }

        m_rxbuf.erase(m_rxbuf.begin(), m_rxbuf.begin() + ws.header_size+(size_t)ws.N);
    }
}

ws_state_e CWebsocket::get_websocket_state(){
    return m_ws_state;
}

}