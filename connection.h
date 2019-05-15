#ifndef CONNECTION_H
#define CONNECTION_H

#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>

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

#include "message.h"

class Connection {
 public:
  int connection_number;
  int socket_fd;
  bool is_duplicate;

  std::shared_ptr<std::thread> read_thread;
  std::shared_ptr<std::thread> write_thread;

  std::mutex m;
  std::condition_variable cv;

  std::queue<std::shared_ptr<Message>> q;

  int state;
  std::string neighbor_node_id;

  void AddWork(std::shared_ptr<Message> msg);
  std::shared_ptr<Message> WaitForWork();
  void ShutDown();

  Connection(int newsockfd);
};

#endif
