#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <string>
#include <vector>
#include "hashtable.h"
#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr=(ptr);\
    (type*) ((char *)__mptr - offsetof(type,member)); })
#include "hashtable.cpp"

static void msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg)
{
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static void fd_set_nb(int fd)
{
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno)
    {
        die("fcntl error");
        return;
    }
    flags = flags | O_NONBLOCK;
    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno)
    {
        die("fcntl error");
    }
}

const size_t k_max_msg = 4096;

enum
{
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,
};

struct Conn
{
    int fd = -1;
    uint32_t state = 0;
    size_t rbuf_size = 0;
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t rbuf[4 + k_max_msg];
    uint8_t wbuf[4 + k_max_msg];
};

static void conn_put(std::vector<Conn *> &fd2Conn, struct Conn *conn)
{
    if (fd2Conn.size() <= (size_t)conn->fd)
    {
        fd2Conn.resize(conn->fd + 1);
    }
    fd2Conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2Conn, int fd)
{
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0)
    {
        msg("accept() error");
        return -1;
    }
    fd_set_nb(connfd);
    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    if (!conn)
    {
        close(connfd);
        msg("malloc error");
        return -1;
    }
    conn->fd = connfd;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn->state = STATE_REQ;
    conn_put(fd2Conn, conn);
    return 0;
}

static void state_req(Conn *conn);
static void state_res(Conn *conn);

const size_t k_max_args = 1024;

static int32_t parse_req(const uint8_t *data, size_t len, std::vector<std::string> &out)
{
    if (len < 4)
        return -1;
    uint32_t n = 0;
    memcpy(&n, &data[0], 4);
    if (n > k_max_args)
        return -1;
    size_t pos = 4;
    while (n--)
    {
        if (pos + 4 > len)
            return -1;
        uint32_t sz = 0;
        memcpy(&sz, &data[pos], 4);
        if (pos + 4 + sz > len)
            return -1;
        out.push_back(std::string((char *)&data[pos + 4], sz));
        pos += 4 + sz;
    }
    if (pos != len)
        return -1;
    return 0;
}

enum
{
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};

static struct
{
    HMap db;
} g_data;

struct Entry
{
    struct HNode node;
    std::string key;
    std::string val;
};

static bool entry_eq(HNode *lhs, HNode *rhs)
{
    struct Entry *le = container_of(lhs, struct Entry, node);
    struct Entry *re = container_of(rhs, struct Entry, node);
    return le->key == re->key;
}

static uint64_t str_hash(const uint8_t *data, size_t len)
{
    uint32_t h = 0x811C9DC5;
    for (size_t i = 0; i < len; i++)
    {
        h = (h + data[i]) * 0x01000193;
    }
    return h;
}

// static uint32_t do_get(const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
// {
//     if (!g_map.count(cmd[1]))
//         return RES_NX;
//     std::string &val = g_map[cmd[1]];
//     assert(val.size() <= k_max_msg);
//     memcpy(res, val.data(), val.size());
//     *reslen = (uint32_t)val.size();
//     return RES_OK;
// }

enum
{
    ERR_UNKNOWN = 1,
    ERR_2BIG = 2,
};

enum
{
    SER_NIL = 0,
    SER_ERR = 1,
    SER_STR = 2,
    SER_INT = 3,
    SER_ARR = 4,
};

static void out_nil(std::string &out)
{
    out.push_back(SER_NIL);
}

static void out_str(std::string &out, const std::string &val)
{
    out.push_back(SER_STR);
    uint32_t len = val.size();
    out.append((char *)&len, 4);
    out.append(val);
}

static void out_int(std::string &out, int64_t val)
{
    out.push_back(SER_INT);
    out.append((char *)&val, 8);
}

static void out_err(std::string &out, int32_t code, const std::string &msg)
{
    out.push_back(SER_ERR);
    out.append((char *)&code, 4);
    uint32_t len = msg.size();
    out.append((char *)&len, 4);
    out.append(msg);
}

static void out_arr(std::string &out, uint32_t n)
{
    out.push_back(SER_ARR);
    out.append((char *)&n, 4);
}

static void do_get(std::vector<std::string> &cmd, std::string &out)
{
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    // if (!node)
    //     return RES_NX;
    // const std::string &val = container_of(node, Entry, node)->val;
    // assert(val.size() <= k_max_msg);
    // memcpy(res, val.data(), val.size());
    // *reslen = (uint32_t)val.size();
    // return RES_OK;
    if (!node)
        return out_nil(out);
    const std::string &val = container_of(node, Entry, node)->val;
    out_str(out, val);
}

