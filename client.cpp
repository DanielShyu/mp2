#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <cstring>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <time.h>
#include <netdb.h>

#define SBCP_VERSION        3
// Message types
#define SBCP_MSG_JOIN            2
#define SBCP_MSG_FWD             3
#define SBCP_MSG_SEND            4
// Bonus
#define SBCP_MSG_NAK             5
#define SBCP_MSG_OFFLINE         6
#define SBCP_MSG_ACK             7
#define SBCP_MSG_ONLINE          8
#define SBCP_MSG_IDLE            9
// Attribute types
#define ATTR_REASON         1
#define ATTR_USERNAME       2
#define ATTR_CLIENT_COUNT   3
#define ATTR_MESSAGE        4
// Max allowed 
#define MAX_USERNAME_LEN    16
#define MAX_MESSAGE_LEN     512
#define MAX_REASON_LEN      32
#define SBCP_HDR_LEN        4
#define ATTR_HDR_LEN        4
#define MAX_BUF             1024
#define IDLE_TIMEOUT_SEC    10   // Bonus Feature 2
using namespace std;
struct __attribute__((__packed__)) sbcp_header {
    uint16_t vrsn_type;   // Version(9b) | Type(7b)
    uint16_t length;      // length all the whole message(including the header)
};
struct __attribute__((__packed__)) sbcp_attr_header {
    uint16_t type;        
    uint16_t length;      // attribute length, including header
};

//Header Encoding
void encode_header(struct sbcp_header *hdr, int type, int total_len) {
    uint16_t vt = (SBCP_VERSION << 7) | (type & 0x7F); 
    hdr->vrsn_type = htons(vt);
    hdr->length    = htons(total_len);
    return ; 
}
void decode_header(const struct sbcp_header *hdr, int *version, int *type, int *length) {
    uint16_t vt = ntohs(hdr->vrsn_type);
    *version = (vt & 0xFF80)>>7 ; 
    *type    = (vt & 0x7F) ; 
    *length  = ntohs(hdr->length);
}

//Attribute Encoding 
int append_attr(char *buf, int offset, int buf_size,
                uint16_t attr_type, const char *payload, int payload_len) {
    int total = payload_len + ATTR_HDR_LEN;// attr header + payload
    if (payload_len < 0 || offset + total > buf_size) { 
        fprintf(stderr, "invalid attribute length\n");
        return -1;
    }
    struct sbcp_attr_header *ah = (struct sbcp_attr_header *)(buf + offset);
    ah->type   = htons(attr_type);
    ah->length = htons(total);
    memcpy(buf + offset + ATTR_HDR_LEN, payload, payload_len);
    return total; 
}

//Build Message based on type

// Join
int build_join(char *buf, int buf_size, const char *username) {
    int name_len = strlen(username) ; 
    if(name_len>MAX_USERNAME_LEN||name_len==0){
        fprintf(stderr,"Valid User name length is between 1 to 16\n");
        return -1;
    }

    int offset = SBCP_HDR_LEN;
    int n = append_attr(buf,offset,buf_size,ATTR_USERNAME,username,name_len);
    if (n<0) {
        fprintf(stderr, "fail to build attribute header\n");
        return -1;
    }
    int total = SBCP_HDR_LEN+ATTR_HDR_LEN+name_len;
    encode_header((sbcp_header*)(buf), SBCP_MSG_JOIN, total);
    return total;
}

// Send Message
int build_send(char *buf, int buf_size, const char *msg, int msg_len) {
    //valide length check
    if(msg_len>MAX_MESSAGE_LEN){
        fprintf(stderr,"Valid message length is capped by 512\n");
        return -1;
    }

    int offset = SBCP_HDR_LEN;
    int n = append_attr(buf,offset,buf_size,ATTR_MESSAGE,msg,msg_len);
    if (n<0) {
        fprintf(stderr, "fail to build message header\n");
        return -1;
    }
    int total = SBCP_HDR_LEN+ATTR_HDR_LEN+msg_len;
    encode_header((sbcp_header*)(buf), SBCP_MSG_SEND, total);
    return total;
}

// Bonus: Idle
int build_idle(char *buf, int buf_size) {
    if(buf_size < SBCP_HDR_LEN){ 
        fprintf(stderr, "Invalid Buffersize\n");
        return -1;
    }
    encode_header((sbcp_header*)(buf),SBCP_MSG_IDLE,SBCP_HDR_LEN) ; 
    return SBCP_HDR_LEN; 
}

