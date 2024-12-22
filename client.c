// client.c
#include <sys/socket.h> // socket(), connect()
#include <stdlib.h>     // exit()
#include <unistd.h>     // close(), read(), write()
#include <arpa/inet.h>  // htons(), inet_pton()
#include <string.h>     // memset(), strlen()
#include <stdio.h>      // printf(), fgets()
#include <pthread.h>    // pthreads for threading
#include <ctype.h>      // toupper()
#include <signal.h>     // signal handling

#define TEMP_PORT 30076
#define BUF_SIZE 1024
#define GRID_SIZE 5
#define SHIP_COUNT 3

// Global variables
int server_fd;
char grid[GRID_SIZE][GRID_SIZE];
int ships_remaining = SHIP_COUNT;
pthread_mutex_t grid_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function prototypes
void initialize_grid(void);
void place_ships(void);
void process_guess(char *guess, char *response);
void *read_from_server(void *arg);
void *write_to_server(void *arg);

// Function to initialize the grid
void initialize_grid(void) {
    memset(grid, 0, sizeof(grid));
}

// Function to place ships
void place_ships(void) {
    char input[16];
    int placed = 0;
    printf("Place your %d ships on the %dx%d grid (e.g., B1, C3):\n", SHIP_COUNT, GRID_SIZE, GRID_SIZE);
    while (placed < SHIP_COUNT) {
        printf("Enter coordinate for ship %d: ", placed + 1);
        fflush(stdout);
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("Error reading input.\n");
            continue;
        }

        // Convert input to uppercase and parse
        char col = toupper(input[0]);
        int row = atoi(&input[1]) - 1;

        if (col < 'A' || col >= 'A' + GRID_SIZE || row < 0 || row >= GRID_SIZE) {
            printf("Invalid coordinate. Try again.\n");
            continue;
        }

        int col_index = col - 'A';

        pthread_mutex_lock(&grid_mutex);
        if (grid[row][col_index] == 'S') {
            printf("Ship already placed at that location. Try again.\n");
            pthread_mutex_unlock(&grid_mutex);
            continue;
        }

        grid[row][col_index] = 'S';
        pthread_mutex_unlock(&grid_mutex);
        placed++;
    }
}

// Function to process opponent's guess
void process_guess(char *guess, char *response) {
    char col = toupper(guess[0]);
    int row = atoi(&guess[1]) - 1;

    if (col < 'A' || col >= 'A' + GRID_SIZE || row < 0 || row >= GRID_SIZE) {
        snprintf(response, BUF_SIZE, "Invalid");
    } else {
        int col_index = col - 'A';

        pthread_mutex_lock(&grid_mutex);
        if (grid[row][col_index] == 'S') {
            grid[row][col_index] = 'H';
            ships_remaining--;
            if (ships_remaining == 0) {
                snprintf(response, BUF_SIZE, "Hit! You lose.");
            } else {
                snprintf(response, BUF_SIZE, "Hit");
            }
        } else if (grid[row][col_index] == 'H' || grid[row][col_index] == 'M') {
            snprintf(response, BUF_SIZE, "Already Hit");
        } else {
            grid[row][col_index] = 'M';
            snprintf(response, BUF_SIZE, "Miss");
        }
        pthread_mutex_unlock(&grid_mutex);
    }
}

// Thread to read messages from the server
void *read_from_server(void *arg) {
    char buffer[BUF_SIZE];
    ssize_t rcount;

    while (1) {
        // Read message from server
        rcount = read(server_fd, buffer, BUF_SIZE - 1);
        if (rcount <= 0) {
            printf("\nServer disconnected.\n");
            break;
        }
        buffer[rcount] = '\0';

        // Check if it's a prompt or opponent's guess
        if (strncmp(buffer, "Your turn", 9) == 0 || strncmp(buffer, "Opponent", 8) == 0 || strlen(buffer) <= 3) {
            printf("%s", buffer);
            fflush(stdout);
        } else {
            // It's a response to our guess
            printf("\nOpponent: %s\n", buffer);
            fflush(stdout);
            if (strstr(buffer, "You win") != NULL || strstr(buffer, "You lose") != NULL) {
                break;
            }
        }
    }

    // Close the client socket
    close(server_fd);
    exit(EXIT_SUCCESS);
    return NULL;
}

// Thread to write messages to the server
void *write_to_server(void *arg) {
    char buffer[BUF_SIZE];
    ssize_t wcount;

    while (1) {
        // Read user input
        if (fgets(buffer, BUF_SIZE, stdin) == NULL) {
            printf("Error reading input.\n");
            break;
        }

        // Send input to server
        wcount = write(server_fd, buffer, strlen(buffer));
        if (wcount == -1) {
            perror("write");
            break;
        }
    }

    // Close the client socket
    close(server_fd);
    exit(EXIT_SUCCESS);
    return NULL;
}

// MAIN: client
int main(void) {
    int err;
    struct sockaddr_in addr;
    char buffer[BUF_SIZE];
    ssize_t rcount;

    // Create a new Internet domain stream socket (TCP/IP socket)
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Exit on socket() error
    if (server_fd == -1) {
        perror("socket");
        exit(1);
    }

    // Set addr struct to 0 bytes
    memset(&addr, 0, sizeof(addr));

    // Set 3 addr structure members
    addr.sin_family = AF_INET;              // addr type is Internet (IPv4)
    inet_pton(AF_INET, "127.0.0.1", &(addr.sin_addr)); // connect to localhost
    addr.sin_port = htons(TEMP_PORT);       // convert port to network byte ordering

    // Attempt a connection to the server
    err = connect(server_fd, (struct sockaddr *)&addr, sizeof(addr));

    // Close socket and exit on connect() error
    if (err == -1) {
        perror("connect");
        close(server_fd);
        exit(2);
    }

    printf("Connected to the server.\n");
    printf("Waiting for an opponent...\n");

    // Read message from server
    rcount = read(server_fd, buffer, BUF_SIZE - 1);
    if (rcount <= 0) {
        printf("Server disconnected.\n");
        close(server_fd);
        exit(3);
    }
    buffer[rcount] = '\0';
    printf("%s", buffer);

    // Initialize grid and place ships
    initialize_grid();
    place_ships();

    // Create threads for reading from and writing to the server
    pthread_t read_thread, write_thread;
    pthread_create(&read_thread, NULL, read_from_server, NULL);
    pthread_create(&write_thread, NULL, write_to_server, NULL);

    // Wait for threads to finish
    pthread_join(read_thread, NULL);
    pthread_join(write_thread, NULL);

    // Close the client socket
    close(server_fd);

    return 0;
}
