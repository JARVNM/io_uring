
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define TIME_SUB_MS(tv1, tv2) ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

#define TEST_MESSAGE "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890abcdefghijklmnopqrstuvwxyz\r\n"

#define WBUFFER_LENGTH  2048
#define RBUFFER_LENGTH  2048
struct test_ctx_s
{
    char ip[16];
    int port;
    int threadnum;
    int connection;
    int requestion;
    int failed;
};

typedef struct test_ctx_s test_ctx_t;

int connect_tcpserver(char* ip, int port)
{
    int connfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in clientaddr;
    memset(&clientaddr, 0, sizeof(clientaddr));
    clientaddr.sin_family = AF_INET;
    clientaddr.sin_port = htons(port);
    clientaddr.sin_addr.s_addr = inet_addr(ip);

    if(connect(connfd, (struct sockaddr*)&clientaddr, sizeof(clientaddr)) < 0)
    {
        perror("connect");
        return -1;
    }

    return connfd;
}

int send_recv_pkt(int sockfd)
{
    char wbuffer[WBUFFER_LENGTH] = {0};
    int i = 0;
    for(i = 0; i < 8; i++)
    {
        strcpy(wbuffer + i * strlen(TEST_MESSAGE), TEST_MESSAGE);
    }

    int res = send(sockfd, wbuffer, strlen(wbuffer), 0);
    if(res < 0)
        exit(1);

    char rbuffer[RBUFFER_LENGTH] = {0};
    int ret = recv(sockfd, rbuffer, RBUFFER_LENGTH, 0);
    if(ret <= 0)
        exit(1);

    if(strcmp(rbuffer, wbuffer) != 0)
    {
        printf("fail: '%s' != '%s'\r\n", rbuffer, wbuffer);
        return -1;
    }

    return 0;
}

void* test_qps_entry(void* arg)
{
    test_ctx_t* ctx = (test_ctx_t*)arg;

    int connfd = connect_tcpserver(ctx->ip, ctx->port);
    if(connfd < 0)
    {
        printf("connect to tcp server failed\r\n");
        return NULL;
    }

    int count = ctx->requestion / ctx->threadnum;
    int i = 0;
    for(i = 0; i < count; i++)
    {
        int res = send_recv_pkt(connfd);
        if(res != 0)
        {
            printf("send_recv_tcppkt failed\n");
            ctx->failed++;
            continue;
        }
    }

    return NULL;
}


int main(int argc, char* argv[])
{
    int ret = 0;
    test_ctx_t ctx = {0};

    //-s 127.0.0.1 -p 2048 -t 50 -c 100 -n 10000
    int opt;
    while((opt = getopt(argc, argv, "s:p:t:c:n:?")) != -1)
    {
        switch(opt)
        {
            case 's':
            {
                printf("-s:%s\r\n", optarg);
                strcpy(ctx.ip, optarg);
                break;
            }
            case 'p':
            {
                printf("-p:%s\r\n", optarg);
                ctx.port = atoi(optarg);
                break;
            }
            case 't':
            {
                printf("-t:%s\r\n", optarg);
                ctx.threadnum = atoi(optarg);
                break;
            }
            case 'c':
            {
                printf("-c:%s\r\n", optarg);
                ctx.connection = atoi(optarg);
                break;
            }
            case 'n':
            {
                printf("-n:%s\r\n", optarg);
                ctx.requestion = atoi(optarg);
                break;
            }
        }


    }

    pthread_t* pths = malloc(ctx.threadnum * sizeof(pthread_t));

    int i = 0;
    struct timeval tv_begin;
    gettimeofday(&tv_begin, NULL);
    for(i = 0; i < ctx.threadnum; i++)
    {
        pthread_create(&pths[i], NULL, test_qps_entry, &ctx);
    }

    for(i = 0; i < ctx.threadnum; i++)
    {
        pthread_join(pths[i], NULL);
    }

    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);
    int time_used = TIME_SUB_MS(tv_end, tv_begin);

    printf("success: %d, failed: %d, time_used: %d, qps: %d\r\n", ctx.requestion - ctx.failed, 
        ctx.failed, time_used, ctx.requestion * 1000 / time_used);


    free(pths);

    return ret;
}