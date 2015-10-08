// ********************************************************************************
// ********  More information: http://www.codebase.eu/                       ******
// ********                                                                  ******
// ********  Compiling : g++ server.cpp -o server -pthread                   ******
// ********************************************************************************


#include <arpa/inet.h> 	// for inet_ntop function
#include <netdb.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <regex.h>
#include <getopt.h>
#include <limits.h>
#include "llist2.h"
#include "bool.h"
#include "colors.h"
#define MAX_THREADS     3      /* Max. number of concurrent chat sessions */
#define APP_NAME "MULTI-THREADED CHAT SERVER"
#define APP_VERSION "VERSION 1.0"

struct addrinfo server, *res = NULL;


struct ServerStats_t {
	int max_threads;
	int num_of_active_connections;
	int num_of_conn_dropped;
	int num_of_clients_serviced;	
};

struct ServerStats_t serverStats;

#define BUF_SIZE_1K 1024

//server functions
int server_socket_init() ;
int server_accept_client(int server_fd);
int server_write_welcome_client(int fd);
void server_read_client(void *arg) ;
void server_wait_client(int server_fd) ;
struct list_entry list_start;
void process_msg(char *message, int self_sockfd);
void send_broadcast_msg(char* format, ...);
void send_private_msg(char* nickname, char* format, ...);
void chomp(char *s);
void change_nickname(char *oldnickname, char *newnickname);
//server constants
const char *PORT = "12345"; 		// port numbers 1-BUF_SIZE_1K are probably reserved by your OS
const int MAXLEN = BUF_SIZE_1K;   	// Max length of a message.
const int MAXFD = 10;       		// Maximum file descriptors to use. Equals maximum clients.
const int BACKLOG = 10;     		// Number of connections that can wait in que before they be accept()ted

// This needs to be declared volatile because it can be altered by an other thread. Meaning the compiler cannot
// optimise the code, because it's declared that not only the program can change this variable, but also external
// programs. In this case, a thread.
int server_fd = 0;
int curr_thread_count = 0;
volatile fd_set the_state;
pthread_mutex_t mutex_state = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t curr_thread_count_mutex = PTHREAD_MUTEX_INITIALIZER;
int main(int argc, char argv[])
{
	int ret = 0;
	struct sockaddr_in client_address;
	int client_fd = 0;
	printf("\n");
	printf("Welcome to Chat Server....\n"); 
	memset(&serverStats, 0, sizeof(struct ServerStats_t));
	serverStats.max_threads = MAX_THREADS;
	// start the main and make the server listen on port 12345
	if (server_socket_init() == -1)
	{
		printf("An error occured. Closing program \n");
		return 1 ;
	}

	//server_wait_client(server_fd);
	while(1)
	{
		//int rfd;
		int *arg; 
		char ipstr[INET6_ADDRSTRLEN];
        	int port;
        	int new_sd;
		int ret =0;
        	struct sockaddr_storage remote_info ;
		pthread_t threads[MAX_THREADS];
        	socklen_t addr_size;
        	addr_size = sizeof(addr_size);
        	new_sd = accept(server_fd, (struct sockaddr *) &remote_info, &addr_size);
        	getpeername(new_sd, (struct sockaddr*)&remote_info, &addr_size);

        	// deal with both IPv4 and IPv6:
        	if (remote_info.ss_family == AF_INET)
        	{
			struct sockaddr_in *s = (struct sockaddr_in *)&remote_info;
			port = ntohs(s->sin_port);
			inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
        	} 
		else
        	{
                // AF_INET6
                struct sockaddr_in6 *s = (struct sockaddr_in6 *)&remote_info;
                port = ntohs(s->sin6_port);
                inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
        	}	

	        printf("\n");
		if(new_sd >= 0)
		{
			printf("Server accepted new connection on socket id :%d\n", new_sd);
			/* A connection between a client and the server has been established.
			 * Now create a new thread and handover the client_sockfd
			 */
			pthread_mutex_lock(&curr_thread_count_mutex);
			if (curr_thread_count < MAX_THREADS)
			{
				/* Prepare client infos in handy structure */
				client_info *ci = (client_info *)malloc(sizeof(client_info));
				ci->sockfd = new_sd;
				ci->address = remote_info;
				sprintf(ci->nickname, "Client_%d\n", new_sd);

				/* Add client info to linked list */
				llist_insert(&list_start, ci);
				llist_show(&list_start);
				/* System Monitoring Statistics Counter : Increments the number of active_connections 
				   counter. Shows the number of active clients*/
				serverStats.num_of_active_connections++;

				/* Pass client info and invoke new thread */
				ret = pthread_create(&threads[curr_thread_count],
						NULL,
						(void *)&server_read_client,
						(void *)&new_sd); /* only pass socket id ? */
				if (ret == 0)
				{
					serverStats.num_of_clients_serviced++;
					pthread_detach(threads[curr_thread_count]);
					curr_thread_count++;

					/* Notify server and clients */
					printf("User %s joined the chat.\n", ci->nickname);
					printf("Connections used: %d of %d\n", curr_thread_count, MAX_THREADS);
				}
				else
				{
					serverStats.num_of_conn_dropped++;
					llist_remove_by_sockfd(&list_start, new_sd);
					serverStats.num_of_active_connections--;
					free(ci);
					close(new_sd);
				}
			}
			else
			{
				serverStats.num_of_conn_dropped++;
				printf("Max. connections reached. Connection limit is %d. Connection dropped.\n", MAX_THREADS);
				close(new_sd);
			}
			pthread_mutex_unlock(&curr_thread_count_mutex);
		}
		else
		{
			/* Connection could not be established. Post error and exit. */
			//perror(strerror(errno));
			printf("Error creating socket\n");
			exit(-1);
		}
	}

	return 0;
}

