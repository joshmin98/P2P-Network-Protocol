#include "connection.h"
#include <iostream>

Connection::Connection(int newsockfd) {
  socket_fd = newsockfd;
  neighbor_node_id = "unknown";
  is_duplicate = false;
}

void Connection::AddWork(std::shared_ptr<Message> msg) {
  m.lock();
  q.push(msg);
  cv.notify_all();
  m.unlock();
}

std::shared_ptr<Message> Connection::WaitForWork() {
  std::unique_lock<std::mutex> l(m);

  while(q.empty()) {
    cv.wait(l);
  }

  std::shared_ptr<Message> msg = q.front();
  q.pop();
  return msg;
}

void Connection::ShutDown() {
  shutdown(socket_fd, SHUT_RDWR);
  close(socket_fd);
  socket_fd = -1;
}
