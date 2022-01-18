/*
 * File: light-websocket-example.cpp
 * Desc:
 * Auth: Hank.Yan(yanhongkui@yeah.net)
 * Date: 2022/1/15
 * Update: 2022/1/15
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string>

#include "light_websocket_client.hpp"

using namespace lightws;
int main(int argc, char* argv[]) {
    int ret = 0;
    
    CWebsocket* ws_cli = new CWebsocket("ws://localhost:20600/ar/9999", true);
    if (ws_cli == NULL) {
        printf("Failed to create CWebsocket\n");
    }
    ret = ws_cli->connect_hostname();
    if (ret != 0) {
        printf("Failed to connect hostname\n");
    }
    ret = ws_cli->send("{\"method\":\"hello_req\"}");
    ret = ws_cli->send("{\"method\":\"goodbye_req\"}");

    while (ws_cli->get_websocket_state() != EWebsocketState::CLOSED) {
      ws_cli->poll();
      ws_cli->dispatch();
    }
    delete ws_cli;

    return 0;
}