int server_socket_init(void)
{

	int yes = 1;
	// first, load up address structs with getaddrinfo():
	memset(&server, 0, sizeof(server));
	/* Initialize client_info list */
        llist_init(&list_start);
	//server.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
	//server_fd = socket(AF_INET6, SOCK_STREAM, AI_PASSIVE);
	server.ai_family = AF_INET6;
	server.ai_socktype = SOCK_STREAM;
	server.ai_flags = AI_PASSIVE;     // fill in my IP for me

	getaddrinfo(NULL, PORT, &server, &res);

	server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if(server_fd == -1)
	{
		printf("Unsuccessful socket creation\n");
	}
	else 
	{
		printf("Server Socket Created Successfully \n");
	}	
	// Avi check on this Sock reusable param.
	if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) != 0)
	{
		printf("Error Setting Socket Reusable\n");
		return -1;
	}
	if(bind(server_fd, res->ai_addr, res->ai_addrlen) != 0)
	{
		printf("Error Binding\n");
		return -1 ;
	}

	if(listen(server_fd, BACKLOG) != 0)
	{
		printf("Error Listening\n");
	}
	return 0;
}

void server_read_client(void *arg)
/// This function runs in a thread for every client, and reads incoming data.
/// It also writes the incoming data to all other clients.
{
        char buffer[BUF_SIZE_1K];
        char message[BUF_SIZE_1K];
        int ret = 0;
        int len = 0;
        int socklen = 0;
        struct list_entry *list_entry;
        fd_set readfds;
        int sockfd = 0;

        /* Load associated client info */
        sockfd = *(int *)arg;
        list_entry = llist_find_by_sockfd(&list_start, sockfd);

        memset(message, 0, BUF_SIZE_1K);
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        /* Send welcome message */
	server_write_welcome_client(sockfd);
        /* Announce to others using broadcast */
        send_broadcast_msg("\n%sUser: %s joined the chat.%s\n\r\n", color_magenta, list_entry->client_info->nickname, color_normal);

        /* Process requests */
        while (1)
        {
                ret = select(FD_SETSIZE, &readfds, (fd_set *)0,
                        (fd_set *)0, (struct timeval *) 0);
                if (ret == -1)
                {
                        printf("Error calling select() on thread.\n");
		  }
                else
                {
                        /* Read data from stream */
                        memset(buffer, 0, BUF_SIZE_1K);
                        socklen = sizeof(list_entry->client_info->address);
                        len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                (struct sockaddr *)&list_entry->client_info->address, (socklen_t *)&socklen);

                        /* Copy receive buffer to message buffer */
                        if (sizeof(message) - strlen(message) > strlen(buffer))
                        {
                                strcat(message, buffer);
                        }

                        /* Check if message buffer contains a full message. A full message
                         * is recognized by its terminating \n character. If a message
                         * is found, process it and clear message buffer afterwards.
                         */
                        char *pos = strstr(message, "\n");
                        if (pos != NULL)
                        {
                                chomp(message);
                                /*Code will be useful for Our Project */
                                /* Process message */
                                process_msg(message, sockfd);
                                memset(message, 0, BUF_SIZE_1K);
                        }
                        else
                        {
                                printf("Message still incomplete....\n");
                        }
                }
        }
}