//send message
int send_all(int sockfd, const char *buf, int len) {
    int sent = 0;
    while (sent<len) {
        int n = send(sockfd, buf+sent, len-sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno==EINTR) {
                continue;
            }
            perror("send");
            return -1;
        }
        sent += n ; 
    }
    return len;
}

// Receive message from server
void handle_server_message(const char *buf, int n) {
    // check length(screen for short message)
    if (n<SBCP_HDR_LEN) {
        fprintf(stderr, "message too short, discarded\n");
        return;
    }
    int version, type, length;
    decode_header((const sbcp_header*)buf, &version, &type, &length);
    // version confirm
    if (version!=SBCP_VERSION) {
        fprintf(stderr, "unknown version %d, discarded\n", version);
        return;
    }
    // length confirm
    if (length!=n) {
        fprintf(stderr, "length mismatch (header %d, read %d), discarded\n", length, n);
        return;
    }
    // attribute check
    int pos = SBCP_HDR_LEN;
    char username[MAX_USERNAME_LEN + 1] = "";
    char message[MAX_MESSAGE_LEN + 1]   = "";
    while (pos<length) {
        // trivial length check
        if (length-pos<ATTR_HDR_LEN) {
            fprintf(stderr, "truncated attribute, discarded\n");
            return;
        }
        const struct sbcp_attr_header *ah = (const struct sbcp_attr_header *)(buf + pos);
        int attr_type = ntohs(ah->type);
        int attr_len  = ntohs(ah->length);
        // check for valid attribute len, avoid infinite loop
        if (attr_len<ATTR_HDR_LEN||pos+attr_len>n) {
            fprintf(stderr, "invalid attribute length, discarded\n");
            return;
        }
        const char *payload = buf+pos+ATTR_HDR_LEN;
        int payload_len     = attr_len-ATTR_HDR_LEN;
        if (attr_type == ATTR_USERNAME) {
            if (payload_len > MAX_USERNAME_LEN) {          
                fprintf(stderr, "username too long, discarded\n");
                return;
            }
            memcpy(username, payload, payload_len);         
            username[payload_len] = '\0';                   
        }
        else if (attr_type == ATTR_MESSAGE) {
            if (payload_len > MAX_MESSAGE_LEN) {          
                fprintf(stderr, "Message too long, discarded\n");
                return;
            }
            memcpy(message, payload, payload_len);         
            message[payload_len] = '\0';     
        }
        pos += attr_len;
    }

    switch (type) {
        case SBCP_MSG_FWD:
            if(username[0] == '\0'||message[0]=='\0'){
                fprintf(stderr, "Invalid format, either empty message or username\n");
                break;
            }
            printf("%s: %s\n", username, message);
            break;
        case SBCP_MSG_ONLINE:
            if(username[0] == '\0'){
                fprintf(stderr, "empty username\n");
                break;
            }
            printf("%s is online\n", username);
            break;
        case SBCP_MSG_OFFLINE:
            if(username[0] == '\0'){
                fprintf(stderr, "empty username\n");
                break;
            }
            printf("%s is offline\n", username);
            break;
        case SBCP_MSG_IDLE:
            if(username[0] == '\0'){
                fprintf(stderr, "empty username\n");
                break;
            }
            printf("%s is idle\n", username);
            break;
        default:
            fprintf(stderr, "unknown message type %d, ignored\n", type);
            break;
    }
}
//bonus ipv6
int connect_to_server_ipv6(const char *host, const char *port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints)); 
    hints.ai_family   = AF_UNSPEC;    //support both ipv4 and ipv6
    hints.ai_socktype = SOCK_STREAM;  //tcp
    struct addrinfo *res;
    int err = getaddrinfo(host,port,&hints,&res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }
    int sockfd = -1;
    struct addrinfo *p;
    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0) {
            continue;                
        }
        if (connect(sockfd,p->ai_addr,p->ai_addrlen) == 0) {
            break;                   
        }
        close(sockfd);                
        sockfd = -1;
    }
    freeaddrinfo(res);

    if (sockfd < 0) {
        fprintf(stderr, "could not connect to %s:%s\n", host, port);
        return -1;
    }
    return sockfd;
}
//bonus ipv6
int connect_to_server(const char *ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd==-1){
        perror("socket");
        return -1; 
    }
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP: %s\n", ip);
        close(sockfd);
        return -1;
    }
    //establish connection
    int connectStatus = connect(
        sockfd,
        reinterpret_cast<sockaddr*>(&servaddr),
        sizeof(servaddr)
    );
    if (connectStatus < 0 ){
        perror("connect");
        close(sockfd);
        return -1 ;
    } //connection fail 
    return sockfd;
}

