// server.c
#include <sys/socket.h>  // socket(), bind(), listen(), accept()
#include <stdlib.h>      // exit(), malloc(), free(), EXIT_FAILURE
#include <unistd.h>      // close(), read(), write()
#include <arpa/inet.h>   // htons(), inet_ntoa()
#include <string.h>      // memset(), strlen(), strcpy()
#include <stdio.h>       // printf(), perror(), snprintf()
#include <pthread.h>     // pthreads for threading

#define TEMP_PORT 30076
#define BUF_SIZE 1024
#define MAX_CLIENTS 100

// Forward declarations
typedef struct client_t client_t;
typedef struct game_t game_t;

// Client structure
struct client_t {
    int sockfd;
    pthread_t read_thread;
    pthread_t write_thread;
    int game_ready;
    int player_number; // 1 or 2
    game_t *game;
};

// Game structure
struct game_t {
    client_t *player1;
    client_t *player2;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int turn; // 1 or 2
    int game_over;
};

// Global variables
client_t *clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function prototypes
void add_client(client_t *cl);
void remove_client(int sockfd);
void *handle_read(void *arg);
void *handle_write(void *arg);
void create_game(client_t *cli);

// Function to add a client
void add_client(client_t *cl) {
    pthread_mutex_lock(&clients_mutex);
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(!clients[i]) {
            clients[i] = cl;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Function to remove a client
void remove_client(int sockfd) {
    pthread_mutex_lock(&clients_mutex);
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(clients[i]) {
            if(clients[i]->sockfd == sockfd) {
                clients[i] = NULL;
                break;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Function to create a game when two clients are connected
void create_game(client_t *cli) {
    pthread_mutex_lock(&clients_mutex);

    client_t *waiting_client = NULL;
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(clients[i] && clients[i]->game_ready == 0 && clients[i]->sockfd != cli->sockfd) {
            waiting_client = clients[i];
            break;
        }
    }

    if(waiting_client) {
        // Pair clients into a game
        game_t *game = (game_t *)malloc(sizeof(game_t));
        if (!game) {
            perror("malloc");
            pthread_mutex_unlock(&clients_mutex);
            return;
        }
        memset(game, 0, sizeof(game_t));
        pthread_mutex_init(&game->mutex, NULL);
        pthread_cond_init(&game->cond, NULL);

        game->player1 = waiting_client;
        game->player2 = cli;
        game->turn = 1; // Player 1 starts
        game->game_over = 0;

        waiting_client->game = game;
        cli->game = game;

        waiting_client->game_ready = 1;
        cli->game_ready = 1;
        waiting_client->player_number = 1;
        cli->player_number = 2;

        // Notify both clients
        char message[] = "Game started. Place your ships.\n";
        write(waiting_client->sockfd, message, strlen(message));
        write(cli->sockfd, message, strlen(message));
    }

    pthread_mutex_unlock(&clients_mutex);
}

// Thread function to handle reading from client
void *handle_read(void *arg) {
    client_t *cli = (client_t *)arg;
    char buffer[BUF_SIZE];
    ssize_t rcount;

    // Wait for the game to be ready
    while(cli->game_ready == 0) {
        sleep(1);
    }

    game_t *game = cli->game;

    // Notify about turn order
    if(cli->player_number == 1) {
        char message[] = "You go first.\n";
        write(cli->sockfd, message, strlen(message));
    } else {
        char message[] = "Opponent goes first. Please wait.\n";
        write(cli->sockfd, message, strlen(message));
    }

    while(!game->game_over) {
        pthread_mutex_lock(&game->mutex);

        if(game->turn == cli->player_number && !game->game_over) {
            // It's this client's turn
            pthread_mutex_unlock(&game->mutex);
            // Read message from client
            rcount = read(cli->sockfd, buffer, BUF_SIZE - 1);
            if(rcount <= 0) {
                printf("Client %d disconnected.\n", cli->sockfd);
                game->game_over = 1;
                pthread_cond_broadcast(&game->cond);
                break;
            }
            buffer[rcount] = '\0';

            // Send message to opponent
            client_t *opponent = (cli->player_number == 1) ? game->player2 : game->player1;
            write(opponent->sockfd, buffer, strlen(buffer));

            // Check for game over
            if(strstr(buffer, "You lose") != NULL || strstr(buffer, "You win") != NULL) {
                game->game_over = 1;
            }

            // Switch turn
            pthread_mutex_lock(&game->mutex);
            game->turn = (cli->player_number == 1) ? 2 : 1;
            pthread_cond_broadcast(&game->cond);
            pthread_mutex_unlock(&game->mutex);
        } else {
            // Wait for opponent's move
            pthread_cond_wait(&game->cond, &game->mutex);
            pthread_mutex_unlock(&game->mutex);
        }
    }

    // Clean up
    close(cli->sockfd);
    remove_client(cli->sockfd);
    free(cli);
    pthread_exit(NULL);
}

// Thread function to handle writing to client
void *handle_write(void *arg) {
    return NULL;
}

// MAIN: server
int main(void) {
    int listen_fd, err, opt;
    struct sockaddr_in addr, client_addr;

    // Initialize clients array
    memset(clients, 0, sizeof(clients));

    // Create a new socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    opt = 1;
    err = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if(err == -1) {
        perror("setsockopt");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // Initialize address structure
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(TEMP_PORT);

    // Bind socket
    err = bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
    if(err == -1) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // Listen
    err = listen(listen_fd, 32);
    if(err == -1) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    printf("Battleship server started on port %d.\n", TEMP_PORT);

    // Accept clients
    while(1) {
        socklen_t client_len = sizeof(client_addr);
        int client_sockfd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if(client_sockfd == -1) {
            perror("accept");
            continue;
        }

        // Create client structure
        client_t *cli = (client_t *)malloc(sizeof(client_t));
        if(!cli) {
            perror("malloc");
            close(client_sockfd);
            continue;
        }
        cli->sockfd = client_sockfd;
        cli->game_ready = 0;
        cli->game = NULL;

        // Add client to list
        add_client(cli);

        // Create threads for reading and writing
        if(pthread_create(&cli->read_thread, NULL, handle_read, (void *)cli) != 0) {
            perror("pthread_create");
            close(cli->sockfd);
            remove_client(cli->sockfd);
            free(cli);
            continue;
        }
       

       
        create_game(cli);
    }

    close(listen_fd);
    return 0;
}
