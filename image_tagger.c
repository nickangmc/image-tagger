// ------------------------------------------------------------ //
// created for COMP30023 Computer Systems - Project 1, 2019
// by Ang Mink Chen <minka@student.unimelb.edu.au>
// ------------------------------------------------------------ //

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// constants
#define MAX_BUFF 2049
#define BREAK_LINE "<br>"
#define MAX_NUM_GUESSWORDS 20
#define MAX_CHAR_PER_GUESSWORD 20
#define HTML_END_BODY_TAG "</body>"
#define HTTP_START_URL_PATH "/?start=Start"
#define KEYWORD_TRAILINGS_REMOVER "&guess=Guess"

static char const * const HTTP_200_FORMAT = "HTTP/1.1 200 OK\r\n\
Content-Type: text/html\r\n\
Content-Length: %ld\r\n\r\n";
static char const * const HTTP_400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
static int const HTTP_400_LENGTH = 47;
static char const * const HTTP_404 = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
static int const HTTP_404_LENGTH = 45;
static bool const DEBUG = false;

// structs definition

typedef enum
{
    GET,
    POST,
    UNKNOWN
} METHOD;

typedef enum
{
    WELCOME,
    START,
    FIRST_TURN,
    DISCARDED,
    ACCEPTED,
    ENDGAME,
    GAMEOVER
} PLAYER_STATE;

typedef enum
{
    INITIAL,
    ONEOFF,
    GAMEONBABY,
    RAGEQUIT,
    GAMEENDED
} GAME_STATE;

typedef struct 
{
    char * username;
    int username_length;
    PLAYER_STATE player_state;
    char guess_words[MAX_NUM_GUESSWORDS][MAX_CHAR_PER_GUESSWORD]; 
    int num_guess_words;
    int socket_no;
} player;

// ------------------------------------------------------------ //
/* Function prototypes */

int get_player_index(player * players, int sockfd);
char * extract_url_path(char * request);
char * extract_post_value(char * request);
char * get_html_page_url(PLAYER_STATE state, bool rematch);
bool keywords_cmp(player * players);
bool served_both_players_gameover(player * players);
void end_game(player * players);
void run_server(char * ip_add, char * port);
void initialise_players(player * players);
void print_game_state(GAME_STATE game_state);
void clear_players_guess_words(player * players);
void print_player_state(PLAYER_STATE player_state);
void send_html_page(int sockfd, char * buff, char * html_url);
void modify_html_page(char* newword, char* old_buffer, char* new_buff);
void construct_player(player * players, char * username, int sockfd);
void add_keyword_to_player(player * players, int p_index, char * keyword);
void send_modified_html_page(int sockfd, char * buff, char * html, char* word);
static int handle_http_request(int sockfd, player * players, GAME_STATE game_state, bool rematch);
GAME_STATE update_game_state(GAME_STATE game_state, player * players);
PLAYER_STATE update_player_state(char * request, char * url, GAME_STATE game_state, PLAYER_STATE prev_state);

// ------------------------------------------------------------ //
/* main */

int main(int argc, char * argv[])
{
    // not enough arguments provided
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s ip port\n", argv[0]);
        return 0;
    }

    run_server(argv[1], argv[2]);

    return 0;
}


// ------------------------------------------------------------ //
/* main functions */

