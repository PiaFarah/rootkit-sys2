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

        /* drain the socket until the rootkit disconnects */
        while (read(client_fd, buf, sizeof(buf)) > 0)
            ;

        timestamp();
        printf("[-] Rootkit disconnected\n");
        fflush(stdout);

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