static void do_set(std::vector<std::string> &cmd, std::string &out)
{
    // (void)res;
    // (void)reslen;
    // g_map[cmd[1]] = cmd[2];
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (node)
    {
        container_of(node, Entry, node)->val.swap(cmd[2]);
    }
    else
    {
        Entry *ent = new Entry();
        ent->key.swap(key.key);
        ent->node.hcode = key.node.hcode;
        ent->val.swap(cmd[2]);
        hm_insert(&g_data.db, &ent->node);
    }
    return out_nil(out);
}

static void do_del(std::vector<std::string> &cmd, std::string &out)
{
    // (void)res;
    // (void)reslen;
    // g_map.erase(cmd[1]);
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);
    if (node)
    {
        delete container_of(node, Entry, node);
    }
    return out_int(out, node?1:0);
}

static void h_scan(HTab *tab, void (*f)(HNode *, void *), void *arg)
{
    if (tab->size == 0)
        return;
    for (size_t i = 0; i < tab->mask + 1; i++)
    {
        HNode *node = tab->tab[i];
        while (node)
        {
            f(node, arg);
            node = node->next;
        }
    }
}

static void cb_scan(HNode *node, void *arg)
{
    std::string &out = *(std::string *)arg;
    out_str(out, container_of(node, Entry, node)->key);
}

static void do_keys(std::vector<std::string> &cmd, std::string &out)
{
    (void)cmd;
    out_arr(out, (uint32_t)hm_size(&g_data.db));
    h_scan(&g_data.db.ht1, &cb_scan, &out);
    h_scan(&g_data.db.ht2, &cb_scan, &out);
}

static bool cmd_is(const std::string &word, const char *cmd)
{
    return 0 == strcasecmp(word.c_str(), cmd);
}

// static int32_t do_request(const uint8_t *req, uint32_t reqlen, uint32_t *rescode, uint8_t *res, uint32_t *reslen)
// {
//     std::vector<std::string> cmd;
//     if (0 != parse_req(req, reqlen, cmd))
//     {
//         msg("bad request");
//         return -1;
//     }
//     if (cmd.size() == 2 && cmd_is(cmd[0], "get"))
//         *rescode = do_get(cmd, res, reslen);
//     else if (cmd.size() == 3 && cmd_is(cmd[0], "set"))
//         *rescode = do_set(cmd, res, reslen);
//     else if (cmd.size() == 2 && cmd_is(cmd[0], "del"))
//         *rescode = do_del(cmd, res, reslen);
//     else
//     {
//         *rescode = RES_ERR;
//         const char *msg = "unknown command";
//         strcpy((char *)res, msg);
//         *reslen = strlen(msg);
//         return 0;
//     }
//     return 0;
// }

static void do_request(std::vector<std::string> &cmd, std::string &out)
{
    if (cmd.size() == 1 && cmd_is(cmd[0], "keys"))
        do_keys(cmd, out);
    else if (cmd.size() == 2 && cmd_is(cmd[0], "get"))
        do_get(cmd, out);
    else if (cmd.size() == 3 && cmd_is(cmd[0], "set"))
        do_set(cmd, out);
    else if (cmd.size() == 2 && cmd_is(cmd[0], "del"))
        do_del(cmd, out);
    else
        out_err(out, ERR_UNKNOWN, "Unkown cmd");
}

static bool try_one_request(Conn *conn)
{
    if (conn->rbuf_size < 4)
    {
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg)
    {
        msg("too long msg");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size)
    {
        return false;
    }
    // uint32_t rescode = 0;
    // uint32_t wlen = 0;
    std::vector<std::string> cmd;
    if (0 != parse_req(&conn->rbuf[4], len, cmd))
    {
        msg("bad req");
        conn->state = STATE_END;
        return false;
    }
    std::string out;
    do_request(cmd, out);
    if (4 + out.size() > k_max_msg)
    {
        out.clear();
        out_err(out, ERR_2BIG, "response is too big");
    }
    uint32_t wlen = (uint32_t)out.size();
    // printf("Client says:%.*s\n", len, &conn->rbuf[4]);
    // wlen += 4;
    // memcpy(&conn->wbuf[0], &len, 4);
    // memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
    // conn->wbuf_size = 4 + len;
    memcpy(&conn->wbuf[0], &wlen, 4);
    memcpy(&conn->wbuf[4], out.data(), out.size());
    conn->wbuf_size = 4 + wlen;
    size_t remain = conn->rbuf_size - 4 - len;
    if (remain)
    {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;
    conn->state = STATE_RES;
    state_res(conn);
    return (conn->state == STATE_REQ);
}

static bool try_fill_buffer(Conn *conn)
{
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t rv = 0;
    do
    {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
    } while (rv < 0 && errno == EINTR);
    if (rv < 0 && errno == EAGAIN)
        return false;
    if (rv < 0)
    {
        msg("read() error");
        conn->state = STATE_END;
        return false;
    }
    if (rv == 0)
    {
        if (conn->rbuf_size > 0)
            msg("unexpected EOF");
        else
            msg("EOF");
        conn->state = STATE_END;
        return false;
    }
    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));
    while (try_one_request(conn))
    {
    }
    return (conn->state = STATE_REQ);
}