// create and run the server
void run_server(char * ip_add, char * port)
{
    // create TCP socket which only accept IPv4
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // reuse the socket if possible
    int const reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0)
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // create and initialise address we will listen on
    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;

    // if ip parameter is not specified
    serv_addr.sin_addr.s_addr = inet_addr(ip_add);
    serv_addr.sin_port = htons(atoi(port));

    // bind address to socket
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    // listen on the socket
    listen(sockfd, 5);

    // log the server
    printf("image_tagger is now running at IP: %s on port %s\n", ip_add, port);

    // initialise an active file descriptors set
    fd_set masterfds;
    FD_ZERO(&masterfds);
    FD_SET(sockfd, &masterfds);
    // record the maximum socket number
    int maxfd = sockfd;

    // initialise the 'players' array structure (to store all the players' information)
    player players[2];
    initialise_players(players);

    // initialise game state (use for maintaining/determining the order of the game)
    GAME_STATE game_state = INITIAL; 

    bool rematch = false;

    while (1)
    {
        // monitor file descriptors
        fd_set readfds = masterfds;
        if (select(FD_SETSIZE, &readfds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            exit(EXIT_FAILURE);
        }

        // loop all possible descriptor
        for (int i = 0; i <= maxfd; ++i)
            // determine if the current file descriptor is active
            if (FD_ISSET(i, &readfds))
            {
                // create new socket if there is new incoming connection request
                if (i == sockfd)
                {
                    struct sockaddr_in cliaddr;
                    socklen_t clilen = sizeof(cliaddr);
                    int newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &clilen);
                    if (newsockfd < 0)
                        perror("accept");
                    else
                    {
                        // add the socket to the set
                        FD_SET(newsockfd, &masterfds);
                        // update the maximum tracker
                        if (newsockfd > maxfd)
                            maxfd = newsockfd;
                        // print out the IP and the socket number
                        char ip[INET_ADDRSTRLEN];
                        printf(
                            "new connection from %s on socket %d\n",
                            // convert to human readable string
                            inet_ntop(cliaddr.sin_family, &cliaddr.sin_addr, ip, INET_ADDRSTRLEN),
                            newsockfd
                        );
                    }
                }
                // a request is sent from the client 
                else 
                {   
                    // process and get the correct game state before handling the HTTP request 
                    switch (game_state)
                    {
                        // don't change the game state if it is a rematch
                        case (INITIAL) :
                            game_state = update_game_state(game_state, players);
                            break;
                        // make sure both players get served the GAMEOVER page if a player quits    
                        case (RAGEQUIT) :
                            if (served_both_players_gameover(players))
                            {
                                // clear and remove the current players' information
                                initialise_players(players);
                                // reset game state
                                game_state = INITIAL;
                            }
                            break;
                        // handle for rematch    
                        case (GAMEENDED) :
                            rematch = true;
                            break;    
                    }
                    
                    // process the HTTP request
                    if (!handle_http_request(i, players, game_state, rematch))
                    {
                        close(i);
                        FD_CLR(i, &masterfds);
                    }

                    // change the game state
                    game_state = update_game_state(game_state, players);

                    // ----------------- DEBUG -------------------
                    if (DEBUG)
                    { 
                        print_game_state(game_state);
                    }
                    // -------------------------------------------
                }
            }
    }
}

