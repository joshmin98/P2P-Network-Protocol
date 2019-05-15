#include <iostream>
#include <thread>
#include <sstream>
#include <fstream>
#include <mutex>
#include <vector>
#include <unistd.h>
#include <algorithm>
#include <istream>
#include <set>
#include <chrono>

#include <stdio.h>

#include "ini_parser.h"
#include "message.h"
#include "connection.h"
#include "c_networking_functions.cpp"

int master_sock_fd = -1;

std::map<std::string, std::vector<std::string>> link_state;
// std::set<std::string> message_cache;
std::map<std::string, Message> message_cache;

bool is_running = true;
std::vector<Connection*> connections;
std::string port, hostname, this_node, pidfile, logfile;
bool do_log_sayhello, do_log_lsupdate, do_log_ucastapp;
int max_ttl;
int curr_connection_number = 1;

std::mutex main_mtx;
std::condition_variable main_cv;
std::queue<Connection*> reaper_queue;

int msg_lifetime;
std::map<std::string, std::string> forwarding_table;
std::queue<std::shared_ptr<Message>> trc_queue;
std::mutex trc;
std::condition_variable trc_cv;
int seq_no = 1;

struct timeval begin_time;
std::ofstream logs;

std::mutex log_mtx;

std::string get_timestamp() {
  char timestamp_buf[32];
  struct timeval now;
  char time_buf[26];
  int i;

  gettimeofday(&now, NULL);
  strcpy(time_buf, ctime(&now.tv_sec));
  for (i=0; i < 11; i++) {
    timestamp_buf[i] = time_buf[i];
  }
  timestamp_buf[11] = time_buf[20];
  timestamp_buf[12] = time_buf[21];
  timestamp_buf[13] = time_buf[22];
  timestamp_buf[14] = time_buf[23];
  for (i=15; i < 24; i++) {
    timestamp_buf[i] = time_buf[i-5];
  }
  sprintf(&timestamp_buf[24], ".%06d", (int)now.tv_usec);
  return timestamp_buf;
}

void log_message(std::string state, Message m, Connection* con) {
  std::string log_message = "[" + get_timestamp() + "] ";
  log_message += "{" + state + "} ";
  log_message += m.message_type + " ";
  log_message += con->neighbor_node_id + " ";
  log_message += std::to_string(m.ttl) + " ";
  if (m.flood == 0) {
    log_message += "- ";
  } else {
    log_message += "F ";
  }
  log_message += std::to_string(m.content_length);
  if (m.message_type == "SAYHELLO") {
    log_mtx.lock();
    if(do_log_sayhello) {
      logs << log_message << std::endl;
    } else {
      std::cout << log_message << std::endl;
    }
    log_mtx.unlock();
  } else if (m.message_type == "LSUPDATE") {
    log_message += " " + m.sender_id + "_" + m.origin_start_time + " ";
    log_message += m.origin_start_time + " ";
    log_message += m.sender_id + " ";
    log_message += "(";
    for(unsigned i = 0; i < m.link_state.size(); ++i) {
      if (i == m.link_state.size() - 1) {
        log_message += m.link_state[i];
        break;
      }
      log_message += m.link_state[i] + ",";
    }
    log_message += ")";
    log_mtx.lock();
    if (do_log_lsupdate) {
      logs << log_message << std::endl;
    } else {
      std::cout << log_message << std::endl;
    }
    log_mtx.unlock();
  } else if (m.message_type == "UCASTAPP") {
    log_message += " " + m.sender_id + "_" + m.origin_start_time + " ";
    log_message += m.sender_id + " ";
    log_message += m.to + " ";
    log_message += m.trc_body;
    log_mtx.lock();
    if(do_log_ucastapp) {
      logs << log_message << std::endl;
    } else {
      std::cout << log_message << std::endl;
    }
    log_mtx.unlock();
  }
};