void send_broadcast_msg(char* format, ...)
{
        int socklen = 0;
        //client_info *cur = NULL;
        struct list_entry *cur = NULL;
        va_list args;
        char buffer[BUF_SIZE_1K];
        memset(buffer, 0, BUF_SIZE_1K);

        /* Prepare message */
        va_start(args, format);
        vsprintf(buffer, format, args);
        va_end(args);

        cur = &list_start;
        while (cur != NULL)
        {
                /* Lock entry */
                pthread_mutex_lock(cur->mutex);

                /* Send message to client */
                if (cur->client_info != NULL)
                {
                        socklen = sizeof(cur->client_info->address);
                        sendto(cur->client_info->sockfd, buffer, strlen(buffer), 0,
                                (struct sockaddr *)&(cur->client_info->address), (socklen_t)socklen);
                }

                /* Unlock entry */
                pthread_mutex_unlock(cur->mutex);

                /* Load next index */
                cur = cur->next;
        }
}

/*
 * Process a chat message coming from a chat client.
 */
void process_msg(char *message, int self_sockfd)
{
        char buffer[BUF_SIZE_1K];
        char statsBuf[BUF_SIZE_1K];
        regex_t regex_quit;
        regex_t regex_nick;
        regex_t regex_msg;
        //regex_t regex_me;
        regex_t regex_stats;
        int ret;
        char newnick[20];
        char oldnick[20];
        char priv_nick[20];
        struct list_entry *list_entry = NULL;
        struct list_entry *nick_list_entry = NULL;
        struct list_entry *priv_list_entry = NULL;
        int processed = FALSE;
        size_t ngroups = 0;
        size_t len = 0;
        regmatch_t groups[3];

        memset(buffer, 0, BUF_SIZE_1K);
        memset(newnick, 0, 20);
        memset(oldnick, 0, 20);
        memset(priv_nick, 0, 20);

        /* Load client info object */
        list_entry = llist_find_by_sockfd(&list_start, self_sockfd);
       /* Remove \r\n from message */
        chomp(message);

        /* Compile regex patterns */
        regcomp(&regex_quit, "^/logout$", REG_EXTENDED);
        regcomp(&regex_nick, "^/chatName ([a-zA-Z0-9_]{1,19})$", REG_EXTENDED);
        regcomp(&regex_msg, "^/privateMessage ([a-zA-Z0-9_]{1,19}) (.*)$", REG_EXTENDED);
        regcomp(&regex_stats, "^/showStats$", REG_EXTENDED);

        /* Check if user wants to quit */
        ret = regexec(&regex_quit, message, 0, NULL, 0);
        if (ret == 0)
        {
                /* Notify */
                send_broadcast_msg("\n%sUser: %s has left the chat server.%s\n\r\n",
                        color_magenta, list_entry->client_info->nickname, color_normal);
                printf("\nUser: %s has left the chat server.\n", list_entry->client_info->nickname);
                pthread_mutex_lock(&curr_thread_count_mutex);
                curr_thread_count--;
                pthread_mutex_unlock(&curr_thread_count_mutex);

                /* Remove entry from linked list */
                llist_remove_by_sockfd(&list_start, self_sockfd);
		serverStats.num_of_active_connections--;

                /* Disconnect client from server */
                close(self_sockfd);

                /* Free memory */
                regfree(&regex_quit);
                regfree(&regex_nick);
                regfree(&regex_msg);
                regfree(&regex_stats);
               /* Terminate this thread */
                pthread_exit(0);
        }

        /* Check if user wants to change nick */
        ngroups = 2;
        ret = regexec(&regex_nick, message, ngroups, groups, 0);
        if (ret == 0)
        {
                processed = TRUE;

                /* Extract nickname */
                len = groups[1].rm_eo - groups[1].rm_so;
                strncpy(newnick, message + groups[1].rm_so, len);
                strcpy(oldnick, list_entry->client_info->nickname);

                strcpy(buffer, "User: ");
                strcat(buffer, oldnick);
                strcat(buffer, " is now known as - ");
                strcat(buffer, newnick);

                /* Change nickname. Check if nickname already exists first. */
                nick_list_entry = llist_find_by_nickname(&list_start, newnick);
                if (nick_list_entry == NULL)
                {
                        change_nickname(oldnick, newnick);
                        send_broadcast_msg("\n\n%s\n%s\n%s\n\r\n", color_yellow, buffer, color_normal);
                        printf("Broadcast Message is sent for username change\n"); 
                }
                else
                {
                        send_private_msg(oldnick, "%sCHATSRV: Cannot change nickname. Nickname already in use.%s\r\n",
                                color_yellow, color_normal);
                        printf("Private message from CHATSRV to %s: Cannot change nickname. Nickname already in use\n",
                                oldnick);
                }
        }

        /* Check if user wants to transmit a private message to another user */
        ngroups = 3;
        ret = regexec(&regex_msg, message, ngroups, groups, 0);
        if (ret == 0)
        {
                processed = TRUE;

                /* Extract nickname and private message */
                len = groups[1].rm_eo - groups[1].rm_so;
                memcpy(priv_nick, message + groups[1].rm_so, len);
                len = groups[2].rm_eo - groups[2].rm_so;
                memcpy(buffer, message + groups[2].rm_so, len);

                /* Check if nickname exists. If yes, send private message to user. 
                 * If not, ignore message.
                 */
                priv_list_entry = llist_find_by_nickname(&list_start, priv_nick);
                if (priv_list_entry != NULL)
                {
                        send_private_msg(priv_nick, "%s %s:\n%s %s %s %s\r\n", color_green, list_entry->client_info->nickname,
                                color_normal, color_red, buffer, color_normal);
                        printf("Private message from %s to %s: %s\n",
                                list_entry->client_info->nickname, priv_nick, buffer);
                }
        }

        /* Check if user wants to say something about himself */
        ngroups = 2; //TODO:
        ret = regexec(&regex_stats, message, ngroups, groups, 0);
        if (ret == 0)
        {
               processed = TRUE;
              int socklen = sizeof(list_entry->client_info->address);

                printf("%s requested the client list\n", list_entry->client_info->nickname);

                char **nicks = malloc(sizeof(*nicks) * 1000);
                int i;
                for (i = 0; i < 1000; i++)
                        nicks[i] = malloc(sizeof(**nicks) * 30);
                int count = llist_get_nicknames(&list_start, nicks);

                memset(buffer, 0, BUF_SIZE_1K);
                for (i = 0; i < count; i++) {
                        sprintf(buffer, "%s%s%s%s", buffer, color_magenta, nicks[i], color_normal);
                        if(i != (count - 1)) sprintf(buffer, "%s, ", buffer);
                        if(i == (count - 1)) sprintf(buffer, "%s\r\n", buffer);
                        free(nicks[i]);
                }


		memset(statsBuf, 0, BUF_SIZE_1K);	
		sprintf(statsBuf, "Max threads: %d\nNumber of active connections: %d\nNumber of clients serviced: %d\nNumber of clients dropped:%d\n",serverStats.max_threads, serverStats.num_of_active_connections,serverStats.num_of_clients_serviced, serverStats.num_of_conn_dropped);	
		printf("Server Stats : \n%s\n", statsBuf);		
                sendto(list_entry->client_info->sockfd, buffer, strlen(buffer), 0,
                                (struct sockaddr *)&(list_entry->client_info->address), (socklen_t)socklen);
                sendto(list_entry->client_info->sockfd, statsBuf, strlen(statsBuf), 0,
                                (struct sockaddr *)&(list_entry->client_info->address), (socklen_t)socklen);
                free(nicks);
        }

        /* Broadcast message */
        if (processed == FALSE)
        {
                send_broadcast_msg("%s%s:%s %s\r\n", color_green, list_entry->client_info->nickname, color_normal, message);
                printf("%s: %s", list_entry->client_info->nickname, message);
        }

        /* Dump current user list */
        llist_show(&list_start);

        /* Free memory */
        regfree(&regex_quit);
        regfree(&regex_nick);
        regfree(&regex_msg);
	regfree(&regex_stats);
}