// process the incoming HTTP request
// return true if the request is processed successfully, false otherwise
static int handle_http_request(int sockfd, player * players, GAME_STATE game_state, bool rematch)
{
    // try to read the request
    char buff[2049];
    int n = read(sockfd, buff, 2049);
    if (n <= 0)
    {
        if (n < 0)
            perror("read");
        else
            printf("socket %d close the connection\n", sockfd);
        return false;
    }

    // terminate the string
    buff[n] = 0;
    // use curr variable as the string pointer
    char * curr = buff;

    // ----------------- DEBUG -------------------
    // log the incoming HTTP request for debug
    if (DEBUG)
    {
        printf("---------------------------\n");
        printf("%s\n", curr);
    }
    // -------------------------------------------

    // parse the method
    METHOD method = UNKNOWN;
    if (strncmp(curr, "GET ", 4) == 0)
    {
        curr += 4;
        method = GET;
    }
    else if (strncmp(curr, "POST ", 5) == 0)
    {
        curr += 5;
        method = POST;
    }
    // no valid methods found
    else if (write(sockfd, HTTP_400, HTTP_400_LENGTH) < 0)
    {
        perror("write");
        return false;
    }

    // get url path
    char * url = extract_url_path(curr);
    // skip over url path
    curr += strlen(url);

    // make sure it is a valid request
    if (*curr == ' ') {
        // use to store html url
        char * html_page_url;
        // try to find the player's index no in the 'players' array structure
        int p_index = get_player_index(players, sockfd);
        // change the state of the player based on incoming request, url path, game state, and player's previous state
        players[p_index].player_state = update_player_state(curr, url, game_state, players[p_index].player_state);

        // GET http request
        if (method == GET)
        {
            // check if the player is a new player or not 
            // (p_index = -1 if the player is not constructed(new player))
            if (p_index >= 0) 
            {
                // retrieve the appropriate html file based on player's state
                html_page_url = get_html_page_url(players[p_index].player_state, rematch);
            }
            // no player has been constructed yet, which means they just got to the site
            else 
            {   
                // if a player quits the game, the other player will get the GAMEOVER page as well
                html_page_url = (game_state == RAGEQUIT) ? get_html_page_url(GAMEOVER, rematch) : get_html_page_url(WELCOME, rematch);
            }
            // load and send the html page to the player
            send_html_page(sockfd, buff, html_page_url);
        }

        // POST http request
        else if (method == POST)
        {
            // check if the player is a new player or not 
            if (p_index >= 0) 
            {   
                // temp variable to hold player's submitted keyword
                char keyword[MAX_CHAR_PER_GUESSWORD];
                // temp variable to hold all of the guess words of the player
                char guess_words[MAX_BUFF];

                // process request based player's state
                switch (players[p_index].player_state) 
                {
                    // a registered player comes back to START page
                    case (START) :
                        // retrieve the html file based on player's state
                        html_page_url = get_html_page_url(players[p_index].player_state, rematch);
                        // load and send the html page with player's username added to it
                        send_modified_html_page(sockfd, buff, html_page_url, players[p_index].username);
                        // early return
                        return true;

                    // two players have started the game (pressed the start button on START page)
                    case (ACCEPTED) :
                        // extract the keyword 
                        strcpy(keyword, extract_post_value(curr));
                        // record the keyword submitted by the player
                        add_keyword_to_player(players, p_index, keyword);
                        
                        // retrieve and temporarily store all the recorded keywords of the player 
                        // so that they can be added and displayed on the html page
                        strcpy(guess_words, BREAK_LINE);
                        for (int i=0; i<players[p_index].num_guess_words; i++) 
                        {
                            strcat(guess_words, players[p_index].guess_words[i]);
                            strcat(guess_words, BREAK_LINE);
                        }

                        // check if there's any keyword matches for both players
                        if (keywords_cmp(players))
                        {
                            // set both player's state to ENDGAME
                            end_game(players);
                            // does what the function says (clears it so that they can rematch)
                            clear_players_guess_words(players); 
                            // load and send the appropriate html page 
                            html_page_url = get_html_page_url(players[p_index].player_state, rematch);
                            send_html_page(sockfd, buff, html_page_url);
                        }
                        // no keyword matches
                        else 
                        {
                            // load the html page with all the previous guess words submitted
                            html_page_url = get_html_page_url(players[p_index].player_state, rematch);
                            send_modified_html_page(sockfd, buff, html_page_url, guess_words);
                        }
                        // early return
                        return true;
                }
                // as player's state does not match any case stated above, 
                // proceeds to retrieve the appropriate html page and sends it back to player
                html_page_url = get_html_page_url(players[p_index].player_state, rematch);
                send_html_page(sockfd, buff, html_page_url);
            }

            // the player has not been constructed (registered with username) yet
            else 
            {
                // if the other player has already quitted, sends GAMEOVER page to this player
                if (game_state == RAGEQUIT)
                {
                    html_page_url = get_html_page_url(GAMEOVER, rematch);
                    send_html_page(sockfd, buff, html_page_url);
                }
                // POST request by unconstructed player can only mean they're trying to submit their
                // username in this case
                // hence, retrieve and store their username 
                else
                {
                    // variable to hold player's submitted username
                    char username[MAX_BUFF];
                    // store the username extracted
                    strcpy(username, extract_post_value(curr));
                    // construct (register) the player with the username and socket no (as the identifier)
                    construct_player(players, username, sockfd);
                    // find the index no of the player in the 'players' array structure
                    p_index = get_player_index(players, sockfd);
                    // find the appropriate html page
                    html_page_url = get_html_page_url(players[p_index].player_state, rematch);
                    // adds a breakline tag in front of the username's string (looks nicer)
                    char processed_username[MAX_BUFF];
                    strcpy(processed_username, BREAK_LINE);
                    strncat(processed_username, username, strlen(username));
                    // load the html page with the username added to it
                    send_modified_html_page(sockfd, buff, html_page_url, processed_username);
                }
            }
        }
        else
            // never used, just for completeness
            fprintf(stderr, "no other methods supported");
    }
    // send 404 (request not valid)
    else if (write(sockfd, HTTP_404, HTTP_404_LENGTH) < 0)
    {
        perror("write");
        return false;
    }
    // success return
    return true;
}