void fix_link_state() {
  for(auto& con : connections) {
    if(con->socket_fd > 0) {
      if (std::find(link_state[this_node].begin(),
                    link_state[this_node].end(),
                    con->neighbor_node_id) == link_state[this_node].end() && 
          con->neighbor_node_id != "unknown") {
        link_state[this_node].push_back(con->neighbor_node_id);
      }
    }
  }
  for(auto& item : link_state) {
    auto find = std::find(item.second.begin(), item.second.end(), "DOWN");
    if (item.second.size() > 1 && find != item.second.end()) {
      item.second.erase(find);
    }
  }
  std::set<std::string> neighbors;
  for(auto& con : connections) {
    if(con->socket_fd > 0)
      neighbors.insert(con->neighbor_node_id);
  }
  for(std::string neighbor : link_state[this_node]) {
    if(neighbors.find(neighbor) == neighbors.end()) {
      link_state[this_node].erase(std::find(link_state[this_node].begin(),
                                            link_state[this_node].end(),
                                            neighbor));
    }
  }
  std::set<std::string> s(link_state[this_node].begin(), link_state[this_node].end());
  link_state[this_node].assign(s.begin(), s.end());
}

void calc_forwarding_table() {
  std::map<std::string, std::string> clear;
  forwarding_table = clear;
  std::set<std::string> visited;
  std::queue<std::pair<std::string, std::string>> q;

  visited.insert(this_node);
  for(std::string neighbor : link_state[this_node]) {
    forwarding_table[neighbor] = neighbor;
    visited.insert(neighbor);
    for(std::string child : link_state[neighbor]) {
      q.push(std::make_pair(child, neighbor));
    }
  }

  while (!q.empty()) {
    auto node = q.front();
    if (visited.find(node.first) != visited.end()) {
      q.pop();
      continue;
    }
    forwarding_table[node.first] = node.second;
    visited.insert(node.first);
    for (std::string child : link_state[node.first]) {
      q.push(std::make_pair(child, node.second));
    }
  }
}

void remove_dead() {
  std::set<std::string> neighbors;
  for(auto& item : link_state[this_node]) {
    neighbors.insert(item);
  }
  neighbors.insert(this_node);
  for(auto& item : link_state) {
    if(neighbors.find(item.first) != neighbors.end())
      continue;
    else {
      bool valid = false;
      for(std::string item2 : item.second) {
        if(neighbors.find(item2) != neighbors.end()) {
          valid = true;
          break;
        }
      }
      if(!valid) {
        std::vector<std::string> nullvec;
        nullvec.push_back("DOWN");
        link_state[item.first] = nullvec;
      }
    }
  }
}

void timeout(double lifetime, bool* timed_out, bool* found) {
  auto start = std::chrono::steady_clock::now();
  double elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - start).count();
  while(elapsed < lifetime && *found == false) {
    elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count();
    usleep(100000);
  }
  if (!found)
    *timed_out = true;
  return;
}

Message trc_wait() {
  bool timed_out = false;
  bool found = false;
  std::unique_lock<std::mutex> l(trc);

  std::thread t(timeout, msg_lifetime, &timed_out, &found);

  while(trc_queue.empty() && !timed_out) {
    trc_cv.wait(l);
    found = true;
  }

  t.join();

  if (found) {
    Message resp = *trc_queue.front();
    trc_queue.pop();
    return resp;
  }
  return Message();
}

