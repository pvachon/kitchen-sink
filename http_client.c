#include "http_client.h"

#include <osapi.h>
#include <os_type.h>
#include <espconn.h>

#include <stddef.h>

#define DEBUG(msg, ...) os_printf("DEBUG: " msg "\r\n", ##__VA_ARGS__)

#define BL_CONTAINER_OF(pointer, type, member) \
        ({ const __typeof__( ((type *)0)->member ) *__memb = (pointer); \
                (type *)( (char *)__memb - offsetof(type, member) ); })

static ICACHE_FLASH_ATTR
void _http_client_on_sent_cb(void *arg)
{
    struct espconn *conn = arg;
    struct http_client *client = BL_CONTAINER_OF(conn, struct http_client, conn);

    DEBUG("Bomb away!");
}

static ICACHE_FLASH_ATTR
void _http_client_on_disconnect_cb(void *arg)
{
    struct espconn *conn = arg;
    struct http_client *client = BL_CONTAINER_OF(conn, struct http_client, conn);

    client->state = HTTP_CLIENT_IDLE;

    DEBUG("HTTP client disconnected...");
}

static ICACHE_FLASH_ATTR
void _http_client_on_connect_cb(void *arg)
{
    struct espconn *conn = arg;
    struct http_client *client = BL_CONTAINER_OF(conn, struct http_client, conn);

    espconn_regist_sentcb(conn, _http_client_on_sent_cb);
    espconn_regist_disconcb(conn, _http_client_on_disconnect_cb);

    client->state = HTTP_CLIENT_CONNECTED;

    DEBUG("HTTP client is connected...");

    espconn_sent(conn, "Hello!\r\n", 8);
}

static ICACHE_FLASH_ATTR
void _http_client_on_error_cb(void *arg, sint8 err)
{
    struct espconn *conn = arg;
    struct http_client *client = BL_CONTAINER_OF(conn, struct http_client, conn);

    client->state = HTTP_CLIENT_ERROR;

    DEBUG("An error occurred while trying to connect to the server. Code: %d", (int)err);
}

static ICACHE_FLASH_ATTR
void _http_client_on_recv_cb(void *arg, char *pdata, unsigned short len)
{
    struct espconn *conn = arg;
    struct http_client *client = BL_CONTAINER_OF(conn, struct http_client, conn);

    DEBUG("Received %u bytes from remote host.\r\n", (unsigned)len);
}

ICACHE_FLASH_ATTR
int http_client_disconnect(struct http_client *client)
{
    int status = 0;

    if (NULL == client) {
        status = -1;
        goto done;
    }

    if (HTTP_CLIENT_CONNECTED == client->state ||
            HTTP_CLIENT_CONNECTING == client->state)
    {
        espconn_disconnect(&client->conn);
    }

done:
    return status;
}


ICACHE_FLASH_ATTR
int http_client_connect(struct http_client *client, uint32_t ip_addr, uint16_t port)
{
    int status = 0;
    struct espconn *conn = &client->conn;
    esp_tcp *tcp_state = &client->tcp_state;

    if (NULL == client) {
        status = -1;
        goto done;
    }

    memset(client, 0, sizeof(*client));

    conn->type = ESPCONN_TCP;
    conn->state = ESPCONN_NONE;
    conn->proto.tcp = tcp_state;
    tcp_state->local_port = espconn_port();
    tcp_state->remote_port = port;
    memcpy(&tcp_state->remote_ip, &ip_addr, 4);

    espconn_regist_connectcb(conn, _http_client_on_connect_cb);
    espconn_regist_reconcb(conn, _http_client_on_error_cb);

    client->state = HTTP_CLIENT_CONNECTING;

    DEBUG("Connecting to %x:%u", ip_addr, (unsigned)port);

    /* And we're off... */
    espconn_connect(conn);

done:
    return status;
}

static
char *_http_methods[] = {
    [HTTP_METHOD_GET] = "GET",
    [HTTP_METHOD_POST] = "POST",
    [HTTP_METHOD_PUT] = "PUT",
    [HTTP_METHOD_DELETE] = "DELETE",
};

static
char _http_buf[512];

ICACHE_FLASH_ATTR
int http_client_send_json_message(struct http_client *client, enum http_method method, const char *host, const char *resource,
        const char *message, size_t msg_len, on_response_func_t response)
{
    int status = 0;

    int offs = os_sprintf(_http_buf, "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\nContent-Type: application/json\r\n");

    if (NULL != message && 0 != msg_len) {
        memcpy(_http_buf + offs - 1, message, msg_len);
        offs += msg_len - 1;
    }

    memcpy(_http_buf + offs - 1, "\r\n", 2);

    espconn_sent(&client->conn, _http_buf, offs + 2);

    client->on_response = response;

    return status;
}