// ------------------------------------------------------------ //
/* HTML helper functions */

// returns the url to the appropriate html page based on player's state
char * get_html_page_url(PLAYER_STATE state, bool rematch) 
{
    switch (state) 
    {
        case (WELCOME) :
            return "./1_intro.html";
        case (START) :
            return "./2_start.html";
        case (FIRST_TURN) :
            return (rematch) ? "./3_1_first_turn.html" : "./3_first_turn.html";
        case (ACCEPTED) :
            return (rematch) ? "./4_1_accepted.html" : "./4_accepted.html";
        case (DISCARDED) :
            return (rematch) ? "./5_1_discarded.html" : "./5_discarded.html";
        case (ENDGAME) :
            return "./6_endgame.html";
        case (GAMEOVER) :
            return "./7_gameover.html";
        // never used, just for completeness
        default :
            return "./updated_html/1_intro.html";
    }
}

void send_html_page(int sockfd, char * buff, char * html_url) 
{
    // get the size of the file
    struct stat st;
    stat(html_url, &st);
    int n = sprintf(buff, HTTP_200_FORMAT, st.st_size);
    // send the header first
    if (write(sockfd, buff, n) < 0)
    {
        perror("write");
        return;
    }
    // send the file
    int filefd = open(html_url, O_RDONLY);
    do
    {
        n = sendfile(sockfd, filefd, NULL, 2048);
    }
    while (n > 0);
    // error handling
    if (n < 0)
    {
        perror("sendfile");
        close(filefd);
        return;
    }
    close(filefd);
}

void send_modified_html_page(int sockfd, char * buff, char * html, char * word)
{
    // get the size of the file
    struct stat st;
    stat(html, &st);

    // increase file size to accommodate the new guess
    int added_length = strlen(word);
    long size = st.st_size + added_length;
    int n = sprintf(buff, HTTP_200_FORMAT,size);
    // send the header first
    if (write(sockfd, buff, n) < 0)
    {
        perror("write");
        return;
    }
    // read the content of the HTML file
    int filefd = open(html, O_RDONLY);
    n = read(filefd, buff, st.st_size);
    if (n < 0)
    {
        perror("read");
        close(filefd);
        return;
    }
    close(filefd);

    // variable to store the modified html file with words added in it
    char new_buff[MAX_BUFF];
    // add the word string to the html file 
    modify_html_page(word, buff, new_buff);
    
    // send the html page back to the player
    if (write(sockfd, new_buff, size) < 0)
    {
        perror("write");
        return;
    }
}

// add words into html page 
void modify_html_page(char* word, char* prev_buff, char* new_buff)
{
    // separate the html file by the end body tag
    char before_body_tag[MAX_BUFF];
    char * after_body_tag = strstr(prev_buff, HTML_END_BODY_TAG);

    // fill the html code before the end-body tag to before_body_tag
    strncpy(before_body_tag, prev_buff, strlen(prev_buff)-strlen(after_body_tag));
    before_body_tag[strlen(before_body_tag)] = 0;

    // combine all the pieces together
    strcpy(new_buff, before_body_tag);
    strcat(new_buff, word);
    strcat(new_buff, after_body_tag);

    new_buff[strlen(new_buff)] = 0;
}


// ------------------------------------------------------------ //
/* HTTP request values extraction functions */