/*
 * Sends a private message to a user.
 */
void send_private_msg(char* nickname, char* format, ...)
{
        int socklen = 0;
        struct list_entry *cur = NULL;
        va_list args;
        char buffer[BUF_SIZE_1K];

        memset(buffer, 0, BUF_SIZE_1K);

        /* Prepare message */
        va_start(args, format);
        vsprintf(buffer, format, args);
        va_end(args);

        cur = llist_find_by_nickname(&list_start, nickname);

        /* Lock entry */
        pthread_mutex_lock(cur->mutex);

        /* Send message to client */
        socklen = sizeof(cur->client_info->address);
        sendto(cur->client_info->sockfd, buffer, strlen(buffer), 0,
                (struct sockaddr *)&(cur->client_info->address), (socklen_t)socklen);

        /* Unlock entry */
	pthread_mutex_unlock(cur->mutex);
}


void chomp(char *s)
{
        while(*s && *s != '\n' && *s != '\r') s++;

        *s = 0;
}


/*
 * Changes the nickname of an existing chat user.
 */
void change_nickname(char *oldnickname, char *newnickname)
{
        struct list_entry *list_entry = NULL;
        client_info *ci_new = NULL;
        int idx = 0;

        printf("\noldnickname = %s, newnickname = %s\n", oldnickname, newnickname);

        /* Load client_info element */
        list_entry = llist_find_by_nickname(&list_start, oldnickname);

        /*Lock entry */
        pthread_mutex_lock(list_entry->mutex);

        printf("\nClient Nickname = %s\n",list_entry->client_info->nickname);

        /* Update nickname */
        strcpy(list_entry->client_info->nickname, newnickname);

        /* Unlock entry */
        pthread_mutex_unlock(list_entry->mutex);
}

