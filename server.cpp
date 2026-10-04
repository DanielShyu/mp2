#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/timeb.h>
#include <string>

//define SBCP protocol version and message types
#define SBCP_vrsn 3 //protocol version 3
#define TYPE_JOIN 2 //join message type
#define TYPE_FWD 3 //forward message type
#define TYPE_SEND 4 //send message type
#define TYPE_USERNAME 2 //username attribute type
#define TYPE_MESSAGE 4 //message attribute type
#define TYPE_REASON 1 //reason attribute type
#define TYPE_CLIENT_COUNT 3 //client count attribute type

struct __attribute__((__packed__)) sbcp_header {
    unsigned int vrsn : 9; // 9 bits for version
    unsigned int type : 7; // 7 bits for type
    uint16_t length;    // 16 bits for length
};

struct __attribute__((__packed__)) sbcp_attribute {
    uint16_t type; // 16 bits for type
    uint16_t length; // 16 bits for length
    char payload[0]; // variable-length payload
};

struct username_info{
    int sd;
    std::string username;
};
struct username_info client_username[30]; //array to store username information for each client


const int MAXLEN = 1024;
char buffer[MAXLEN];

int main(int argc, char **argv){
    int listenfd, connfd;
    int addrlen , new_socket , client_socket[30] , max_clients = 30 , activity, i , valread , sd;
	int max_sd;
    struct sockaddr_in address;
    fd_set readfds;// Set of socket descriptors

    //initialise client_username array
    for (int i = 0; i < 30; i++) {
        client_username[i].sd = 0;
        client_username[i].username = "";
    }
    //initialise all client_socket[] to 0 so not checked
    for (i = 0; i < max_clients; i++) 
    {
        client_socket[i] = 0;
    }

    // The server is started with the command line: ./server <server_ip> <server_port> <max_clients>
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port> <max_clients>\n", argv[0]);
        return 1;
    }

    // Convert the port number and max clients from string to integer.
    char *server_ip = argv[1];
    int port = atoi(argv[2]);
    max_clients = atoi(argv[3]);
    if (port <= 0 || max_clients <= 0) {
        fprintf(stderr, "Error: Invalid port or max_clients value.\n");
        return 1;
    }
    
    //create socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return 1;
    }
    
    //setup server address structure
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    // Convert the server IP address to binary form and store it in servaddr.sin_addr
    if (inet_pton(AF_INET, server_ip, &servaddr.sin_addr) <= 0) {
    fprintf(stderr, "Invalid IP address: %s\n", server_ip);
    return 1;
    }

    //bind socket to the specified port
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        close(listenfd);
        return 1;
    }

    //listen for incoming connections
    if (listen(listenfd, MAXLEN) < 0) {
        perror("listen");
        close(listenfd);
        return 1;
    }

    //accept incoming connections
    addrlen = sizeof(struct sockaddr);

    //To Do: use select to handle multiple connections
    while (1) {
        //clear the socket set
        FD_ZERO(&readfds);
 
        //add listening socket to set
        FD_SET(listenfd, &readfds);
        max_sd = listenfd;
		
        //add child sockets to set
        for ( i = 0 ; i < max_clients ; i++)
        {
            //socket descriptor
			sd = client_socket[i];
            
			//if valid socket descriptor then add to read list
			if(sd > 0)
				FD_SET( sd , &readfds);
            
            //highest file descriptor number, need it for the select function
            if(sd > max_sd)
				max_sd = sd;
        }

        //wait for an activity on one of the sockets , timeout is NULL , so wait indefinitely
        activity = select( max_sd + 1 , &readfds , NULL , NULL , NULL);
   
        if ((activity < 0) && (errno!=EINTR)) 
        {
            perror("select error");
        }
         
        //If something happened on the listening socket
        if (FD_ISSET(listenfd, &readfds)) 
        {
            if ((new_socket = accept(listenfd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0)
            {
                perror("accept");
                exit(EXIT_FAILURE);
            }
         
            //inform user of socket number - used in send and receive commands
            printf("New connection , socket fd is %d , ip is : %s , port : %d \n" , new_socket , inet_ntoa(address.sin_addr) , ntohs(address.sin_port));
             
            //add new socket to array of sockets
            for (i = 0; i < max_clients; i++) 
            {
                //if position is empty
				if( client_socket[i] == 0 )
                {
                    client_socket[i] = new_socket;
                    printf("Adding to list of sockets as %d\n" , i);
					break;
                }
            }
        }
         
        //else its some IO operation on some other socket
        for (i = 0; i < max_clients; i++) 
        {
            sd = client_socket[i];
             
            if (FD_ISSET( sd , &readfds)) 
            {
                //Check if it was for closing , and also read the incoming message
                if ((valread = read( sd , buffer, 1024)) == 0)
                {
                    //Somebody disconnected , get his details and print
                    getpeername(sd , (struct sockaddr*)&address , (socklen_t*)&addrlen);
                    printf("Host disconnected , ip %s , port %d \n" , inet_ntoa(address.sin_addr) , ntohs(address.sin_port));
                     
                    //Close the socket and mark as 0 in list for reuse
                    close(sd );
                    client_socket[i] = 0;
                    client_username[i].sd = 0;
                    client_username[i].username = "";
                }
                 
                //handle SBSP message
                else
                {
                    struct sbcp_header *header = (struct sbcp_header *)buffer; //put the buffer as an SBSP header
                    int sbcpheader_type = header->type; //get the SBSP message type
                    int sbcpheader_length = ntohs(header->length); //get the length of the message from the header

                    //check the type of the SBSP message and handle accordingly
                    if(sbcpheader_type == TYPE_JOIN)
                    {
                        struct sbcp_attribute *attr = (struct sbcp_attribute *)(buffer + sizeof(struct sbcp_header)); //point to the attribute section of the message
                        int attr_type = ntohs(attr->type); //get the type of the attribute
                        int attr_length = ntohs(attr->length); //get the length of the attribute

                        //get the length of the user name
                        int username_length = attr_length - sizeof(struct sbcp_attribute);
                        std::string client_name(attr->payload, username_length);
                        
                        //Check if the username is already in use
                        bool is_duplicate = false;
                        for (int i = 0; i < 30; i++) {
                            if (client_username[i].sd != 0 && client_username[i].username == client_name) {
                                is_duplicate = true;
                                break;
                            }
                        }
                        if (is_duplicate) {
                            printf("Duplicate username detected: %s\n", client_name.c_str());
                            close(sd); //close the socket to decline the join request
                            
                            //To do(bonus) return nak
                        } else {
                            //store the username information in the client_username array
                            for (int i = 0; i < 30; i++) {
                                if (client_username[i].sd == 0) {
                                    client_username[i].sd = sd;
                                    client_username[i].username = client_name;
                                    break;
                                }
                            }
                        }

                        //To do(bonus) return ack
                    }
                    else if(sbcpheader_type == TYPE_SEND)
                    {
                        struct sbcp_attribute *attr = (struct sbcp_attribute *)(buffer + sizeof(struct sbcp_header)); //point to the attribute section of the message
                        int attr_type = ntohs(attr->type); //get the type of the attribute
                        int attr_length = ntohs(attr->length); //get the length of the attribute

                        //get the length of the message
                        int message_length = attr_length - sizeof(struct sbcp_attribute);
                        std::string send_message(attr->payload, message_length);
                        //get the sender's name from the client_username
                        std::string sender_name = "";
                        for (int i = 0; i < 30; i++) {
                            if (client_username[i].sd == sd) {
                                sender_name = client_username[i].username;
                                break;
                            }
                        }
                        printf("Message from %s: %s\n", sender_name.c_str(), send_message.c_str());

                        //copy the message text to be sent in FWD
                        char fwd_buffer[1024]; //buffer to hold the forwarded message
                        memset(fwd_buffer, 0, sizeof(fwd_buffer)); //clear the buffer before copying the message

                        int attr_usernamelength = sender_name.length()+sizeof(struct sbcp_attribute);
                        int attr_messagelength = message_length+sizeof(struct sbcp_attribute);
                        int fwd_totallength = attr_usernamelength + attr_messagelength+sizeof(struct sbcp_header);

                        struct sbcp_header *fwd_hdr = (struct sbcp_header *)fwd_buffer;
                        fwd_hdr->type = TYPE_FWD;
                        fwd_hdr->length = htons(fwd_totallength);

                        struct sbcp_attribute *fwd_attr_username = (struct sbcp_attribute *)(fwd_buffer + sizeof(struct sbcp_header));
                        fwd_attr_username->type = htons(TYPE_USERNAME);
                        fwd_attr_username->length = htons(attr_usernamelength);
                        memcpy(fwd_attr_username->payload, sender_name.c_str(), sender_name.length());

                        struct sbcp_attribute *fwd_attr_message = (struct sbcp_attribute *)(fwd_buffer + sizeof(struct sbcp_header) + attr_usernamelength);
                        fwd_attr_message->type = htons(TYPE_MESSAGE);
                        fwd_attr_message->length = htons(attr_messagelength);
                        memcpy(fwd_attr_message->payload, send_message.c_str(), message_length);

                        for (int i = 0; i < 30; i++) {
                            if (client_username[i].sd == sd) {
                                continue; // skip the sender
                            }
                            else if(client_username[i].sd!=0){
                                send(client_username[i].sd, fwd_buffer, fwd_totallength, 0);
                            }
                        }
                    }
                    else
                    {
                        printf("Unknown SBSP message type: %d\n", sbcpheader_type);
                    }
                }
            }
        }
    }
     
    return 0;
    }