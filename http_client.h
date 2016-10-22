#pragma once

#include <osapi.h>
#include <os_type.h>
#include <ets_sys.h>
#include <user_interface.h>
#include <espconn.h>

#include <stdbool.h>

#include "c99_fixups.h"

struct http_client;

typedef void (*on_response_func_t)(struct http_client *client, unsigned response_code, const char *body, size_t length);

enum http_client_state {
    HTTP_CLIENT_IDLE = 0,
    HTTP_CLIENT_CONNECTING,
    HTTP_CLIENT_CONNECTED,
    HTTP_CLIENT_ERROR,
};

struct http_client {
    enum http_client_state state;
    struct espconn conn;
    esp_tcp tcp_state;
    on_response_func_t on_response;
};

enum http_method {
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
};

/**
 * Using the HTTP client, connect to the specified host IP address, on the given port. This does not
 * send any headers.
 */
int http_client_connect(struct http_client *client, uint32_t ip_addr, uint16_t port);

/**
 * Force the HTTP client to disconnect.
 */
int http_client_disconnect(struct http_client *client);

/**
 * Send a JSON message. Register a response callback that will be called when the message has been sent. If the response has
 * a body, this function will receive the response as well as the status code.
 */
int http_client_send_json_message(struct http_client *client, enum http_method method, const char *host, const char *resource,
        const char *message, size_t msg_len, on_response_func_t response);