void console() {
  while (true && is_running) {
    std::cout << hostname << ":" << port << "> ";
    char input[100];
    std::cin.getline(input, sizeof(input));

    std::stringstream ss(input);

    std::string command = "";
    std::string arg1 = "";
    std::string arg2 = "";

    ss >> command >> arg1 >> arg2;

    //////////////////
    // quit command //
    //////////////////
    if (command == "shutdown" || command == "quit") {
      is_running = false;
      for (auto& con : connections) {
        if (con->socket_fd == -1 || con->socket_fd == -2) {
          con->ShutDown();
          //TODO: Shutdown message for connection here
        }
      }
      shutdown(master_sock_fd, SHUT_RDWR);
      close(master_sock_fd);
      master_sock_fd = -1;
      std::cout << "Server shutdown" << std::endl;
      return;
    }

    ///////////////////////
    // neighbors command //
    ///////////////////////
    else if (command == "neighbors" && arg1 == "") {

      // Check for active connections
      unsigned count = 0;
      for (auto& con : connections) {
        if (con->socket_fd == -1 || con->socket_fd == -2) {
          ++count;
        }
      }
      if (count == connections.size()) {
        std::cout << this_node << " has no neighbors" << std::endl;
        continue;
      }

      // Display info for active connections
      std::cout << "node " << this_node << " has ";
      std::vector<std::string> active_cons;
      for (auto& con : connections) {
        if (con->socket_fd == -1 || con->socket_fd == -2) {
          continue;
        }
        active_cons.push_back(con->neighbor_node_id);
      }
      if (active_cons.size() == 1) {
        std::cout << "node " << active_cons[0] << " as its neighbor." << std::endl;
        continue;
      } else if(active_cons.size() == 2) {
        std::cout << "nodes " << active_cons[0] << " and " <<
            active_cons[1] << " as its neighbors." << std::endl;
        continue;
      }else {
        std::cout << "nodes ";
      }
      for(unsigned i = 0; i < active_cons.size(); ++i) {
        if(i == active_cons.size() - 1) {
          std::cout << "and " << active_cons[i] << " ";
          break;
        }
        std::cout << active_cons[i] << ", ";
      }
      std::cout << "as its neighbors." << std::endl;
    }

    //////////////////////
    // netgraph command //
    //////////////////////
    else if (command == "netgraph") {
      for(auto& item : link_state) {
        std::set<std::string> s(link_state[item.first].begin(), link_state[item.first].end());
        link_state[item.first].assign(s.begin(), s.end());
      }

      for (auto& item : link_state) {
        if((item.second.size() == 1 && item.second[0] == "DOWN") || item.second.size() == 0)
          continue;
        std::cout << item.first << ": ";
        for(unsigned i = 0; i < item.second.size(); ++i) {
          std::string neighbor = item.second[i];
          if (i == item.second.size() - 1) {
            std::cout << neighbor;
            break;
          }
          std::cout << neighbor << ",";
        }
        std::cout << std::endl;
      }
    }

    ////////////////////////
    // forwarding command //
    ////////////////////////
    else if (command == "forwarding") {
      calc_forwarding_table();
      for(auto& item : forwarding_table) {
        std::cout << item.first << ": " << item.second << std::endl;
      }
    }

    //////////////////////////
    // traceroute X command //
    //////////////////////////
    else if (command == "traceroute" && arg1 != "") {
      // Update forwarding table
      calc_forwarding_table();

      for(int i = 1; i <= max_ttl; ++i) {
        auto start = std::chrono::steady_clock::now();
        // Send message to appropriate link
        bool found = false;
        for(auto& link : connections) {
          if(link->neighbor_node_id == forwarding_table[arg1] && link->socket_fd > 0) {
            Message trc_msg = Message("UCASTAPP",
                                      i,
                                      this_node,
                                      arg1,
                                      seq_no,
                                      "REQUEST");
            link->AddWork(std::make_shared<Message>(trc_msg));
            log_message("i", trc_msg, link);
            found = true;
            seq_no++;
            break;
          }
        }

        if(!found) {
          std::cout << arg1 << " is not in the forwarding table." << std::endl;
          break;
        }

        Message resp = trc_wait();
        double rtt = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count() / 1000.0;

        if (resp.trc_body == "NONE") {
          std::cout << "Not found" << std::endl;
          break;
        }

        if (resp.trc_body.find(arg1) != std::string::npos) {
          std::cout << i << " - " << resp.sender_id << ", " << rtt << "ms" << std::endl;
          break;
        } else if (resp.trc_body.find("Zero") != std::string::npos) {
          std::cout << i << " - " << resp.sender_id << ", " << rtt << "ms" << std::endl;
        }
      }
      std::queue<std::shared_ptr<Message>> q;
      trc_queue = q;
    }

    //////////////////////////////////
    // clear command (for me debug) //
    //////////////////////////////////
    else if (command == "clear") {
      system("clear");
    }

    else {
      std::cout << "Command not recognized. Valid commands are:" << std::endl;
      std::cout << "\t" << "forwarding" << std::endl;
      std::cout << "\t" << "neighbors" << std::endl;
      std::cout << "\t" << "netgraph" << std::endl;
      std::cout << "\t" << "traceroute target" << std::endl;
      std::cout << "\t" << "shutdown" << std::endl;
    }
  }
}