static void state_req(Conn *conn)
{
    while (try_fill_buffer(conn))
    {
    }
}

static bool try_flush_buffer(Conn *conn)
{
    ssize_t rv = 0;
    do
    {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
    } while (rv < 0 && errno == EINTR);

    if (rv < 0 && errno == EAGAIN)
    {
        return false;
    }
    if (rv < 0)
    {
        msg("write() error");
        conn->state = STATE_END;
        return false;
    }
    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent <= conn->wbuf_size);
    if (conn->wbuf_sent == conn->wbuf_size)
    {
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    return true;
}

static void state_res(Conn *conn)
{
    while (try_flush_buffer(conn))
    {
    }
}

static void connection_io(Conn *conn)
{
    if (conn->state == STATE_RES)
        state_res(conn);
    else if (conn->state == STATE_REQ)
        state_req(conn);
    else
        assert(0);
}
// static void do_something(int connfd)
// {
//     char rbuff[64];
//     ssize_t n = read(connfd, rbuff, sizeof(rbuff) - 1);

//     if (n < 0)
//     {
//         msg("read() error");
//         return;
//     }

//     printf("client says:%s\n", rbuff);

//     char wbuff[] = "World";
//     write(connfd, wbuff, sizeof(wbuff));
// }

// static int32_t read_full(int fd, char *buf, size_t n)
// {
//     while (n > 0)
//     {
//         ssize_t rv = read(fd, buf, n);
//         if (rv <= 0)
//             return -1;
//         assert((size_t)rv <= n);
//         n -= (size_t)rv;
//         buf += rv;
//     }
//     return 0;
// }

// static int32_t write_all(int fd, const char *buf, size_t n)
// {
//     while (n > 0)
//     {
//         ssize_t rv = write(fd, buf, n);
//         if (rv <= 0)
//             return -1;
//         assert((size_t)rv <= n);
//         n -= (size_t)rv;
//         buf += rv;
//     }
//     return 0;
// }

// static int32_t one_request(int connfd)
// {
//     char rbuf[4 + k_max_msg + 1];
//     errno = 0;
//     int32_t err = read_full(connfd, rbuf, 4);
//     if (err)
//     {
//         if (errno == 0)
//             msg("EOF");
//         else
//             msg("read() error");
//         return err;
//     }
//     uint32_t len = 0;
//     memcpy(&len, rbuf, 4);

//     if (len > k_max_msg)
//     {
//         msg("too long");
//         return -1;
//     }
//     err = read_full(connfd, &rbuf[4], len);
//     if (err)
//     {
//         msg("read() error");
//         return err;
//     }
//     rbuf[4 + len] = '\0';
//     printf("client says:%s\n", &rbuf[4]);
//     const char reply[] = "World";
//     char wbuf[4 + sizeof(reply)];
//     len = (uint32_t)strlen(reply);
//     memcpy(wbuf, &len, 4);
//     memcpy(&wbuf[4], &reply, len);
//     return write_all(connfd, wbuf, 4 + len);
// }

int main()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        die("socket()");

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0);

    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
    if (rv < 0)
        die("bind()");

    rv = listen(fd, SOMAXCONN);
    if (rv < 0)
        die("listen()");
    std::vector<Conn *> fd2conn;
    fd_set_nb(fd);
    std::vector<struct pollfd> poll_args;
    // while (true)
    // {
    //     struct sockaddr_in client = {};
    //     socklen_t socklen = sizeof(client);

    //     int connfd = accept(fd, (struct sockaddr *)&client, &socklen);
    //     if (connfd < 0)
    //         continue;

    //     while (true)
    //     {
    //         int32_t err = one_request(connfd);
    //         if (err)
    //             break;
    //     }
    //     // do_something(connfd);
    //     close(connfd);
    // }
    while (true)
    {
        poll_args.clear();
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);
        for (Conn *conn : fd2conn)
        {
            if (!conn)
                continue;
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
        if (rv < 0)
            die("poll");
        for (size_t i = 1; i < poll_args.size(); i++)
        {
            if (poll_args[i].revents)
            {
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn);
                if (conn->state == STATE_END)
                {
                    fd2conn[conn->fd] = nullptr;
                    (void)close(conn->fd);
                    free(conn);
                }
            }
        }
        if (poll_args[0].revents)
        {
            (void)accept_new_conn(fd2conn, fd);
        }
    }

    return 0;
}