/*
 * socket demonstrations:
 * This is the server side of an "internet domain" socket connection, for
 * communicating over the network.
 *
 * In this case we are willing to wait for chatter from the client
 * _or_ for a new connection.
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h> // for rand

#ifndef PORT
    #define PORT 56073
#endif

# define SECONDS 10

enum client_state {
    AWAITING_NAME,  // Client has connected but hasn't sent their name
    LOOKING_FOR_MATCH,  // Client has sent their name and is waiting for a match
    IN_MATCH_ATTACK, // Client is currently in a match
    IN_MATCH_DEFEND,
    TYPING_CHAT // Client is typing a message
};

struct client {
    int fd;
    struct in_addr ipaddr;
    struct client *next;
    // Buffers for the client to store that name
    char name[256];
    int inputLength;
    char inputBuffer[256];

    char type[256];
    int typeLengthBuffer;
    // Store opponent if in match or NULL if not in match
    struct client *opponent; 
    // Store the last played opponent 
    struct client *lastplayed;
    enum client_state state; // state of the client
    enum client_state prevState;
    // game_state: 0: no game, 1: attack mode, -1: defend move
    int game_state; 
    int health;
    int power_moves;
    int on_mute; // 0: not muted, 1: muted
    int in_state_typing_mute; // to handl ebreak if one player is on mute but stil is typign
    // Buffer to store the input from the client
    char buf[256];
};

static struct client *addclient(struct client *top, int fd, struct in_addr addr);
static struct client *removeclient(struct client *top, int fd);
static void broadcast(struct client *top, char *s, int size);
int handleclient(struct client *p, struct client *top);

int bindandlisten(void);

int main(void) {
    // Calling srand once to seed the time
    srand(time(NULL));
    // max fd is the maximum file descriptor number
    int clientfd, maxfd, nready;
    // we need a pointer to a client struct, this has all of our clients
    struct client *p;
    struct client *head = NULL;
    socklen_t len;
    struct sockaddr_in q;
    // we need a timeout for select ( we pass that into select  )
    struct timeval tv;
    // we need two sets of file descriptors because select is destructive
    // this means that select will remove the file descriptor from the set
    fd_set allset;
    fd_set rset;

    int i;


    int listenfd = bindandlisten();
    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset); // clear the set
    FD_SET(listenfd, &allset); // add listenfd to the set
    // maxfd identifies how far into the set to search
    maxfd = listenfd; // the maximum file descriptor is the listenfd (0 is stdin ..)

    while (1) {
        // make a copy of the set before we pass it into select
        rset = allset;
        /* timeout in seconds (You may not need to use a timeout for
        * your assignment)*/
        tv.tv_sec = SECONDS;
        tv.tv_usec = 0;  /* and microseconds */

        nready = select(maxfd + 1, &rset, NULL, NULL, &tv);
        // when select returns, we know that there is a client ready to talk
        // but which one? thats why we need to iterate over all of the clients
        if (nready == 0) {
            printf("No response from clients in %d seconds\n", SECONDS);
            continue;
        }

        if (nready == -1) {
            perror("select");
            continue;
        }

        // if the listenfd is in the set, we know that a new client is connecting
        if (FD_ISSET(listenfd, &rset)){
            printf("a new client is connecting\n");
            len = sizeof(q); // to pass in size of address for accept
            if ((clientfd = accept(listenfd, (struct sockaddr *)&q, &len)) < 0) {
                perror("accept");
                exit(1);
            }
            // add that client into the set of clients
            FD_SET(clientfd, &allset);
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }
            printf("connection from %s\n", inet_ntoa(q.sin_addr));
            // adding the client to the list of clients
            head = addclient(head, clientfd, q.sin_addr);
        }
        // checking all of the clients to see which one is ready to talk
        for(i = 0; i <= maxfd; i++) {
            if (FD_ISSET(i, &rset)) { // this checks if the file descriptor is 
            // in the ready set and if its then we know that the client is ready to talk
                for (p = head; p != NULL; p = p->next) { 
                    if (p->fd == i) {
                        // handle the client
                        int result = handleclient(p, head);
                        if (result == -1) { // client disconnected
                            // remove the client from the set of file descriptors
                            int tmp_fd = p->fd;
                            head = removeclient(head, p->fd);
                            FD_CLR(tmp_fd, &allset);
                            close(tmp_fd);
                        }
                        break;
                    }
                }
            }
        }
    }
    return 0;
}