void reaper() {
  while (true) {
    main_mtx.lock();
    if (connections.size() >= 100000) {
      is_running = false;
      std::cout << "100,000 connections served. Proceed with auto-shutdown..." << std::endl;
      main_mtx.lock();
      break;
    }
    main_mtx.unlock();

    usleep(250000);
    std::vector<Connection*> look;

    if (is_running) {
      main_mtx.lock();
      for(auto& con : connections) {
        if (con->socket_fd == -2) {
          look.push_back(con);
        }
      }
      main_mtx.unlock();
    } else {
      break;
    }

    main_mtx.lock();
    for (auto& con : look) {
      con->ShutDown();
    }
    main_mtx.unlock();

    if (!is_running) {
      main_mtx.lock();
      for (auto& con : connections) {
        if (con->socket_fd >= 0) {
          con->ShutDown();
        }
      }
      main_mtx.unlock();
      for (auto& con : connections) {
        con->read_thread->join();
        con->write_thread->join();
      }
    }
  }
}

void debug_connection_send(Message m, Connection* con) {
  std::cout << "[" << con->connection_number << "]\tSending LSUPDATE to " <<
      con->neighbor_node_id << " TTL=" << m.ttl << ", From=" << m.sender_id <<
      ", MessageID=" << m.message_id << ", LinkState= ";
  for (std::vector<std::string>::iterator it = m.link_state.begin();
       it != m.link_state.end();
       ++it) {
    auto end_check = it;
    end_check++;
    if (end_check == m.link_state.end()) {
      std::cout << *it << std::endl;
      break;
    }
    std::cout << *it << ", ";
  }
}

void debug_connection_receive(Message m, Connection* con) {
  std::cout << "[" << con->connection_number << "]\tReceived LSUPDATE from " <<
      con->neighbor_node_id << " TTL=" << m.ttl << ", From=" << m.sender_id <<
      ", MessageID=" << m.sender_id + "_" + m.origin_start_time << ", LinkState= ";
  for (std::vector<std::string>::iterator it = m.link_state.begin();
       it != m.link_state.end();
       ++it) {
    auto end_check = it;
    end_check++;
    if (end_check == m.link_state.end()) {
      std::cout << *it << std::endl;
      break;
    }
    std::cout << *it << ", ";
  }
}

void debug_print_link_state(std::string node, std::string state) {
  std::cout << "------" << state << " LINK STATE INFO------" << std::endl;
  if(link_state.find(node) == link_state.end()) {
    std::cout << "No link state info for " << node << std::endl;
    std::cout << "------END LINK STATE INFO------" << std::endl;
    return;
  }
  std::cout << node << std::endl;
  for(auto& item : link_state[node]) {
    std::cout << "\t" << item << std::endl;
  }
  std::cout << "------END LINK STATE INFO------" << std::endl;
  return;
}

