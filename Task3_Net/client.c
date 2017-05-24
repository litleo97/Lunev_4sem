#include "common.h"
#define NET_DEBUG
//#define INET_LOOPBACK/**/
#define EXIT_FAILURE 1

long pasreInput(int argc, char** argv)
{
    if (argc != 2)
    {
        printf("Usage: ./command <quantity_of_servers>\n");
        exit(-1);
    }
    
    char* endptr = NULL;
    errno = 0;  
    long threads_req = strtol(argv[1], &endptr, 10);
    if (errno != 0)
    {
        perror("Strtol:");
        exit(errno);
    }
    if (*endptr != '\0')
    {
        printf("2nd arg is not a number\n");
        exit(-1);
    }

    return threads_req;
}

typedef struct server_fd
{
    int fd;
    int threads_avail;
    double left;
    double right;
    double res;

} server_struc_t;

int main(int argc , char *argv[])
{
    const unsigned udp_port = 8886;
    const unsigned tcp_port = 8888;
    const int servers_qty = (int)pasreInput(argc, argv);
    const double left = 0;
    const double right = 20;
    /*UDP - broadcast connection*/
    
    int udp_socket;
    if((udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        perror("UDP client socket creation");
        exit(EXIT_FAILURE);
    } 

    /*get current ip adress*/
    struct ifreq ifr_ip;
    ifr_ip.ifr_addr.sa_family = AF_INET;
    strncpy(ifr_ip.ifr_name, "wlp3s0", IFNAMSIZ-1);
    #ifdef INET_LOOPBACK
    strncpy(ifr_ip.ifr_name, "lo", IFNAMSIZ-1);
    #endif
    if(ioctl(udp_socket, SIOCGIFADDR, &ifr_ip) == -1)
    {
        perror("Getting ip address");
        exit(EXIT_FAILURE);
    }
    struct in_addr ip_addr = ((struct sockaddr_in *)&ifr_ip.ifr_addr)->sin_addr;

    /*get broadcast address*/
    struct ifreq ifr_brd;
    ifr_brd.ifr_addr.sa_family = AF_INET;
    strncpy(ifr_brd.ifr_name, "wlp3s0", IFNAMSIZ-1);
    if(ioctl(udp_socket, SIOCGIFBRDADDR, &ifr_brd) == -1)
    {
        perror("Getting broadcast address:");
        exit(EXIT_FAILURE);
    }

    struct in_addr brd_addr = ((struct sockaddr_in *)&ifr_brd.ifr_addr)->sin_addr;

    struct sockaddr_in sock_in;
    sock_in.sin_addr.s_addr = ip_addr.s_addr;
    sock_in.sin_port = htons(udp_port);
    sock_in.sin_family = AF_INET;

    /*bind*/
    if(bind(udp_socket, (struct sockaddr*)&sock_in, sizeof(struct sockaddr)) == -1)
    {
        perror("UDP client socket binding");
        exit(EXIT_FAILURE);
    }

    int enable_brd = 1;
    if(setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &enable_brd, sizeof(int)) == -1)
    {
        perror("UDP client socket broadcast enabling");
        exit(EXIT_FAILURE);
    }

    sock_in.sin_addr.s_addr = brd_addr.s_addr;
    sock_in.sin_port = htons(udp_port);
    sock_in.sin_family = AF_INET;

    sendto(udp_socket, &ip_addr, sizeof(struct in_addr), 0,
        (struct sockaddr *)&sock_in, sizeof(struct sockaddr_in));

    close(udp_socket);

    /*tcp connection*/
    int tcp_sock = socket(AF_INET , SOCK_STREAM, 0);
    if(tcp_sock == -1)
    {
        perror("TCP client socket creation");
        exit(EXIT_FAILURE);
    }

    int enable_reuse = 1;
    if(setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &enable_reuse, sizeof(int)) == -1)
    {
        perror("UDP client socket broadcast enabling");
        exit(EXIT_FAILURE);
    }

    sock_in.sin_addr.s_addr = ip_addr.s_addr;
    sock_in.sin_port = htons(tcp_port);
    sock_in.sin_family = AF_INET;
    if(bind(tcp_sock, (struct sockaddr*)&sock_in, sizeof(struct sockaddr)) == -1)
    {
        perror("TCP client socket binding");
        exit(EXIT_FAILURE);
    }

    listen(tcp_sock, servers_qty);

    int epollfd = epoll_create1(0);
    if(epollfd == -1)
    {
        perror("epoll");
        exit(EXIT_FAILURE);
    }
    struct epoll_event event;
    struct epoll_event events[64];

    server_struc_t server[servers_qty];
    int max_fd = 0;
    int threads_total = 0;
    struct sockaddr_in server_sock;
    unsigned int sockaddr_len = sizeof(struct sockaddr);
    for (int i = 0; i < servers_qty; ++i)
    {
        server[i].res = 0;
        server[i].fd = accept(tcp_sock, (struct sockaddr *)&server_sock, (socklen_t *)&sockaddr_len);
        if(server[i].fd < 0)
        {
            printf("TCP client accept: Connection failed\n");
            exit(EXIT_FAILURE);
        }
        if(server[i].fd > max_fd)
            max_fd = server[i].fd;

        if(recv(server[i].fd, &(server[i].threads_avail), sizeof(int), 0) <= 0)
        {
            printf("TCP client recv: Recieve failed\n");
            exit(EXIT_FAILURE);            
        }
        threads_total += server[i].threads_avail;

        //make socket nonblock
        /*int flags = fcntl(server[i].fd, F_GETFL);
        flags |= O_NONBLOCK;
        fcntl(server[i].fd, F_SETFL, flags);*/
        /*epoll structure add*/
    }

    //distribution
    double seg_size = (right - left) / threads_total;
    double current_left = left;
    for (int i = 0; i < servers_qty; ++i)
    {
        server[i].left = current_left;
        server[i].right = current_left + server[i].threads_avail * seg_size;
        current_left = server[i].right;
    }

    #ifdef NET_DEBUG
    printf("server struct info:\n");
    for (int i = 0; i < servers_qty; ++i)
    {
        printf("server[%d].fd = %d\n", i, server[i].fd);
        printf("server[%d].threads_avail = %d\n", i, server[i].threads_avail);
        printf("server[%d].left = %g\n", i, server[i].left);
        printf("server[%d].right = %g\n", i, server[i].right);
    }
    #endif

    //info sending
    for (int i = 0; i < servers_qty; ++i)
    {
        if(send(server[i].fd, &(server[i].left), sizeof(double), 0) < 0)
        {
            printf("TCP client send: Unsuccessful\n");
            exit(EXIT_FAILURE);
        }

        if(send(server[i].fd, &(server[i].right), sizeof(double), 0) < 0)
        {
            printf("TCP client send: Unsuccessful\n");
            exit(EXIT_FAILURE);
        }
    }
    //sleep(20);
    //results mining
    for (int i = 0; i < servers_qty; ++i)
    {
        if(recv(server[i].fd, &(server[i].res), sizeof(double), 0) <= 0)
        {
            printf("TCP client recv: Recieve failed\n");
            exit(EXIT_FAILURE);            
        }
    }

    double res = 0;
    for (int i = 0; i < servers_qty; ++i)
    {
        res += server[i].res;
        close(server[i].fd);
    }
    printf("RESULT:: %g\n", res);
    close(tcp_sock);

    return 0;

}