int handleclient(struct client *p, struct client *top) {
    //char buf[1]; // buffer to read from the client, one byte at a time
    char outbuf[512];
    int lenName; 
    int len = 0;
    
    if (p->state == AWAITING_NAME) {
        // Attemp to read from buffer 
        lenName = read(p->fd, p->buf, 1);
        if (lenName > 0){
            if (p->buf[0] == '\n' || p->buf[0] == '\r') {
                // Null terminate the input buffer
                p->inputBuffer[p->inputLength] = '\0';
                strncpy(p->name, p->inputBuffer, sizeof(p->name));
                // Ensure null termination
                p->name[sizeof(p->name)-1] = '\0';
                // Broadcast to all clients that the client has joined the area
                sprintf(outbuf, "\r\n**%s joined the area.**\r\n", p->name);
                broadcast(top, outbuf, strlen(outbuf));
                sprintf(outbuf, "\nWelcome, %s! Awaiting opponent...\n", p->name);
                write(p->fd, outbuf, strlen(outbuf));
                // Attempt matchmaking
                p->state = LOOKING_FOR_MATCH;
                // Reset the input buffer
                memset(p->inputBuffer, 0, sizeof(p->inputBuffer));
                p->inputLength = 0;
                p->buf[0] = '\0';
            }
            else {
                // Append the character to the input buffer
                p->inputBuffer[p->inputLength] = p->buf[0];
                p->inputLength++;
                return 0;
            }
        }
        else if (lenName <= 0) {
        // socket is closed
        printf("Disconnect from %s\n", inet_ntoa(p->ipaddr));
        sprintf(outbuf, "Goodbye %s\r\n", inet_ntoa(p->ipaddr));
        broadcast(top, outbuf, strlen(outbuf));
        return -1;
        } 
        // Add some error schecking for the read?
    }
    // Starting the matchmaking
    if (p->state == LOOKING_FOR_MATCH) {
        struct client *other;
        for (other = top; other!= NULL; other = other->next) {
            // Check if other is not equal to p, hasn't been previous matched with p, and is looking for a match
            if (other != p && other != p->lastplayed && other->state == LOOKING_FOR_MATCH) {
                // Setting up the match
                p->opponent = other;
                p->health = rand() % 11 + 20;
                p->power_moves= rand() % 3 + 1;
                p->state = IN_MATCH_DEFEND;
                p->on_mute = 0;
                other->opponent = p;
                other->on_mute = 0;
                other->health = rand() % 11 + 20;
                other->power_moves= rand() % 3 + 1;
                other->state = IN_MATCH_ATTACK;
                

                // Notify the clients that they are in a match
                sprintf(outbuf, "You engage %s!\n", p->opponent->name);
                write(p->fd, outbuf, strlen(outbuf));
                sprintf(outbuf, "\nYou engage %s!\n", p->name);
                write(p->opponent->fd, outbuf, strlen(outbuf));
                sprintf(outbuf, "\n(a)ttack\n(p)owermove\n(s)peak something\n(m)mute opponent\n\n");
                write(p->opponent->fd, outbuf, strlen(outbuf));
                return 0;
            }
        }
        if (p->opponent == NULL) {
            // No match found -- waiting for another player
            return 0;
        }
        
    }
    // Handle the game logic
    if (p->state == TYPING_CHAT){
        lenName = read(p->fd, p->buf, 1);
        if (lenName  > 0) {
            // Check if new line or network new line is recieved 
            if (p->buf[0] == '\n' || p->buf[0] == '\r') {
                // Null terminate the input buffer
                p->inputBuffer[p->inputLength] = '\0';
                int counter = 0;
                if (strstr(p->inputBuffer, "xyz") != NULL) {
                    // Cheat code found, perform the action
                    p->power_moves = 20; // Set power moves to 20 or any other cheat action
                    sprintf(outbuf, "Cheat activated: Power moves set to 20!\n");
                    write(p->fd, outbuf, strlen(outbuf));
                    p->state = p->prevState;
                    // Reset the input buffer
                    memset(p->inputBuffer, 0, sizeof(p->inputBuffer));
                    p->inputLength = 0;
                    p->buf[0] = '\0';
                    counter = 1;
                }
                if (strstr(p->inputBuffer, "mute") != NULL) {
                    counter = 1;
                    if (p->on_mute == 1) {
                        p->on_mute = 0;
                        sprintf(outbuf, "\nYou are no longer muting %s!\n", p->opponent->name);
                        write(p->fd, outbuf, strlen(outbuf));
                        p->state = p->prevState;
                        // Reset the input buffer
                        memset(p->inputBuffer, 0, sizeof(p->inputBuffer));
                        p->inputLength = 0;
                        p->buf[0] = '\0';
                    }
                    else {
                        p->on_mute = 1;
                        sprintf(outbuf, "\nYou are now muting %s!\n", p->opponent->name);
                        write(p->fd, outbuf, strlen(outbuf));
                        p->state = p->prevState;
                        // Reset the input buffer
                        memset(p->inputBuffer, 0, sizeof(p->inputBuffer));
                        p->inputLength = 0;
                        p->buf[0] = '\0';
                    }
                    
                    // Cheat code found, perform the action
                    //p->power_moves = 20; // Set power moves to 20 or any other cheat action
                    //sprintf(outbuf, "Cheat activated: Power moves set to 20!\n");
                    //write(p->fd, outbuf, strlen(outbuf));
                }

                
                if (p->opponent->on_mute == 0 && counter == 0 && p->in_state_typing_mute == 0) {
                    sprintf(outbuf, "\n%s says: ", p->name);
                    write(p->opponent->fd, outbuf, strlen(outbuf));
                    sprintf(outbuf, "%s\n\n", p->inputBuffer);
                    write(p->opponent->fd, outbuf, strlen(outbuf));
                }
                sprintf(outbuf, "\n");
                write(p->fd, outbuf, strlen(outbuf));
                counter = 0;
                p->in_state_typing_mute = 0;

                p->state = p->prevState;
                // Reset the input buffer
                memset(p->inputBuffer, 0, sizeof(p->inputBuffer));
                p->inputLength = 0;
                p->buf[0] = '\0';
            }
            else {
                // Append the character to the input buffer
                p->inputBuffer[p->inputLength] = p->buf[0];
                p->inputLength++;
                return 0;
            }

        }
        else if (lenName <= 0) {
        // socket is  client disconnect
        // Notify the clients that the game is over
        sprintf(outbuf, "%s is dead!. You win!\n", p->opponent->name);
        write(p->fd, outbuf, strlen(outbuf));

        sprintf(outbuf, "You are dead!. %s is VICTORIUS!...\n", p->name);
        write(p->opponent->fd, outbuf, strlen(outbuf));

        // Reset the game state
        sprintf(outbuf, "Awaiting opponent...\n");
        write(p->fd, outbuf, strlen(outbuf));
        write(p->opponent->fd, outbuf, strlen(outbuf));  
            
        p->lastplayed = p->opponent; // Assigns p->opponent to p->lastplayed
        p->opponent->lastplayed = p; // Assigns p to p->opponent->lastplayed
        sprintf(outbuf, "Type anything to find a new match...: \n");
        write(p->opponent->fd, outbuf, strlen(outbuf));
        p->state = LOOKING_FOR_MATCH; // Sets p->state to LOOKING_FOR_MATCH
        p->opponent->state = LOOKING_FOR_MATCH; // Accesses p->opponent and sets its state
        p->opponent->opponent = NULL; // Sets p->opponent to NULL
        p->opponent = NULL; // Sets p->opponent to NULL

        printf("Disconnect from %s\n", inet_ntoa(p->ipaddr));
        sprintf(outbuf, "Goodbye %s\r\n", inet_ntoa(p->ipaddr));
        broadcast(top, outbuf, strlen(outbuf));
        return -1;
        } 
    }
    len = read(p->fd, p->buf, sizeof(p->buf) - 1); 
    if (p->state == IN_MATCH_ATTACK || p->state == IN_MATCH_DEFEND) {
        if(p->state == IN_MATCH_ATTACK) {
            // Send p's info
            sprintf(outbuf, "\nYour health:%d\nYour powermoves: %d\n%s's health:%d\n", p->health, p->power_moves, p->opponent->name, p->opponent->health);
            write(p->fd, outbuf, strlen(outbuf));
            // Send p's opponent info
            sprintf(outbuf, "\nYour health:%d\nYour powermoves: %d\n%s's health:%d\n", p->opponent->health, p->opponent->power_moves, p->name, p->health);
            write(p->opponent->fd, outbuf, strlen(outbuf));
            // Send the options to p
            sprintf(outbuf, "\n(a)ttack\n(p)owermove\n(s)peak something\n(m)mute opponent\n\n");
            write(p->fd, outbuf, strlen(outbuf));
            // Notify p's opponent that they are waiting for p to make a move
            sprintf(outbuf, "Waiting for %s to strike...\n", p->name);
            write(p->opponent->fd, outbuf, strlen(outbuf));
        }
        else if(p->state == IN_MATCH_DEFEND) {
            // Send p's info
            // There is nothing to do when the client is in defend mode
            if (len <= 0) {
                // Notify the clients that the game is over
                sprintf(outbuf, "%s has left the game!!\n", p->opponent->name);
                write(p->fd, outbuf, strlen(outbuf));

                // Reset the game state
                sprintf(outbuf, "Awaiting opponent...\n");
                write(p->opponent->fd, outbuf, strlen(outbuf));  
                    
                p->lastplayed = p->opponent; // Assigns p->opponent to p->lastplayed
                p->opponent->lastplayed = p; // Assigns p to p->opponent->lastplayed
                p->state = LOOKING_FOR_MATCH; // Sets p->state to LOOKING_FOR_MATCH
                p->opponent->state = LOOKING_FOR_MATCH; // Accesses p->opponent and sets its state
                p->opponent->opponent = NULL; // Sets p->opponent to NULL
                p->opponent = NULL; // Sets p->opponent to NULL
                printf("Disconnect from %s\n", inet_ntoa(p->ipaddr));
                sprintf(outbuf, "Goodbye %s\r\n", inet_ntoa(p->ipaddr));
                broadcast(top, outbuf, strlen(outbuf));
                return -1;
            } 
            p->buf[0] = '\0'; // Erasing the buffer because its not thier attacking turn
            return 0;
        }
        // Parsing the input from the client
        if (len > 0 && (p->state == IN_MATCH_ATTACK)){
            if(p->buf[0] == 'a') {
                // Using an attack move
                p->buf[0] = '\0'; // Erasing the buffer
                int dmg = rand() % 6 + 1;
                p->opponent->health -= dmg;
                sprintf(outbuf, "You hit %s for %d damage!\n", p->opponent->name, dmg);
                write(p->fd, outbuf, strlen(outbuf));
                sprintf(outbuf, "%s hits you for %d damage!\n", p->name, dmg);
                write(p->opponent->fd, outbuf, strlen(outbuf));
                // Check if the opponent is dead
                if (p->opponent->health <= 0) {
                    // Notify the clients that the game is over
                    sprintf(outbuf, "%s is dead!. You win!\n", p->opponent->name);
                    write(p->fd, outbuf, strlen(outbuf));

                    sprintf(outbuf, "You are dead!. %s is VICTORIUS!...\n", p->name);
                    write(p->opponent->fd, outbuf, strlen(outbuf));

                    // Reset the game state
                    sprintf(outbuf, "Awaiting opponent...\n");
                    write(p->fd, outbuf, strlen(outbuf));
                    write(p->opponent->fd, outbuf, strlen(outbuf));  
                      
                    p->lastplayed = p->opponent; // Assigns p->opponent to p->lastplayed
                    p->opponent->lastplayed = p; // Assigns p to p->opponent->lastplayed
                    p->state = LOOKING_FOR_MATCH; // Sets p->state to LOOKING_FOR_MATCH
                    p->opponent->state = LOOKING_FOR_MATCH; // Accesses p->opponent and sets its state
                    p->opponent->opponent = NULL; // Sets p->opponent to NULL
                    p->opponent = NULL; // Sets p->opponent to NULL
                    return 0;
                }
                else {
                    p->state = IN_MATCH_DEFEND;
                    p->opponent->state = IN_MATCH_ATTACK;
                    sprintf(outbuf, "\n(a)ttack\n(p)owermove\n(s)peak something\n(m)mute opponent\n\n");
                    write(p->opponent->fd, outbuf, strlen(outbuf)); 
                    return 0;
                }
                return 0;
            }
            else if (p->buf[0] == 'p') {
                // Using a power move
                p->buf[0] = '\0'; // Erasing the buffer
                if (p->power_moves <= 0) {
                    sprintf(outbuf, "You are out of power moves!\n");
                    write(p->fd, outbuf, strlen(outbuf));
                    p->state = IN_MATCH_DEFEND;
                    p->opponent->state = IN_MATCH_ATTACK; 
                    sprintf(outbuf, "\n(a)ttack\n(p)owermove\n(s)peak something\n(m)mute opponent\n\n");
                    write(p->opponent->fd, outbuf, strlen(outbuf));
                    return 0;
                }
                if (p->power_moves > 0) {
                    p->power_moves--;
                }
                int dmg;
                int prob = rand() % 2;
                if (prob == 0) {
                    // Missed the power move
                    sprintf(outbuf, "Unlucky! You missed %s!\n", p->opponent->name);
                    write(p->fd, outbuf, strlen(outbuf));
                    sprintf(outbuf, "%s missed you! How Lucky!\n", p->name);
                    write(p->opponent->fd, outbuf, strlen(outbuf));
                    p->state = IN_MATCH_DEFEND;
                    p->opponent->state = IN_MATCH_ATTACK; 
                    sprintf(outbuf, "\n(a)ttack\n(p)owermove\n(s)peak something\n(m)mute opponent\n\n");
                    write(p->opponent->fd, outbuf, strlen(outbuf));
                    return 0;
                }
                else {
                    // Hit the power move
                    dmg = (rand() % 6 + 1) * 3;
                    p->opponent->health -= dmg;
                    sprintf(outbuf, "You hit %s for %d damage with a power move!\n", p->opponent->name, dmg);
                    write(p->fd, outbuf, strlen(outbuf));
                    sprintf(outbuf, "%s hits you for %d damage with a power move!\n", p->name, dmg);
                    write(p->opponent->fd, outbuf, strlen(outbuf));
                    // Check if the opponent is dead
                    if (p->opponent->health <= 0) {
                        // Notify the clients that the game is over
                        sprintf(outbuf, "%s is dead!. You win!\n", p->opponent->name);
                        write(p->fd, outbuf, strlen(outbuf));

                        sprintf(outbuf, "You are dead!. %s is VICTORIUS!...\n", p->name);
                        write(p->opponent->fd, outbuf, strlen(outbuf));

                        // Reset the game state
                        sprintf(outbuf, "Awaiting opponent...\n");
                        write(p->fd, outbuf, strlen(outbuf));
                        write(p->opponent->fd, outbuf, strlen(outbuf));  
                        
                        p->lastplayed = p->opponent;
                        p->opponent->lastplayed = p;
                        p->opponent->opponent = NULL;
                        p->opponent = NULL;
                        p->state = LOOKING_FOR_MATCH;
                        p->opponent->state = LOOKING_FOR_MATCH;
                        sprintf(outbuf, "Do you want to play another match?\n");
                        write(p->opponent->fd, outbuf, strlen(outbuf));
                        return 0;
                    }
                    else {
                        p->state = IN_MATCH_DEFEND;
                        p->opponent->state = IN_MATCH_ATTACK;
                        sprintf(outbuf, "\n(a)ttack\n(p)owermove\n(s)peak something\n(m)mute opponent\n\n");
                        write(p->opponent->fd, outbuf, strlen(outbuf)); 
                        return 0;
                    }  
                    return 0;
                }  
            }
            
            else if (p->buf[0] == 's') {
                // Speaking something
                p->buf[0] = '\0'; // Erasing the buffer
                p->prevState = p->state;
                p->state = TYPING_CHAT;
                p->opponent->state = IN_MATCH_DEFEND;
                p->in_state_typing_mute = 0;
                sprintf(outbuf, "\nSpeak: ");
                write(p->fd, outbuf, strlen(outbuf));
                return 0;
            }
            else if (p->buf[0] == 'm') {
                // Speaking something
                p->buf[0] = '\0'; // Erasing the buffer
                p->prevState = p->state;
                p->state = TYPING_CHAT;
                p->opponent->state = IN_MATCH_DEFEND;
                p->in_state_typing_mute = 1;
                sprintf(outbuf, "\nDo you want to mute/unmute your opponent? type (mute) to confirm: ");
                write(p->fd, outbuf, strlen(outbuf));
                return 0;
            }
        }
    else if (len <= 0) {
        // Notify the clients that the game is over
        sprintf(outbuf, "%s has left the game!!\n", p->opponent->name);
        write(p->fd, outbuf, strlen(outbuf));

        // Reset the game state
        sprintf(outbuf, "Awaiting opponent...\n");
        write(p->opponent->fd, outbuf, strlen(outbuf));  
            
        p->lastplayed = p->opponent; // Assigns p->opponent to p->lastplayed
        p->opponent->lastplayed = p; // Assigns p to p->opponent->lastplayed
        p->state = LOOKING_FOR_MATCH; // Sets p->state to LOOKING_FOR_MATCH
        p->opponent->state = LOOKING_FOR_MATCH; // Accesses p->opponent and sets its state
        p->opponent->opponent = NULL; // Sets p->opponent to NULL
        p->opponent = NULL; // Sets p->opponent to NULL
        printf("Disconnect from %s\n", inet_ntoa(p->ipaddr));
        sprintf(outbuf, "Goodbye %s\r\n", inet_ntoa(p->ipaddr));
        broadcast(top, outbuf, strlen(outbuf));
        return -1;
    } 
    }
    return 0;
}

 /* bind and listen, abort on error
  * returns FD of listening socket
  */