void connection_read(int connection_number) {
  main_mtx.lock();
  Connection* con = connections[connection_number - 1];
  main_mtx.unlock();

  char buf[2048];
  read(con->socket_fd, buf, sizeof(buf));
  std::string message_string = buf;
  Message m = Message(message_string);
  
  if (con->state == 0) {
    main_mtx.lock();
    for (auto& connection : connections) {
      if (connection == con) {
        continue;
      } else {
        if (connection->state == 1 && (connection->neighbor_node_id == m.sender_id)) {
          connection->is_duplicate = true;
        }
      }
    }
    main_mtx.unlock();

    ////////////////////////////////////////////////////////////////////////
    // Send SAYHELLO- this code path is executed when we are connected to //
    ////////////////////////////////////////////////////////////////////////
    if (con->is_duplicate == false) {
      main_mtx.lock();
      con->state = 1;
      con->neighbor_node_id = m.sender_id;
      main_mtx.unlock();

      log_message("r", m, con);
      // std::cout << "[" << con->connection_number << "]\t" << m.message_type <<
      //     " message received from " << con->neighbor_node_id << std::endl;

      // Send SAYHELLO back
      log_message("i", m, con);
      // std::cout << "Sending SAYHELLO to " << con->neighbor_node_id << std::endl;
      Message w = Message("SAYHELLO", hostname + ":" + port);
      con->AddWork(std::make_shared<Message>(w));

      //// debug_print_link_state(this_node, "ORIGINAL");
      // Update our link state
      link_state[this_node].push_back(m.sender_id);
      // debug_print_link_state(this_node, "MODIFIED");

      fix_link_state();

      // Send LSUPDATE
      Message lsu = Message("LSUPDATE", this_node, link_state[this_node], max_ttl, 1);
      log_message("i", lsu, con);
      main_mtx.lock();
      for (auto& neighbor : connections) {
        // debug_connection_send(lsu, neighbor);
        // std::cout << lsu.getString() << std::endl;
        neighbor->AddWork(std::make_shared<Message>(lsu));
      }
      main_mtx.unlock();
      message_cache[this_node] = lsu;
      // message_cache.insert(std::string(lsu.message_id));
    }
  }

  /////////////////////////////////////////////////////////////////////////////
  // Receive SAYHELLO back- this code path is executed when we connect first //
  /////////////////////////////////////////////////////////////////////////////
  else {
    log_message("r", m, con);
    // std::cout << "[" << con->connection_number << "]\t" << m.message_type <<
    //     " message received from " << con->neighbor_node_id << std::endl;

    // debug_print_link_state(this_node, "ORIGINAL");
    // Update our link state
    link_state[this_node].push_back(m.sender_id);
    // debug_print_link_state(this_node, "MODIFIED");

    fix_link_state();

    // Send LSUPDATE
    Message lsu = Message("LSUPDATE", this_node, link_state[this_node], max_ttl, 1);
    log_message("i", lsu, con);
    main_mtx.lock();
    for (auto& neighbor : connections) {
      // debug_connection_send(lsu, neighbor);
      // std::cout << lsu.getString() << std::endl;
      neighbor->AddWork(std::make_shared<Message>(lsu));
    }
    main_mtx.unlock();
    message_cache[this_node] = lsu;
    // message_cache.insert(std::string(lsu.message_id));
  }

  ///////////////////////
  // Main message loop //
  ///////////////////////
  if (con->is_duplicate == false) {
    while (true) {
      char buf[2048];
      int bytes_read = read(con->socket_fd, buf, sizeof(buf));
      std::string plaintext = buf;
      Message m = Message(plaintext);

      if(m.content_length <= -1 || m.sender_id == "unknown" || m.content_length > 500) {
        continue;
      }

      log_message("r", m, con);
      fix_link_state();

      main_mtx.lock();
      if (is_running == false) {
        main_mtx.unlock();
        break;
      } else if (bytes_read == 0 || bytes_read == -1) {
        // Send LSUPDATE
        link_state.erase(link_state.find(con->neighbor_node_id));
        link_state[this_node].erase(std::find(link_state[this_node].begin(),
                                              link_state[this_node].end(),
                                              con->neighbor_node_id));

        std::map<std::string, std::vector<std::string>> new_link_state;
        for(auto& item : link_state) {
          if(item.second.size() == 1 && item.second[0] == con->neighbor_node_id) {
            link_state.erase(link_state.find(item.first));
          } else if(item.second.size() == 0) {
            link_state.erase(link_state.find(item.first));
          } else {
            new_link_state.insert(item);
          }
        }
        // NEED TO RUN BFS

        link_state = new_link_state;


        std::vector<std::string> nullvec;
        nullvec.push_back("DOWN");

        remove_dead();

        Message lsu = Message("LSUPDATE", this_node, link_state[this_node], max_ttl, 1);
        // log_message("i", lsu, con);
        Message lsu2 = Message("LSUPDATE", con->neighbor_node_id, nullvec, max_ttl, 1);
        for(auto& neighbor : connections) {
          neighbor->AddWork(std::make_shared<Message>(lsu));
          neighbor->AddWork(std::make_shared<Message>(lsu2));
        }
        
        main_mtx.unlock();
        break;
      }
      main_mtx.unlock();

      //////////////////////////////
      // Handle LSUPDATE messages //
      //////////////////////////////
      if (m.message_type == "LSUPDATE") {
        if(message_cache[m.sender_id].message_id != m.message_id
           && m.sender_id != this_node
           && m.content_length < 500
           //incoming_start > prev_start
           ) {
        //if (message_cache.find(m.message_id) == message_cache.end()) {
          // Print received message
          // debug_connection_receive(m, con);
          // std::cout << m.getString() << std::endl;
          // debug_print_link_state(m.sender_id, "ORIGINAL");

          main_mtx.lock();
          m.ttl--;


          // Check for new row
          bool send_update = false;
          if (link_state.find(m.sender_id) == link_state.end()) {
            send_update = true;
          } else if (link_state[m.sender_id][0] == "DOWN") {
            send_update = true;
          }

          // if (send_update == false) {
          //   if (message_cache[m.sender_id].origin_start_time == m.origin_start_time &&
          //       message_cache[m.sender_id].link_state == m.link_state) {
          //     main_mtx.unlock();
          //     continue;
          //   }
          // }

          // Update link state
          link_state[m.sender_id] = m.link_state;

          // debug_print_link_state(m.sender_id, "MODIFIED");

          // Forward connection
          message_cache[m.sender_id] = m;
          // Message forward = Message("LSUPDATE",
          //                           m.sender_id,
          //                           m.link_state,
          //                           m.ttl,
          //                           m.flood);
          // UNCOMMENT
          // forward.message_id = m.message_id;
          if (m.ttl > 0) {
            for(auto& neighbor : connections) {
              if(neighbor != con) {
                // debug_connection_send(m, neighbor);
                // std::cout << m.getString() << std::endl;
                neighbor->AddWork(std::make_shared<Message>(m));
                log_message("d", m, neighbor);
              }
            }
          }
          main_mtx.unlock();
          // message_cache.insert(m.message_id);

          fix_link_state();
          // Flood this node's link state if new row
          if (send_update) {
            Message lsu = Message("LSUPDATE",
                                  this_node,
                                  link_state[this_node],
                                  max_ttl,
                                  m.flood);
            log_message("i", lsu, con);
            main_mtx.lock();
            for (auto& neighbor : connections) {
              // debug_connection_send(lsu, neighbor);
              // std::cout << lsu.getString() << std::endl;
              neighbor->AddWork(std::make_shared<Message>(lsu));
            }
            main_mtx.unlock();
            message_cache[this_node] = lsu;
            // message_cache.insert(lsu.message_id);
          }

        }
      }
      /////////////////////////////////
      // Traceroute message handling //
      /////////////////////////////////
      else if (m.message_type == "UCASTAPP") {
        // Init forwarding table
        calc_forwarding_table();
        --m.ttl;

        ///////////////////////////////
        // Handle Traceroute-Request //
        ///////////////////////////////
        if (m.body_type == "REQUEST") {
          // Write message back
          if (m.to == this_node) {
            Message trc_resp = Message("UCASTAPP",
                                       max_ttl,
                                       this_node,
                                       m.sender_id,
                                       m.seq_no,
                                       "RESPONSE");
            con->AddWork(std::make_shared<Message>(trc_resp));
            log_message("i", trc_resp, con);
          } else {
            // Handle forward and zerottl
            // Send zero if ttl zero
            if(m.ttl == 0) {
              Message trc_zero = Message("UCASTAPP",
                                         max_ttl,
                                         this_node,
                                         m.sender_id,
                                         m.seq_no,
                                         "ZERO");
              con->AddWork(std::make_shared<Message>(trc_zero));
              log_message("i", trc_zero, con);
            }
            // Forward
            else {
              for(auto& link : connections) {
                if(link->neighbor_node_id == forwarding_table[m.to] && link->socket_fd > 0) {
                  link->AddWork(std::make_shared<Message>(m));
                  // log_message("f", m);
                  break;
                }
              }
            }
          }
        }

        ////////////////////////////////
        // Handle Traceroute-Response //
        ////////////////////////////////
        else if (m.body_type == "RESPONSE") {
          if(m.to == this_node) {
            // arrived
            trc.lock();
            trc_queue.push(std::make_shared<Message>(m));
            trc_cv.notify_all();
            trc.unlock();
          } else {
            // forward
            if (m.ttl == 0)
              continue;
            for(auto& link : connections) {
              if(link->neighbor_node_id == forwarding_table[m.to] && link->socket_fd > 0) {
                link->AddWork(std::make_shared<Message>(m));
                // log_message("f", m);
              }
            }
          }
        }

        ///////////////////////////////
        // Handle Traceroute-ZeroTTL //
        ///////////////////////////////
        else if (m.body_type == "ZERO") {
          if(m.to == this_node) {
            trc.lock();
            trc_queue.push(std::make_shared<Message>(m));
            trc_cv.notify_all();
            trc.unlock();
          } else {
            //forward
            if (m.ttl == 0)
              continue;
            for(auto& link : connections) {
              if(link->neighbor_node_id == forwarding_table[m.to] && link->socket_fd > 0) {
                link->AddWork(std::make_shared<Message>(m));
                // log_message("f", m);
              }
            }
          }
        }

      }
      /////////////////////////
      // Other messages here //
      /////////////////////////
    }
  }

  ////////////////////////
  // Disconnect message //
  ////////////////////////
  Message w = Message("special", con->neighbor_node_id);
  con->AddWork(std::make_shared<Message>(w));

  main_mtx.lock();
  if (con->socket_fd >= 0) {
    con->ShutDown();
  }
  con->socket_fd = -2;
  main_mtx.unlock();
}

