#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <liburing.h>
#include <netinet/in.h>
#include <string.h>

#define ENTRY_LENGTH    512
#define BUFFER_LENGTH   512

#define EVENT_ACCEPT    0
#define EVENT_READ      1
#define EVENT_WRITE     2

struct conn_info
{
    int fd;
    int event;
};

int init_server(unsigned short port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(struct sockaddr_in));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    //printf("before bind\r\n");
    if(-1 == bind(sockfd, (struct  sockaddr*)&servaddr, sizeof(struct sockaddr)))
    {
        printf("bind error\r\n");
        return -1;
    }

    listen(sockfd, 10);
    //printf("after listen\r\n");
    return sockfd;
}

void set_event_accept(struct io_uring* ring, int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flag)
{
    struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
    struct conn_info accept_info = 
    {
        .fd = sockfd,
        .event = EVENT_ACCEPT,
    };
    //printf("befor prep accept\r\n");
    io_uring_prep_accept(sqe, sockfd, (struct sockaddr*)addr, addrlen, flag);
    //printf("after prep accept\r\n");
    memcpy(&sqe->user_data, &accept_info, sizeof(struct conn_info));
}

void set_event_recv(struct io_uring* ring, int sockfd, void* buffer, size_t len, int flag)
{
    struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
    struct conn_info item_info = 
    {
        .fd = sockfd,
        .event = EVENT_READ,
    };

    io_uring_prep_recv(sqe, sockfd, buffer, len, flag);
    memcpy(&sqe->user_data, &item_info, sizeof(item_info));
}

void set_event_send(struct io_uring* ring, int sockfd, void* buffer, size_t len, int flag)
{
    struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
    struct conn_info item_info = 
    {
        .fd = sockfd,
        .event = EVENT_WRITE,
    };

    io_uring_prep_send(sqe, sockfd, buffer, len, flag);
    memcpy(&sqe->user_data, &item_info, sizeof(item_info));
}

int main(int argc, char* argv[])
{
    unsigned short port = 9999;
    int sockfd = init_server(port);
    //printf("after init server\r\n");
    struct io_uring_params params;
    struct io_uring ring;
    memset(&params, 0, sizeof(params));
    io_uring_queue_init_params(ENTRY_LENGTH, &ring, &params);
    //printf("after init queue\r\n");
    struct sockaddr_in clientaddr;
    socklen_t len =  sizeof(clientaddr);
    //printf("before set accept\r\n");
    set_event_accept(&ring, sockfd, (struct sockaddr*)&clientaddr, &len, 0);
    //printf("after set accept\r\n");
    char buf[BUFFER_LENGTH] = {0};

    //printf("begin while\r\n");
    while(1)
    {
        io_uring_submit(&ring);

        struct io_uring_cqe* cqe;
        io_uring_wait_cqe(&ring, &cqe);

        struct io_uring_cqe* cqes[128];
        int nready = io_uring_peek_batch_cqe(&ring, cqes, 128);

        int i = 0;
        for(i = 0; i < nready; i++)
        {
            struct io_uring_cqe* entries = cqes[i];
            struct conn_info item_info;
            memcpy(&item_info, &entries->user_data, sizeof(struct conn_info));

            if(item_info.event == EVENT_ACCEPT)
            {
                set_event_accept(&ring, sockfd, (struct sockaddr*)&clientaddr, &len, 0);

                int connfd = entries->res;

                set_event_recv(&ring, connfd, buf, BUFFER_LENGTH, 0);
            }
            else if(item_info.event == EVENT_READ)
            {
                int ret = entries->res;

                if(ret == 0)
                {
                    close(item_info.fd);
                }
                else if(ret > 0)
                {
                    printf("clinet: %d, buffer: %s\r\n", item_info.fd, buf);
                    set_event_send(&ring, item_info.fd, buf, ret, 0);
                }
            }
            else if(item_info.event == EVENT_WRITE)
            {
                int ret = entries->res;

                set_event_recv(&ring, item_info.fd, buf, BUFFER_LENGTH, 0);
            }
        }


        io_uring_cq_advance(&ring, nready);
    }
}