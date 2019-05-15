#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <openssl/md5.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>

void timestamp_diff(struct timeval *t1, struct timeval *t2, struct timeval *t2_minus_t1) {
  if (t2->tv_usec >= t1->tv_usec) {
    t2_minus_t1->tv_usec = t2->tv_usec - t1->tv_usec;
    t2_minus_t1->tv_sec = t2->tv_sec - t1->tv_sec;
  } else {
    t2_minus_t1->tv_usec = 1000000 + t2->tv_usec - t1->tv_usec;
    t2_minus_t1->tv_sec = t2->tv_sec - 1 - t1->tv_sec;
  }
}

std::string get_timestamp(struct timeval start_time) {
  char event_time_buf[16];
  struct timeval now;
  gettimeofday(&now, NULL);

  struct timeval elapsed_time;

  timestamp_diff(&start_time, &now, &elapsed_time);
  snprintf(event_time_buf,
           sizeof(event_time_buf),
           "%06d.%06d",
           (int)(elapsed_time.tv_sec),
           (int)(elapsed_time.tv_usec));
  return event_time_buf;
}

static int create_master_socket(const char *port_number_str, int debug) {
  int socket_fd = (-1);
  int reuse_addr = 1;
  struct addrinfo hints;
  struct addrinfo* res = NULL;

  signal(SIGPIPE, SIG_IGN);

  memset(&hints,0,sizeof(hints));
  hints.ai_family = AF_UNSPEC; //AF_UNSPEC
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = AI_NUMERICSERV|AI_ADDRCONFIG;

  // change me back to localhost
  getaddrinfo("localhost", port_number_str, &hints, &res);
  socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (socket_fd == (-1)) {
    perror("socket() system call");
    exit(-1);
  }
  setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (void*)(&reuse_addr), sizeof(int));
  if (bind(socket_fd, res->ai_addr, res->ai_addrlen) == (-1)) {
    perror("bind() system call");
    exit(-1);
  }
  listen(socket_fd, 2);

  if (debug) {
    struct sockaddr_in server_addr;
    socklen_t server_len = (socklen_t)sizeof(server_addr);

    getsockname(socket_fd, (struct sockaddr *)(&server_addr), &server_len);
    fprintf(stderr,
            "[SERVER] Server listening at %s:%1d\n",
            inet_ntoa(server_addr.sin_addr),
            (int)htons((uint16_t)(server_addr.sin_port & 0x0ffff)));
  }
  return socket_fd;
}

static int my_accept(const int master_socket_fd, int debug) {
  int newsockfd = (-1);
  struct sockaddr_in cli_addr;
  unsigned int clilen = sizeof(cli_addr);

  newsockfd = accept(master_socket_fd, (struct sockaddr *)(&cli_addr), &clilen);
  if (debug && newsockfd != (-1)) {
    struct sockaddr_in peer;
    socklen_t peer_addr_len = (socklen_t)sizeof(peer);

    getpeername(newsockfd, (struct sockaddr *)(&peer), &peer_addr_len);
    fprintf(stderr,
            "[SERVER] connected to client from %s:%1d\n",
            inet_ntoa(peer.sin_addr),
            (int)htons((uint16_t)(peer.sin_port & 0x0ffff)));
  }
  return newsockfd;
}

static int create_client_socket(const char *host_name,
                                const char *port_number_str,
                                const int debug) {
  int socket_fd = (-1);
  struct addrinfo hints;
  struct addrinfo* res = NULL;

  signal(SIGPIPE, SIG_IGN);

  memset(&hints,0,sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = AI_NUMERICSERV|AI_ADDRCONFIG;

  getaddrinfo(host_name, port_number_str, &hints, &res);
  socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (socket_fd == (-1)) {
    perror("socket() system call");
    exit(-1);
  }
  if (debug) {
    struct sockaddr_in cs_addr;
    socklen_t cs_len = (socklen_t)sizeof(cs_addr);

    getsockname(socket_fd, (struct sockaddr *)(&cs_addr), &cs_len);
    fprintf(stderr,
            "socket at %s:%1d ",
            inet_ntoa(cs_addr.sin_addr),
            (int)htons((uint16_t)(cs_addr.sin_port & 0x0ffff)));
  }
  return socket_fd;
}

static int my_connect(const int client_socket_fd,
                      const char *host_name,
                      const char *port_number_str,
                      const int debug) {
  struct sockaddr_in soc_address;

  memset(&soc_address, 0, sizeof(soc_address));
  if (*host_name >= '0' && *host_name <= '9') {
    soc_address.sin_addr.s_addr = inet_addr(host_name);
  } else {
    struct hostent *p_hostent;

    p_hostent = gethostbyname(host_name);
    memcpy(&soc_address.sin_addr, p_hostent->h_addr, p_hostent->h_length);
  }
  soc_address.sin_family = AF_INET;
  soc_address.sin_port = htons((unsigned short)atoi(port_number_str));

  if (connect(client_socket_fd, (struct sockaddr*)&soc_address, sizeof(soc_address)) == (-1)) {
    return (-1);
  }
  if (debug) {
    struct sockaddr_in cs_addr;
    socklen_t cs_len = (socklen_t)sizeof(cs_addr);

    getpeername(client_socket_fd, (struct sockaddr *)(&cs_addr), &cs_len);
    fprintf(stderr,
            "is contacting server at %s:%1d ...\n",
            inet_ntoa(cs_addr.sin_addr),
            (int)htons((uint16_t)(cs_addr.sin_port & 0x0ffff)));
  }
  return 0;
}