int bindandlisten(void) {
    struct sockaddr_in r;
    int listenfd;
    // creating a TCP socket (note: SOCK_STREAM is TCP)
    // some error checking, return value of listenfd for error checking <0
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }
    //  OS release your server's port as soon as your server terminates
    int yes = 1;
    if ((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1) {
        perror("setsockopt");
    }
    // define the server address
    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    // INADDR_ANY means any address on local machine
    r.sin_addr.s_addr = INADDR_ANY;
    // htons converts the PORT number to network byte order
    r.sin_port = htons(PORT);

    // bind the server address to the socket (note: casting sockaddr_in to sockaddr)
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }
    // listen for connections on the socket, 5 is the maximum number of connections
    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
    return listenfd;
}

static struct client *addclient(struct client *top, int fd, struct in_addr addr) {
    char outbuf[512];
    struct client *p = malloc(sizeof(struct client));
    if (!p) {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", inet_ntoa(addr));

    p->fd = fd;
    p->ipaddr = addr;
    p->next = top;
    p->opponent = NULL;
    p->lastplayed = NULL;
    p->state = AWAITING_NAME;
    top = p;
    sprintf(outbuf, "What is your name?\n");
    write(p->fd, outbuf, strlen(outbuf));
    return top;
}

static struct client *removeclient(struct client *top, int fd) {
    struct client **p;

    for (p = &top; *p && (*p)->fd != fd; p = &(*p)->next)
        ;
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
        struct client *t = (*p)->next;
        printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));
        free(*p);
        *p = t;
    } else {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n",
                 fd);
    }
    return top;
}


static void broadcast(struct client *top, char *s, int size) {
    struct client *p;
    for (p = top; p; p = p->next) {
        write(p->fd, s, size);
    }
    /* should probably check write() return value and perhaps remove client */
}