// takes in a HTTP request (without the REST method string in it)
// returns the URL path 
char * extract_url_path(char * request) 
{
    char temp[MAX_BUFF];
    char * url;
    const char s[2] = " ";

    strncpy(temp, request, strlen(request));
    url = strtok(temp, s);

    return url;
}

// parse throught HTTP request and returns the body value accordingly
char * extract_post_value(char * request) 
{
    char * value;

    if (strstr(request, "user=")) 
    {
        value = strstr(request, "user=") + 5;
    }
    else if (strstr(request, "keyword="))
    {
        value = strstr(request, "keyword=") + 8;
        value[strlen(value)-strlen(KEYWORD_TRAILINGS_REMOVER)] = '\0';
    }
    else if (strstr(request, "quit="))
    {
         value = strstr(request, "quit=") + 5;
    }
    return value;
}


// ------------------------------------------------------------ //
/* player helper functions */

// assign the username and the socket no (as the player identifier) 
// to an available slot in the 'players' array structure
// also initialise some relevant information for the player
void construct_player(player * players, char * username, int sockfd)
{
    for (int i=0; i<2; i++) 
    {
        if (players[i].socket_no < 0)
        {
            players[i].username = username;
            players[i].username_length = strlen(username);
            players[i].player_state = START;
            players[i].socket_no = sockfd;
            break;
        }
    }
    
}

// initialise players in the 'players' array structure 
// can be used to reset players
void initialise_players(player * players)
{
    for (int i=0; i<2; i++)
    {
        player new_player;
        new_player = (player){.username = NULL, 
                              .username_length = 0,
                              .player_state = WELCOME,
                              .num_guess_words = 0,
                              .socket_no = -1};
        players[i] = new_player;
    }
    clear_players_guess_words(players);
}

// retrieve the correct player state based on certain conditions:
// (incoming http request, game state, and previous player's state)
PLAYER_STATE update_player_state(char * request, char * url, GAME_STATE game_state, PLAYER_STATE prev_state) 
{
    if (game_state == RAGEQUIT)
    {
        return GAMEOVER;
    }
    else if (strstr(request, "user=")) 
    {
        return START;
    }
    else if (strstr(request, "quit=")) 
    {
        return GAMEOVER;
    }
    else if (strstr(request, "keyword="))
    {
        if (prev_state == ENDGAME)
        {
            return ENDGAME;
        }
        else 
        {
            switch (game_state) 
            {
                case (ONEOFF) :
                    return DISCARDED;

                case (GAMEONBABY) :
                    return ACCEPTED;    

                case (RAGEQUIT) :
                    return GAMEOVER;
                    
                case (GAMEENDED) :
                    return ENDGAME;        
            }
        }
        
    }
    else 
    {
        if (strncmp(url, HTTP_START_URL_PATH, 13) == 0)
        {
            return FIRST_TURN;
        }
        else 
        {
            return WELCOME;
        }
    }
}



// find and returns the the player's index no in the 'players' array 
// based on socket no
// returns -1 if none can be found
int get_player_index(player * players, int sockfd) 
{
    for (int i=0; i<2; i++) {
        if (players[i].socket_no == sockfd) {
            return i;
        }
    }
    return -1;
}

// add keyword submitted to the player's guess_words property
// also increment the player's num_guess_words by 1
void add_keyword_to_player(player * players, int p_index, char * keyword) 
{
    int num_word = players[p_index].num_guess_words;
    strcpy(players[p_index].guess_words[num_word], keyword);
    players[p_index].num_guess_words += 1;
}

