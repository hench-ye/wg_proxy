#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <map>
#include <vector>

#define MAX_FD 64
uv_udp_t udp_socket;
std::vector<uv_udp_t> vec_udp_out; //  0 ~ 63
std::map<int32_t, uint16_t> fd_to_id;
struct ClientIp
{
    std::string ip;
    uint16_t port;
};
std::map<uint32_t, uint16_t> map_udp_in; // client to server, client ip  -> server(uv_udp_t id)
std::map<int32_t, ClientIp> map_udp_out; // server to client, out fd -> client,

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    buf->base = (char *)malloc(suggested_size);
    buf->len = suggested_size;
}
int get_available_id(uint16_t &id)
{
    static uint16_t cur_id = 0;
    uint16_t old_id = cur_id;

    while(1) {
        uint16_t test_id = cur_id;
        for (std::map<uint32_t, uint16_t>::iterator it = map_udp_in.begin(); it != map_udp_in.end(); ++it)
        {
            if (it->second == cur_id)
            {
                if (++cur_id >= MAX_FD)
                    cur_id = 0;
                break;
            }
        }
        if (test_id == cur_id)  // find availble id
            break;
        if (old_id == cur_id)  // no available id
            return 1;
    }

    id = cur_id;
    if (++cur_id >= MAX_FD)
        cur_id = 0;
    return 0;
}
void on_send(uv_udp_send_t *req, int status)
{
    if (status)
    {
        fprintf(stderr, "send error %s\n", uv_strerror(status));
    }
    /* releases the request allocated on send_msg() */
    if (req)
        free(req);
}

void send_msg(char *msg, uint32_t len, uv_udp_t& sock)
{
  uv_buf_t buf = uv_buf_init(msg, len);

  struct sockaddr_in send_addr;
  uv_ip4_addr("192.168.50.218", 36818, &send_addr);

  uv_udp_send_t *send_req = (uv_udp_send_t *)malloc(sizeof(uv_udp_send_t));
  uv_udp_send(send_req, &sock, &buf, 1, (const struct sockaddr *)&send_addr, on_send);
}
// read from client
void on_read_client(uv_udp_t *socket, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags)
{
    if (nread < 0)
    {
        fprintf(stderr, "read error %s\n", uv_err_name(nread));
        uv_close((uv_handle_t *)socket, NULL);
    }
    else if (nread > 0)
    {
        char sender[17] = {0};
        uv_ip4_name((const struct sockaddr_in *)addr, sender, 16);
        const struct sockaddr_in *addr_temp = (const struct sockaddr_in *)addr;

        std::map<uint32_t, uint16_t>::iterator it = map_udp_in.find(addr_temp->sin_addr.s_addr);
        if (it == map_udp_in.end())
        {
            printf("recv from client %s:%d  %d,%d\n", sender, addr_temp->sin_port, (int)buf->len, nread);
            uint16_t id;
            if (get_available_id(id) != 0)
            {
                printf("get available id fail.\n");
                return;
            }
            map_udp_in[addr_temp->sin_addr.s_addr] = id;
            printf("add udp_in: %x, %d\n", addr_temp->sin_addr.s_addr, id);
            ClientIp client;
            client.ip = sender;
            client.port = ntohs(addr_temp->sin_port);
            map_udp_out[vec_udp_out[id].io_watcher.fd] = client;
            printf("add udp_out: %s, %d\n", sender, ntohs(addr_temp->sin_port));
            send_msg(buf->base, nread, vec_udp_out[id]);
        }
        else
        {
            printf("recv from client %s:%d,%d,%d, id: %d\n", sender, addr_temp->sin_port, (int)buf->len, nread, it->second);
            send_msg(buf->base, nread, vec_udp_out[it->second]);
        }
    }

    if (buf && buf->base)
    {
        /* releases the buffer allocated on alloc_buffer() */
        free(buf->base);
    }
}
//read from server
void on_read_server(uv_udp_t *socket, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags)
{
    if (nread < 0)
    {
        fprintf(stderr, "read error %s\n", uv_err_name(nread));
        uv_close((uv_handle_t *)socket, NULL);
    }
    else if (nread > 0)
    {
        char sender[17] = {0};
        uv_ip4_name((const struct sockaddr_in *)addr, sender, 16);
        printf("recv from server %s:%d,%d, id:%d,%d\n", sender, (int)buf->len, nread, socket->io_watcher.fd, fd_to_id[socket->io_watcher.fd]);
        //send_msg(buf->base, nread);
/*        struct sockaddr_in recv_addr;
        int len = 0;
        int ret = uv_udp_getsockname(socket, (struct sockaddr *)&recv_addr, &len);
        printf("recv from server:%d\n", ntohs(recv_addr.sin_port));
        uint16_t port = ntohs(recv_addr.sin_port);
*/
/*        if (vec_udp_out[0] == *socket)
            printf("0 ok");
        else printf("0 not ok");
        if (vec_udp_out[1] == *socket)
            printf("1 ok");
        else printf("1 not ok");
        */
        ///printf("id: %d,%d\n", socket->io_watcher.fd, fd_to_id[socket->io_watcher.fd]);
        uv_buf_t buf_temp = uv_buf_init(buf->base, (uint32_t)nread);
        struct sockaddr_in send_addr;
        uv_ip4_addr(map_udp_out[socket->io_watcher.fd].ip.c_str(), map_udp_out[socket->io_watcher.fd].port, &send_addr);
        uv_udp_send_t *send_req = (uv_udp_send_t *)malloc(sizeof(uv_udp_send_t));
        uv_udp_send(send_req, &udp_socket, &buf_temp, 1, (const struct sockaddr *)&send_addr, on_send);
    }

    if (buf && buf->base)
    {
        /* releases the buffer allocated on alloc_buffer() */
        free(buf->base);
    }
}
int main()
{
    uv_loop_t *loop = uv_default_loop();

    uv_udp_init(loop, &udp_socket);
    struct sockaddr_in recv_addr;
    uv_ip4_addr("0.0.0.0", 12345, &recv_addr);
    uv_udp_bind(&udp_socket, (const struct sockaddr *)&recv_addr, UV_UDP_REUSEADDR);
    uv_udp_recv_start(&udp_socket, alloc_buffer, on_read_client);

    uv_udp_t udp;
    for (int i = 0; i < MAX_FD; i++)
        vec_udp_out.push_back(udp);

    for (int i = 0; i < MAX_FD; i++)
    {
        uv_udp_init(loop, &vec_udp_out[i]);
        uv_ip4_addr("0.0.0.0", 20000 + i, &recv_addr);
        uv_udp_bind(&vec_udp_out[i], (const struct sockaddr *)&recv_addr, UV_UDP_REUSEADDR);
        uv_udp_recv_start(&vec_udp_out[i], alloc_buffer, on_read_server);
        printf("create:%d\n", vec_udp_out[i].io_watcher.fd);
        fd_to_id[vec_udp_out[i].io_watcher.fd] = i;
    }

    //  send_msg("hi there!");
    //  send_msg("hello world");

    return uv_run(loop, UV_RUN_DEFAULT);
}