int main(int argc, char **argv) {
    //error handle
    signal(SIGPIPE, SIG_IGN);
    // check valid usage
    if(argc!=4){
        fprintf(stderr, "Usage: %s <username> <server_ip> <server_port>\n", argv[0]);
        return 1; 
    }
    // build connection
    const char* username = argv[1]; 
    const char*  server_ip = argv[2]; 
    const char*  server_port = argv[3];
    int portNumber = atoi(server_port) ; 
    if(portNumber>65535||portNumber<1){
        fprintf(stderr, "Invalid port number\n");
        return 1; 
    }
    //int sockfd = connect_to_server(server_ip,portNumber); 
    int sockfd = connect_to_server_ipv6(server_ip,server_port);
    if(sockfd<0){
        fprintf(stderr, "Connection Fail\n");
        return 1 ; 
    }
    // join the server
    char buf[MAX_BUF];
    int len = build_join(buf,sizeof(buf),username);
    if(len<0){
        close(sockfd);
        return 1; 
    }
    int sent = send_all(sockfd,buf,len);
    if(sent<0){
        close(sockfd);
        return 1 ; 
    }
    //bonus: time out 
    time_t last_active = time(NULL);   // JOIN 完成的時間，當作第一次活動
    bool idle_sent = false;  
    //select between listen and send
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);   
        FD_SET(sockfd, &readfds);
        //bonus: timeout
        struct timeval tv;
        struct timeval *tvp = NULL;        
        if (!idle_sent) {
            int remaining = IDLE_TIMEOUT_SEC-(time(NULL)-last_active);
            if (remaining < 0) {
                remaining = 0;             
            }
            tv.tv_sec  = remaining;
            tv.tv_usec = 0;
            tvp = &tv;               
        }
        //bonus: timeout end        
        int maxfd = sockfd;               
        int ready = select(maxfd + 1, &readfds, NULL, NULL, tvp);
        if(ready==-1){
            if(errno==EINTR){
                continue;
            }
            perror("select");
            close(sockfd);
            return 1; 
        }
        //bonus timeout
        //send idle
        if (ready == 0) {
            int idle_len = build_idle(buf,sizeof(buf));
            if (idle_len < 0) {
                close(sockfd);
                return 1;
            }
            if (send_all(sockfd,buf,idle_len) < 0) {
                close(sockfd);
                return 1;
            }
            idle_sent = true; 
            continue;   // 沒有 fd 就緒，直接進入下一輪
        }
        //bonus timeout end
        // message ready to send
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char line[MAX_MESSAGE_LEN + 2] ; 
            if (fgets(line, sizeof(line), stdin) == NULL) {
                fprintf(stderr,"EOF detect, disconnected from the server\n");
                close(sockfd);
                return 0 ; 
            }
            int l = strlen(line);
            if (l > 0 && line[l - 1] == '\n') {   
                line[l - 1] = '\0';               
                l--;                              
            }
            //empty input, enter next loop
            if(l==0){
                continue; 
            }
            int build_len = build_send(buf,sizeof(buf),line,l);
            if(build_len<0){
                continue; //message build fail, wait for the next input
            }
            int sent = send_all(sockfd,buf,build_len); 
            if(sent<0){
                close(sockfd);//message send fail, disconnect.
                return 1; 
            }
            last_active = time(NULL);
            idle_sent   = false;
        }
        // message arrive
        if (FD_ISSET(sockfd, &readfds)) {
            char rbuf[MAX_BUF];
            int r = read(sockfd, rbuf, sizeof(rbuf));
            if(r==0){
                fprintf(stderr, "Server closed\n");
                close(sockfd);
                return 1 ; 
            }
            if(r<0){
                if(errno==EINTR){
                    continue;
                }
                perror("read");
                close(sockfd);
                return 1 ; 
            }
            handle_server_message(rbuf,r);
        }
    }
    
    close(sockfd);
    return 0;
}