// use to compare the keywords between the players' guess_words array
// returns true if there's a keyword match found, false otherwise
bool keywords_cmp(player * players)
{
    if (players[0].num_guess_words > 0 && players[1].num_guess_words > 0)
    {
        for (int i=0; i<players[0].num_guess_words; i++) 
        {
            char * i_word = players[0].guess_words[i];
            for (int j=0; j<players[1].num_guess_words;j++)
            {
                char * j_word = players[1].guess_words[j];
                int max_len = 0;
                if (strlen(i_word) > strlen(j_word))
                {
                    max_len = strlen(i_word);
                }
                else if (strlen(i_word) < strlen(j_word))
                {
                    max_len = strlen(j_word);
                }
                else 
                {
                    max_len = strlen(i_word);
                }
                if (strncmp(i_word, j_word, max_len) == 0)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

// change the state of both players to ENDGAME
void end_game(player * players) 
{
    for (int i=0; i<2; i++)
    {
        players[i].player_state = ENDGAME;
    }
}

// Clearing the both of the players' guess_words array
// also reset the players' num_guess_word to 0
void clear_players_guess_words(player * players) 
{
    for (int i=0; i<2; i++) 
    {
        memset(players[i].guess_words, 0, sizeof(players[i].guess_words[0][0]) * MAX_NUM_GUESSWORDS * MAX_CHAR_PER_GUESSWORD);
        players[i].num_guess_words = 0;
    }
}

// Check if both states of the players are GAMEOVER
bool served_both_players_gameover(player * players)
{
    for (int i=0; i<2; i++) 
    {
        if (players[i].socket_no != -1) {
            if (players[i].player_state != GAMEOVER) 
            {
                return false;
            }
        }
    }
    return true;
}


// ------------------------------------------------------------ //
/* game state helper function */

// find the game state based on previous game state and players' states
// returns the appropriate game state
GAME_STATE update_game_state(GAME_STATE game_state, player * players)
{
    // get players' states
    PLAYER_STATE stateA = players[0].player_state;
    PLAYER_STATE stateB = players[1].player_state;

    // ----------------- DEBUG -------------------
    if (DEBUG) 
    {
        printf("Player %d : ", players[0].socket_no);
        print_player_state(stateA);
        printf("Player %d : ", players[1].socket_no);
        print_player_state(stateB);
        printf("\n");
    }
    // -------------------------------------------

    // game state logic handling and returns the appropriate game state
    if (stateA == GAMEOVER || stateB == GAMEOVER)
    {
        return RAGEQUIT;
    }
    else if ((stateA == FIRST_TURN || stateA == ACCEPTED || stateA == DISCARDED) &&
        (stateB == FIRST_TURN || stateB == ACCEPTED || stateB == DISCARDED))
    {
        return GAMEONBABY;
    }
    else if ((stateA == FIRST_TURN || stateA == DISCARDED) || 
             (stateB == FIRST_TURN || stateB == DISCARDED) ||
             ((stateA == FIRST_TURN || stateA == DISCARDED) && stateB == ENDGAME) ||
             ((stateB == FIRST_TURN || stateB == DISCARDED) && stateA == ENDGAME))
    {
        return ONEOFF;
    }
    else if ((stateA == ENDGAME && stateB != FIRST_TURN) || 
             (stateB == ENDGAME && stateA != FIRST_TURN))
    {
        return GAMEENDED;
    }
}


// ------------------------------------------------------------ //
/* For debug purposes */

void print_player_state(PLAYER_STATE player_state)
{
    switch (player_state) 
    {
        case (WELCOME) :
            printf("WELCOME");
            break;
        case (START) :
            printf("START");
            break;
        case (FIRST_TURN) :
            printf("FIRST_TURN");
            break;
        case (DISCARDED) :
            printf("DISCARDED");
            break;
        case (ACCEPTED) :
            printf("ACCEPTED");
            break;
        case (ENDGAME) :
            printf("ENDGAME");
            break;
        case (GAMEOVER) :
            printf("GAMEOVER");
            break;
    }
    printf("\n");
} 

void print_game_state(GAME_STATE game_state) 
{
    switch (game_state) 
    {
        case (INITIAL) :
            printf("INITIAL");
            break;
        case (ONEOFF) :
            printf("ONEOFF");
            break;
        case (GAMEONBABY) :
            printf("GAMEONBABY");
            break;
        case (RAGEQUIT) :
            printf("RAGEQUIT");
            break;
        case (GAMEENDED) :
            printf("GAMEENDED");
            break;
    }
    printf("\n");
}

// End of file //