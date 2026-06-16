#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <ctype.h>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024
#define PORT 8100

typedef struct {
    int fd;
    char nick[50];
    int is_joined;
} Client;

Client clients[MAX_CLIENTS];
char current_topic[256] = "";
char current_op[50] = "";

void send_to_client(int fd, const char *msg) {
    send(fd, msg, strlen(msg), 0);
}

void broadcast(const char *msg, int exclude_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].is_joined && clients[i].fd != exclude_fd && clients[i].fd != 0) {
            send_to_client(clients[i].fd, msg);
        }
    }
}

int is_valid_nick(const char *nick) {
    if (strlen(nick) == 0) return 0;
    for (int i = 0; nick[i]; i++) {
        if (!islower(nick[i]) && !isdigit(nick[i])) return 0;
    }
    return 1;
}

int is_nick_used(const char *nick) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].is_joined && strcmp(clients[i].nick, nick) == 0) return 1;
    }
    return 0;
}

int get_client_by_nick(const char *nick) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].is_joined && strcmp(clients[i].nick, nick) == 0) return clients[i].fd;
    }
    return -1;
}

void remove_client(int i, int silent) {
    if (clients[i].is_joined) {
        if (!silent) {
            char buf[256];
            sprintf(buf, "QUIT %s\n", clients[i].nick);
            broadcast(buf, clients[i].fd);
        }
        if (strcmp(clients[i].nick, current_op) == 0) {
            memset(current_op, 0, sizeof(current_op));
        }
    }
    close(clients[i].fd);
    clients[i].fd = 0;
    clients[i].is_joined = 0;
    memset(clients[i].nick, 0, sizeof(clients[i].nick));
}

void handle_client_message(int client_idx, char *buffer) {
    int fd = clients[client_idx].fd;
    char *cmd = strtok(buffer, " \r\n");
    if (!cmd) return;

    if (strcmp(cmd, "JOIN") == 0) {
        char *nick = strtok(NULL, " \r\n");
        if (!nick || !is_valid_nick(nick)) {
            send_to_client(fd, "201 INVALID NICK NAME\n");
            return;
        }
        if (is_nick_used(nick)) {
            send_to_client(fd, "200 NICKNAME IN USE\n");
            return;
        }
        
        strcpy(clients[client_idx].nick, nick);
        clients[client_idx].is_joined = 1;
        
        if (strlen(current_op) == 0) {
            strcpy(current_op, nick);
        }

        send_to_client(fd, "100 OK\n");
        if (strlen(current_topic) > 0) {
            char t_msg[512];
            sprintf(t_msg, "TOPIC %s %s\n", current_op, current_topic);
            send_to_client(fd, t_msg);
        }

        char b_msg[256];
        sprintf(b_msg, "JOIN %s\n", nick);
        broadcast(b_msg, fd);

    } else if (!clients[client_idx].is_joined) {
        send_to_client(fd, "999 UNKNOWN ERROR\n");

    } else if (strcmp(cmd, "MSG") == 0) {
        char *msg = strtok(NULL, "\r\n");
        if (!msg) {
            send_to_client(fd, "999 UNKNOWN ERROR\n");
            return;
        }
        send_to_client(fd, "100 OK\n");
        char b_msg[1024];
        sprintf(b_msg, "MSG %s %s\n", clients[client_idx].nick, msg);
        broadcast(b_msg, fd);

    } else if (strcmp(cmd, "PMSG") == 0) {
        char *target = strtok(NULL, " ");
        char *msg = strtok(NULL, "\r\n");
        if (!target || !msg) {
            send_to_client(fd, "999 UNKNOWN ERROR\n");
            return;
        }
        int target_fd = get_client_by_nick(target);
        if (target_fd == -1) {
            send_to_client(fd, "202 UNKNOWN NICKNAME\n");
            return;
        }
        send_to_client(fd, "100 OK\n");
        char p_msg[1024];
        sprintf(p_msg, "PMSG %s %s\n", clients[client_idx].nick, msg);
        send_to_client(target_fd, p_msg);

    } else if (strcmp(cmd, "OP") == 0) {
        char *target = strtok(NULL, " \r\n");
        if (strcmp(clients[client_idx].nick, current_op) != 0) {
            send_to_client(fd, "203 DENIED\n");
            return;
        }
        if (!target || get_client_by_nick(target) == -1) {
            send_to_client(fd, "202 UNKNOWN NICKNAME\n");
            return;
        }
        strcpy(current_op, target);
        send_to_client(fd, "100 OK\n");
        char b_msg[256];
        sprintf(b_msg, "OP %s\n", target);
        broadcast(b_msg, fd);	

    } else if (strcmp(cmd, "KICK") == 0) {
        char *target = strtok(NULL, " \r\n");
        if (strcmp(clients[client_idx].nick, current_op) != 0) {
            send_to_client(fd, "203 DENIED\n");
            return;
        }
        if (!target || get_client_by_nick(target) == -1) {
            send_to_client(fd, "202 UNKNOWN NICKNAME\n");
            return;
        }
        send_to_client(fd, "100 OK\n");
        char b_msg[256];
        sprintf(b_msg, "KICK %s %s\n", target, current_op);
        broadcast(b_msg, fd);
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].is_joined && strcmp(clients[i].nick, target) == 0) {
                remove_client(i, 1);	
                break;
            }
        }

    } else if (strcmp(cmd, "TOPIC") == 0) {
        if (strcmp(clients[client_idx].nick, current_op) != 0) {
            send_to_client(fd, "203 DENIED\n");
            return;
        }
        char *topic = strtok(NULL, "\r\n");
        if (topic) strcpy(current_topic, topic);
        else strcpy(current_topic, "");
        
        send_to_client(fd, "100 OK\n");
        char b_msg[512];
        sprintf(b_msg, "TOPIC %s %s\n", current_op, current_topic);
        broadcast(b_msg, fd);

    } else if (strcmp(cmd, "QUIT") == 0) {
        send_to_client(fd, "100 OK\n");
        remove_client(client_idx, 0);
    } else {
        send_to_client(fd, "999 UNKNOWN ERROR\n");
    }
}

int main() {
    int server_fd, new_socket, max_sd;
    struct sockaddr_in address;
    fd_set readfds;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = 0;
        clients[i].is_joined = 0;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 10);

    printf("Chat server is running on port %d...\n", PORT);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = clients[i].fd;
            if (sd > 0) FD_SET(sd, &readfds);
            if (sd > max_sd) max_sd = sd;
        }

        select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if (FD_ISSET(server_fd, &readfds)) {
            new_socket = accept(server_fd, NULL, NULL);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].fd == 0) {
                    clients[i].fd = new_socket;
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = clients[i].fd;
            if (FD_ISSET(sd, &readfds)) {
                char buffer[BUFFER_SIZE];
                memset(buffer, 0, BUFFER_SIZE);
                int valread = read(sd, buffer, BUFFER_SIZE - 1);
                
                if (valread == 0) {
                    remove_client(i, 0);
                } else {
                    handle_client_message(i, buffer);
                }
            }
        }
    }
    return 0;
}