#ifndef MESSAGE_H
#define MESSAGE_H

#include <string>
#include <vector>
#include <set>

#include <openssl/sha.h>
#include <sys/time.h>

class Message {
public:
  std::string message_type;
  std::string sender_id;
  //char message_id[(SHA_DIGEST_LENGTH << 1) + 1];
  std::string message_id;
  std::string origin_start_time;
  int content_length;
  unsigned flood;

  std::vector<std::string> link_state;
  unsigned ttl;

  std::string to;
  std::string trc_body;
  std::string body_type;
  int seq_no;

  Message();
  Message(std::string plaintext);
  Message(std::string message_type, std::string sender_id);
  Message(std::string message_type,
          std::string sender_id,
          std::vector<std::string> link_state,
          unsigned ttl,
          unsigned flood);
  Message(std::string message_type,
          unsigned ttl,
          std::string from,
          std::string to,
          int seq_no,
          std::string body_type);

  std::string getString();
};
#endif