void connection_write(int connection_number) {
  main_mtx.lock();
  Connection* con = connections[connection_number - 1];
  main_mtx.unlock();

  while (true) {
    std::shared_ptr<Message> w = con->WaitForWork();
    if (w->message_type != "special") {
      char buf[2048];
      std::string message = w->getString();
      message.copy(buf, message.size());
      write(con->socket_fd, buf, sizeof(buf));
    } else {
      break;
    }
  }
}

void neighbor(std::string neighbors, std::string interval) {
  /////////////////////////////////////////
  // Initialize data for neighbor thread //
  /////////////////////////////////////////
  int interval_usec = std::stoi(interval) * 1000000;

  std::vector<std::string> orig_list;
  std::string item;
  size_t pos = 0;
  if (neighbors.find(",") == std::string::npos) {
    // If only one neighbor
    orig_list.push_back(neighbors);
  } else {
    // Multiple comma-seperated neighbors
    while ((pos = neighbors.find(",")) != std::string::npos) {
      item = neighbors.substr(0, pos);
      neighbors.erase(0, pos + 1);
      orig_list.push_back(item);
    }
    if (neighbors.length() != 0) {
      item = neighbors.substr(0, std::string::npos);
      orig_list.push_back(item);
    }
  }
  
  ///////////////////////////////
  // Logic for neighbor thread //
  ///////////////////////////////
  while (true) {
    main_mtx.lock();
    std::vector<std::string> list(orig_list);
    if (is_running) {
      for(auto &connection : connections) {
        auto find = std::find(list.begin(), list.end(), connection->neighbor_node_id);
        if (find != list.end() && connection->socket_fd >= 0) {
          list.erase(find);
        }
      }
    } else {
      main_mtx.unlock();
      break;
    }
    main_mtx.unlock();

    for(auto &node : list) {
      int newsockfd = create_client_socket(node.substr(0, 9).c_str(),
                                           node.substr(10, std::string::npos).c_str(),
                                           0);
      int eval = my_connect(newsockfd,
                            node.substr(0, 9).c_str(),
                            node.substr(10, std::string::npos).c_str(),
                            0);

      if (eval != -1) {
        main_mtx.lock();
        Connection* c = new Connection(newsockfd);
        c->write_thread = std::make_shared<std::thread>(connection_write,
                                                       curr_connection_number);
        c->read_thread = std::make_shared<std::thread>(connection_read,
                                                      curr_connection_number);
        c->connection_number = curr_connection_number;
        c->state = 1;
        c->neighbor_node_id = node;
        Message w = Message("SAYHELLO", hostname + ":" + port);
        c->AddWork(std::make_shared<Message>(w));
        log_message("i", w, c);
        // std::cout << "Sending SAYHELLO to " << c->neighbor_node_id << std::endl;
        connections.push_back(c);
        ++curr_connection_number;
        main_mtx.unlock();
      }
    }
    usleep(interval_usec);
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "\033[1;31mUSAGE:\033[0m ./pa6 INIFILE" << std::endl;
    return 1;
  }

  ///////////////////////
  // Parse config file //
  ///////////////////////
  ini_map config = ini_to_map(argv[1]);
  port = config["startup"]["port"];
  hostname = config["startup"]["host"];
  this_node = hostname + ":" + port;
  pidfile = config["startup"]["pidfile"];
  logfile = config["startup"]["logfile"];
  std::ofstream pid(pidfile);
  if(pid.is_open()) {
    pid << getpid() << std::endl;
  } else {
    return 1;
  }
  pid.close();

  logs.open(logfile);

  std::string neighbors;
  auto topology = config["topology"];
  for (auto& line : topology) {
    std::string line_port = line.first.substr(10, std::string::npos);
    if (line_port == port) {
      neighbors = line.second;
    }
  }

  std::string interval = config["params"]["neighbor_retry_interval"];
  max_ttl = std::stoi(config["params"]["max_ttl"]);
  msg_lifetime = std::stoi(config["params"]["msg_lifetime"]);

  do_log_sayhello = config["logging"]["SAYHELLO"] == "1";
  do_log_lsupdate = config["logging"]["LSUPDATE"] == "1";
  do_log_ucastapp = config["logging"]["UCASTAPP"] == "1";

  ////////////////////////
  // Init master socket //
  ////////////////////////
  master_sock_fd = create_master_socket(port.c_str(), 0);

  ////////////////////
  // Create threads //
  ////////////////////
  std::thread console_thread(console);
  std::thread reaper_thread(reaper);
  std::thread neighbor_thread(neighbor, neighbors, interval);

  /////////////////
  // Start timer //
  /////////////////
  gettimeofday(&begin_time, NULL);

  /////////////////
  // Accept loop //
  /////////////////
  while (true) {
    int newsockfd = -1;

    newsockfd = my_accept(master_sock_fd, 0);
    if (newsockfd != -1) {
      main_mtx.lock();
      if (is_running == false) {
        shutdown(newsockfd, SHUT_RDWR);
        main_mtx.unlock();
        break;
      }
      Connection* c = new Connection(newsockfd);
      c->write_thread = std::make_shared<std::thread>(connection_write,
                                                     curr_connection_number);
      c->read_thread = std::make_shared<std::thread>(connection_read,
                                                    curr_connection_number);
      c->connection_number = curr_connection_number;
      c->state = 0;
      connections.push_back(c);

      ++curr_connection_number;
      main_mtx.unlock();
    } else {
      break;
    }
  }

  ///////////////////////
  // Join with threads //
  ///////////////////////
  neighbor_thread.join();
  reaper_thread.join();
  console_thread.join();

  logs.close();
  remove(pidfile.c_str());

  return 0;
}
