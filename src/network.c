#include "game.h"

int startServer() {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror(RED "[!] Socket creation failed" RESET);
        return -1;
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror(RED "[!] Setsockopt failed" RESET);
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8888); 

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror(RED "[!] Bind failed (Port might be blocked or in use)" RESET);
        return -1;
    }
    if (listen(server_fd, 3) < 0) {
        perror(RED "[!] Listen failed" RESET);
        return -1;
    }

    printf(YELLOW "Hosting on Port 8888. Waiting for opponent to join...\n" RESET);
    int addrlen = sizeof(address);
    int client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    
    if (client_socket < 0) perror(RED "[!] Accept failed" RESET);
    
    close(server_fd); 
    return client_socket;
}

int connectToServer(const char* ip) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror(RED "[!] Socket creation error" RESET);
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8888);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        perror(RED "[!] Invalid address format" RESET);
        return -1;
    }
    
    printf(YELLOW "Connecting to Host at %s...\n" RESET, ip);
    
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror(RED "[!] Connection Failed" RESET);
        return -1;
    }
    return sock;
}