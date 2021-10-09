#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <uv.h>
#include <map>
#include <vector>
#include <string>

#define MAX_FD 64
#define EXPIRE_DURATION 300  // seconds
#define LISTEN_PORT 12345
#define OUT_UDP_PORT 20000
std::string WG_SERVER = "143.198.183.28";
//#define WG_SERVER "192.168.50.218"
uint16_t WG_PORT = 36818;
uv_udp_t udp_socket;
std::vector<uv_udp_t> vec_udp_out; //  0 ~ 63
std::map<int32_t, uint16_t> fd_to_id;
struct ClientIp
{
    std::string ip;
    uint16_t port;
};
struct ServerInfo {
    uint16_t uv_udp_id;
    time_t  out_time;
};
std::map<uint64_t, ServerInfo> map_udp_in; // client to server, client ip,port  -> server(uv_udp_t id)
std::map<int32_t, ClientIp> map_udp_out; // server to client, out fd -> client,

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    buf->base = (char *)malloc(suggested_size);
    buf->len = suggested_size;
}
uint64_t to_ip_port(uint32_t ip, uint16_t port) {
    return ((uint64_t)ip)<<16 | port;
}
int get_available_id(uint16_t &id)
{
    static uint16_t cur_id = 0;
    uint16_t old_id = cur_id;

    while(1) {
        uint16_t test_id = cur_id;
        for (std::map<uint64_t, ServerInfo>::iterator it = map_udp_in.begin(); it != map_udp_in.end(); ++it)
        {
            if (it->second.uv_udp_id == cur_id)
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
int clear_udp() {
    time_t timer;
    time(&timer);
    for (std::map<uint64_t, ServerInfo>::iterator it = map_udp_in.begin(); it != map_udp_in.end(); ) {
        double seconds = difftime(timer, it->second.out_time);
        if (seconds < EXPIRE_DURATION) {
            ++it;
            continue;
        }
        int32_t fd = vec_udp_out[it->second.uv_udp_id].io_watcher.fd;
        printf("erase udp: %x,%d,%d\n", it->first, it->second.uv_udp_id, fd);
        map_udp_out.erase(fd);
        it = map_udp_in.erase(it);
    }
    printf("udp size:%d\n", map_udp_in.size());
    return 0;
}

void on_send(uv_udp_send_t *req, int status)
{
    if (status)
    {
        fprintf(stderr, "send error %s\n", uv_strerror(status));
    }
    /* releases the request allocated on send_msg() */
    if (req) {
        if (req->data)
            free(req->data);
        free(req);
    }
}

void send_msg(const char* server, uint16_t port, char *msg, uint32_t len, uv_udp_t& sock)
{
  char *buffer = (char *)malloc(len);
  memcpy(buffer, msg, len);
  uv_buf_t buf = uv_buf_init(buffer, len);

  struct sockaddr_in send_addr;
  uv_ip4_addr(server, port, &send_addr);

  uv_udp_send_t *send_req = (uv_udp_send_t *)malloc(sizeof(uv_udp_send_t));
  send_req->data = (void*)buffer;
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

        uint16_t port = ntohs(addr_temp->sin_port);
        uint64_t ip_port = to_ip_port(addr_temp->sin_addr.s_addr, addr_temp->sin_port);
        std::map<uint64_t, ServerInfo>::iterator it = map_udp_in.find(ip_port);
        if (it == map_udp_in.end())
        {
            printf("recv from client %s:%d  %d,%d\n", sender, port, (int)buf->len, nread);
            uint16_t id;
            if (get_available_id(id) != 0)
            {
                printf("get available id fail.\n");
                clear_udp();
                if (buf && buf->base)
                    free(buf->base);
                return;
            }
            ServerInfo info;
            info.uv_udp_id = id;
            time(&info.out_time);
            map_udp_in[ip_port] = info;
            printf("add udp_in: %" PRIx64 ", %d\n", ip_port, id);
            ClientIp client;
            client.ip = sender;
            client.port = port;
            map_udp_out[vec_udp_out[id].io_watcher.fd] = client;
            printf("add udp_out: %s, %d\n", sender, port);

            send_msg(WG_SERVER.c_str(), WG_PORT, buf->base, nread, vec_udp_out[id]);
            if (map_udp_in.size() > MAX_FD / 2)
                clear_udp();
        }
        else
        {
            uint16_t id = it->second.uv_udp_id;
            
/*            if (port !=  map_udp_out[vec_udp_out[id].io_watcher.fd].port) {
                printf("source ip changed from %d to %d\n", map_udp_out[vec_udp_out[id].io_watcher.fd].port, port);
                map_udp_out[vec_udp_out[id].io_watcher.fd].port = port;
            } */
            //printf("recv from client %s:%d,%d,%d, id: %d\n", sender, ntohs(addr_temp->sin_port), (int)buf->len, nread, it->second.uv_udp_id);
            send_msg(WG_SERVER.c_str(), WG_PORT, buf->base, nread, vec_udp_out[it->second.uv_udp_id]);
            time(&it->second.out_time);
        }
    }

    if (buf && buf->base)
        free(buf->base);
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
        int fd = socket->io_watcher.fd;
        //printf("recv from server %s:%d,%d, id:%d,%d\n", sender, (int)buf->len, nread, fd, fd_to_id[fd]);
        if (map_udp_out.find(fd) == map_udp_out.end()) {
            if (buf && buf->base)
                free(buf->base);
            printf("not find udp session: %d\n", fd);
            return;
        }
        send_msg(map_udp_out[fd].ip.c_str(), map_udp_out[fd].port, buf->base, nread, udp_socket);
    }

    if (buf && buf->base)
        free(buf->base);
}
int main(int argc,char **argv)
{
    int ch;
    opterr = 0;
    while((ch = getopt(argc,argv, "s:p:"))!= -1)
    {
        switch(ch)
        {
            case 's': 
                printf("option server: %s\n",optarg);
                WG_SERVER = optarg;
                break;
            case 'p': 
                printf("option port: %s\n",optarg);
                WG_PORT = std::stoi(optarg);
                break;
            default: 
                break;
        }
    }

    uv_loop_t *loop = uv_default_loop();

    uv_udp_init(loop, &udp_socket);
    struct sockaddr_in recv_addr;
    uv_ip4_addr("0.0.0.0", LISTEN_PORT, &recv_addr);
    uv_udp_bind(&udp_socket, (const struct sockaddr *)&recv_addr, UV_UDP_REUSEADDR);
    uv_udp_recv_start(&udp_socket, alloc_buffer, on_read_client);

    uv_udp_t udp;
    for (int i = 0; i < MAX_FD; i++)
        vec_udp_out.push_back(udp);

    for (int i = 0; i < MAX_FD; i++)
    {
        uv_udp_init(loop, &vec_udp_out[i]);
        //uv_ip4_addr("0.0.0.0", OUT_UDP_PORT + i, &recv_addr);
        uv_ip4_addr("0.0.0.0", 0, &recv_addr);
        uv_udp_bind(&vec_udp_out[i], (const struct sockaddr *)&recv_addr, UV_UDP_REUSEADDR);
        uv_udp_recv_start(&vec_udp_out[i], alloc_buffer, on_read_server);
        struct sockaddr_in sockname;
        int namelen = sizeof(sockname);
        int r = uv_udp_getsockname(&vec_udp_out[i], (struct sockaddr*) &sockname, &namelen);
        uint16_t port = ntohs(sockname.sin_port);
        printf("create:%d, port:%d\n", vec_udp_out[i].io_watcher.fd, port);
        fd_to_id[vec_udp_out[i].io_watcher.fd] = i;
    }

    //  send_msg("hi there!");
    //  send_msg("hello world");

    return uv_run(loop, UV_RUN_DEFAULT);
}
