#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE 100

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";


struct RWLOCK_T{
    sem_t lock;
    sem_t writeLock;
    int readcnt;
};
struct CACHE{
    int lruNumber;
    char url[MAXLINE];
    char content[MAX_OBJECT_SIZE];
};

struct CACHE cache[MAX_CACHE];
struct RWLOCK_T* rw;


void solve(int client_fd);
int analyze_uri(char *uri, char *host, char *path, char *port, char *request_head);
void read_requesthdrs(rio_t *rp, int fd);
void return_content(int server_fd, int client_fd, char *uri);
void *thread(void *vargp);
int maxlrucache();
void rwlock_init();
char *readcache(char *url);
void writecache(char *buf, char *url);



int main(int argc, char **argv)
{   
    char host[MAXLINE], port[MAXLINE];
    socklen_t client_len;
    struct sockaddr_storage client_addr;
    
    if(argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int listenfd = open_listenfd(argv[1]);
    int *connect;
    pthread_t tid;
    rw = Malloc(sizeof(struct RWLOCK_T));
    rwlock_init();

    while(1)
    {
        client_len = sizeof(client_addr);
        connect = Malloc(sizeof(int));

        *connect = Accept(listenfd, (SA *) &client_addr, &client_len);
        Getnameinfo((SA *) &client_addr, client_len, host, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", host, port);
        Pthread_create(&tid, NULL, thread, connect);
    }

    return 0;
}


void solve(int client_fd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], path[MAXLINE], port[MAXLINE], request_head[MAXLINE];
    int server_fd;
    rio_t rio;

    Rio_readinitb(&rio, client_fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s%s%s", method, uri, version);

    if(strcasecmp(method, "GET"))
    {
        printf("Not implemented");
        return ;
    }

    char *content = readcache(uri);
    if(content != NULL)
    {
        Rio_writen(client_fd, content, strlen(content));
        free(content);
    }
    else
    {
        analyze_uri(uri, host, path, port, request_head);
        server_fd = Open_clientfd(host, port);
        Rio_writen(server_fd, request_head, strlen(request_head));
        read_requesthdrs(&rio, server_fd);
        return_content(server_fd, client_fd, uri);
    }
}


int analyze_uri(char *uri, char *host, char *path, char *port, char *request_head)
{
    sprintf(port, "80");
    char *end, *bp;
    char *tail = uri + strlen(uri);
    char *bg = strstr(uri, "//");
    bg = (bg != NULL ? bg + 2 : uri);
    end = bg;
    while(*end != '/' && *end != ':') ++end;
    strncpy(host, bg, end - bg);
    bp = end + 1;
    if(*end == ':')
    {
        ++end;
        bp = strstr(bg, "/");
        strncpy(port, end, bp - end);
        end = bp;
    }
    strncpy(path, end, (int) (tail - end)  + 1);
    sprintf(request_head, "GET %s HTTP/1.0\r\nHost: %s\r\n", path, host);
    return 1;
}

void read_requesthdrs(rio_t *rp, int fd)
{
    char buf[MAXLINE];
 
    sprintf(buf, "%s", user_agent_hdr);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Connection: close\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Proxy-Connection: close\r\n");
    Rio_writen(fd, buf, strlen(buf));
 

    for(Rio_readlineb(rp, buf, MAXLINE); strcmp(buf, "\r\n"); Rio_readlineb(rp, buf, MAXLINE))
    {
        if(strncmp("Host", buf, 4) == 0 || strncmp("User-Agent", buf, 10) == 0 || strncmp("Connection", buf, 10) == 0 || strncmp("Proxy-Connection", buf, 16) == 0)
            continue;
        printf("%s", buf);
        Rio_writen(fd, buf, strlen(buf));
    }
    Rio_writen(fd, buf, strlen(buf));
    return;
}

void return_content(int server_fd, int client_fd, char *uri)
{
    size_t n, size = 0;
    char buf[MAXLINE], content[MAX_OBJECT_SIZE];
    rio_t srio;
 
    Rio_readinitb(&srio, server_fd);
    while((n = Rio_readlineb(&srio, buf, MAXLINE)) != 0)
    {
        Rio_writen(client_fd, buf, n);
        if(n + size <= MAX_OBJECT_SIZE)
        {
            sprintf(content + size, "%s", buf);
            size += n;
        }
        else
        {
            size = MAX_OBJECT_SIZE + 1;
        }
    }
    writecache(content, uri);
    return ;
}

void *thread(void *vargp)
{
    int connect = *((int *) vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    solve(connect);
    Close(connect);
    return NULL;
}


void rwlock_init()
{
    rw->readcnt = 0;
    sem_init(&rw->lock, 0, 1);
    sem_init(&rw->writeLock, 0, 1);
    return;
}

void writecache(char *buf, char *url)
{
    sem_wait(&rw->writeLock);
    int index;

    for(index = 0; index < MAX_CACHE; ++index)
    {
        if(cache[index].lruNumber == 0)
        {
            break;
        }
    }
    if(index == MAX_CACHE)
    {
        int minlru = cache[0].lruNumber;
        for(int i = 1; i < MAX_CACHE; ++i)
        {
            if(cache[i].lruNumber < minlru)
            {
                minlru = cache[i].lruNumber;
                index = i;
            }
        }
    }
    cache[index].lruNumber = maxlrucache() + 1;
    strcpy(cache[index].url, url);
    strcpy(cache[index].content, buf);
    sem_post(&rw->writeLock);

    return;

}

char *readcache(char * url)
{
    sem_wait(&rw->lock);
    if(rw->readcnt == 1)
    {
        sem_wait(&rw->writeLock);
    }
    rw->readcnt++;
    sem_post(&rw->lock);

    char *content = NULL;
    for(int i = 0; i < MAX_CACHE; ++i)
    {
        if(strcmp(url, cache[i].url) == 0)
        {
            content = (char *) Malloc(strlen(cache[i].content));
            strcpy(content, cache[i].content);
            int maxlru = maxlrucache();
            cache[i].lruNumber = maxlru + 1;
            break;
        }
    }
    sem_wait(&rw->lock);
    rw->readcnt--;
    if(rw->readcnt == 0)
    {
        sem_post(&rw->writeLock);
    }
    sem_post(&rw->lock);
    return content;
}

int maxlrucache()
{
    int max_lru = 0;
    for(int i = 0; i < MAX_CACHE; ++i)
    {
        if(cache[i].lruNumber > max_lru)
        {
            max_lru = cache[i].lruNumber;       
        }
    }
    return max_lru;
}