int server_write_welcome_client(int fd)
// This function will send data to the clients fd.
        // data contains the message to be send
{
        int ret;
        va_list args;
        char data[BUF_SIZE_1K];
        memset(data, 0, BUF_SIZE_1K);
        sprintf(data, "\n%s/------------------------------------------------------------/%s\r\n", color_white, color_normal);
        ret = send(fd, data, strlen(data),0);
        memset(data, 0, BUF_SIZE_1K);
        sprintf(data, "%s\t|  W E L C O M E   T O |%s\r\n", color_white, color_normal);
        ret = send(fd, data, strlen(data),0);
        memset(data, 0, BUF_SIZE_1K);
        sprintf(data, "%s\t| %s %s |%s\r\n", color_white, APP_NAME, APP_VERSION, color_normal);
        ret = send(fd, data, strlen(data),0);
        memset(data, 0, BUF_SIZE_1K);
        sprintf(data, "%s\t| CMPE207 PROJECT BY Avinash, Amogh, Nikash, Sharan |%s\r\n", color_white, color_normal);
        ret = send(fd, data, strlen(data),0);
        memset(data, 0, BUF_SIZE_1K);
        sprintf(data, "%s\\-------------------------------------------------------------/%s\r\n", color_white, color_normal);
        ret = send(fd, data, strlen(data),0);
        return 0;

}
                                                               
