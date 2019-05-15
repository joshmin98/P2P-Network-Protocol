#include "message.h"

#include <vector>
#include <string>
#include <openssl/sha.h>
#include <stdexcept>

#include <bits/stdc++.h>
#include <iostream>

Message::Message() {
  this->trc_body = "NONE";
}


#define SOFT_STATE 0
 
void GetObjID(
    char node_id[40],
    const char *obj_category,
    char hexstring_of_unique_obj_id[(SHA_DIGEST_LENGTH<<1)+1],
    char origin_start_time[18])
{
  static unsigned long seq_no=0L;
  static struct timeval node_start_time;

  if (seq_no++ == 0L) {
    gettimeofday(&node_start_time, NULL);
  }
#if SOFT_STATE
  static char hexchar[]="0123456789abcdef";
  char unique_str[128];
  char returned_obj_id[SHA_DIGEST_LENGTH];

  snprintf(unique_str, sizeof(unique_str), "%s_%d_%s_%1ld",
           node_id, (int)(node_start_time.tv_sec), obj_category, (long)seq_no);
  SHA1((unsigned char *)unique_str, strlen(unique_str), (unsigned char *)returned_obj_id);
  for (int i=0; i < SHA_DIGEST_LENGTH; i++) {
    unsigned char ch=(unsigned char)returned_obj_id[i];
    int hi_nibble=(int)(unsigned int)((ch>>4)&0x0f);
    int lo_nibble=(int)(unsigned int)(ch&0x0f);

    hexstring_of_unique_obj_id[i<<1] = hexchar[hi_nibble];
    hexstring_of_unique_obj_id[(i<<1)+1] = hexchar[lo_nibble];
  }
  hexstring_of_unique_obj_id[SHA_DIGEST_LENGTH<<1] = '\0';
#else /* ~SOFT_STATE */
  struct timeval now;
  gettimeofday(&now, NULL);
  /* [BC: fixed 4/10/2019] */
  snprintf(hexstring_of_unique_obj_id, (SHA_DIGEST_LENGTH<<1)+1,
           "%s_%010d.%06d", node_id, (int)(now.tv_sec), (int)(now.tv_usec));
#endif /* SOFT_STATE */
  snprintf(origin_start_time, 18, "%010d.%06d", (int)(node_start_time.tv_sec), (int)(node_start_time.tv_usec));
}

// void GetObjID(char origin_start_time[18]) {
//   static unsigned long seq_no=0L;
//   static struct timeval node_start_time;
//   if (seq_no++ == 0L) {
//     gettimeofday(&node_start_time, NULL);
//   }
//   struct timeval now;
//   gettimeofday(&now, NULL);
//   snprintf(origin_start_time, 18, "%010d.%06d",
//            (int)(node_start_time.tv_sec),
//            (int)(node_start_time.tv_usec));
// }

Message::Message(std::string message_type, std::string sender_id) {
  this->message_type = message_type;
  this->sender_id = sender_id;
  this->ttl = 1;
}

Message::Message(std::string message_type,
                 std::string sender_id,
                 std::vector<std::string> link_state,
                 unsigned ttl,
                 unsigned flood) {
  this->message_type = message_type;
  this->sender_id = sender_id;
  this->link_state = link_state;
  this->ttl = ttl;

  char node_id[40];
  this->sender_id.copy(node_id, 40);
  char hexstring_of_unique_obj_id[(SHA_DIGEST_LENGTH<<1)+1];
  char origin_start_time[18];
  GetObjID(node_id, "msg", hexstring_of_unique_obj_id, origin_start_time);


  this->message_id = hexstring_of_unique_obj_id;
  this->origin_start_time = origin_start_time;
  this->flood = flood;
  int content_length = 0;
  for(auto item : link_state) {
    content_length += item.size();
  }
  content_length += link_state.size() - 1;
  this->content_length = content_length;
}

Message::Message(std::string message_type,
                 unsigned ttl,
                 std::string from,
                 std::string to,
                 int seq_no,
                 std::string body_type) {
  this->message_type = message_type;
  this->ttl = ttl;
  this->seq_no = seq_no;
  this->sender_id = from;
  this->to = to;
  this->body_type = body_type;
  this->flood = 0;

  char node_id[40];
  this->sender_id.copy(node_id, 40);
  char hexstring_of_unique_obj_id[(SHA_DIGEST_LENGTH<<1)+1];
  char origin_start_time[18];
  GetObjID(node_id, "msg", hexstring_of_unique_obj_id, origin_start_time);


  this->message_id = hexstring_of_unique_obj_id;
  this->origin_start_time = origin_start_time;

  if(body_type == "REQUEST") {
    this->trc_body = "Traceroute-Request=" + this->to + "," +
        std::to_string(this->seq_no);
  } else if (body_type == "RESPONSE") {
    this->trc_body = "Traceroute-Response=" + this->sender_id + "," +
        std::to_string(this->seq_no);
  } else if (body_type == "ZERO") {
    this->trc_body = "Traceroute-ZeroTTL=" + this->sender_id + "," +
        std::to_string(this->seq_no);
  }
  this->content_length = this->trc_body.size();
}


Message::Message(std::string plaintext) {
  std::vector<std::string> data;

  std::string plaintext_copy = plaintext;

  size_t pos = 0;
  std::string delimiter = "\r\n";

  while ((pos = plaintext.find(delimiter)) != std::string::npos) {
    std::string line = plaintext.substr(0, pos);
    plaintext.erase(0, pos + delimiter.length());
    data.push_back(line);
  }

  for (std::string line : data) {
    if (line.find("353NET/1.0 SAYHELLO") != std::string::npos) {
      this->message_type = "SAYHELLO";
    } else if (line.find("353NET/1.0 special") != std::string::npos) {
      this->message_type = "special";
    } else if (line.find("353NET/1.0 LSUPDATE") != std::string::npos) {
      this->message_type = "LSUPDATE";
    } else if (line.find("353NET/1.0 UCASTAPP TRACEROUTE/1.0") != std::string::npos) {
      this->message_type = "UCASTAPP";
    } else if (line.find("From: ") != std::string::npos) {
      this->sender_id = line.substr(6, std::string::npos);
    } else if (line.find("TTL: ") != std::string::npos) {
      try {
        this->ttl = std::stoi(line.substr(5, std::string::npos));
      } catch (const std::out_of_range& err) {

      }
    } else if (line.find("MessageID: ") != std::string::npos) {
      // strcpy(this->message_id, line.substr(11, std::string::npos).c_str());
      this->message_id = line.substr(11, std::string::npos);
    } else if (line.find("Content-Length: ") != std::string::npos) {
      try {
        this->content_length = std::stoi(line.substr(16, std::string::npos));
      } catch (const std::out_of_range& err) {
        this->content_length = -1;
      }
      if(this->content_length > 1000 || this->content_length <= -1) {
        this->content_length = -1;
      }
    } else if (line.find("To: ") != std::string::npos) {
      this->to = line.substr(4, std::string::npos);
    } else if (line.find("OriginStartTime: ") != std::string::npos) {
      this->origin_start_time = line.substr(17, std::string::npos);
    } else if (line.find("Flood: ") != std::string::npos) {
      try {
        this->flood = std::stoi(line.substr(7, std::string::npos));
      } catch (const std::out_of_range& err) {

      }
    }
  }

  // Get message body for linkstate
  if(this->message_type == "LSUPDATE") {
    plaintext = plaintext.substr(0, this->content_length);
    this->flood = 1;
    if (plaintext.find(",") == std::string::npos) {
      this->link_state.push_back(plaintext);
    } else {
      pos = 0;
      delimiter = ",";
      while ((pos = plaintext.find(delimiter)) != std::string::npos) {
        std::string node = plaintext.substr(0, pos);
        plaintext.erase(0, pos + delimiter.length());
        this->link_state.push_back(node);
      }
      this->link_state.push_back(plaintext);
    }
  }

  // Get message body for traceroute
  else if(this->message_type == "UCASTAPP") {
    this->flood = 0;
    this->trc_body = plaintext_copy.substr(plaintext_copy.find("\r\n\r\n") + 4,
                                          this->content_length);
    if (this->trc_body.find("Response") != std::string::npos) {
      this->body_type = "RESPONSE";
    } else if (this->trc_body.find("Request") != std::string::npos) {
      this->body_type = "REQUEST";
    } else if (this->trc_body.find("Zero") != std::string::npos) {
      this->body_type = "ZERO";
    }

    this->seq_no =  std::stoi(this->trc_body.substr(this->trc_body.find(",") + 1,
                                                  std::string::npos));
  }

  else if(this->message_type == "SAYHELLO") {
    this->flood = 0;
    this->content_length = 0;
  }
}

std::string Message::getString() {
  std::string message = "";
  if (this->message_type == "special" || this->message_type == "SAYHELLO") {
    message += "353NET/1.0 " + this->message_type + " NONE/1.0\r\n";
    message += "From: " + this->sender_id + "\r\n";
    message += "TTL: " + std::to_string(this->ttl) + "\r\n";
    message += "Content-Length: 0\r\n";
    message += "\r\n";
  } else if (this->message_type == "LSUPDATE") {
    // Create message body
    std::string update = "";
    for(unsigned i = 0; i < this->link_state.size(); ++i) {
      if (i == this->link_state.size() - 1) {
        update += link_state[i];
        break;
      }
      update += link_state[i] + ",";
    }
    this->content_length = update.size();
    if(this->content_length < -1 || this->content_length > 1000) {
      this->content_length = -1;
    }
    // Create message string
    message += "353NET/1.0 " + this->message_type + " NONE/1.0\r\n";
    message += "TTL: " + std::to_string(this->ttl) + "\r\n";
    message += "MessageID: " + std::string(this->message_id) + "\r\n";
    message += "From: " + this->sender_id + "\r\n";
    message += "OriginStartTime: " + std::string(this->origin_start_time) + "\r\n";
    message += "Content-Length: " + std::to_string(this->content_length) + "\r\n";
    message += "\r\n";
    message +=  update;
  } else if (this->message_type == "UCASTAPP") {
    message += "353NET/1.0 " + this->message_type + " TRACEROUTE/1.0\r\n";
    message += "TTL: " + std::to_string(this->ttl) + "\r\n";
    message += "MessageID: " + std::string(this->message_id) + "\r\n";
    message += "From: " + this->sender_id + "\r\n";
    message += "To: " + this->to + "\r\n";
    message += "OriginStartTime: " + std::string(this->origin_start_time) + "\r\n";
    message += "Content-Length: " + std::to_string(this->content_length) + "\r\n";
    message += "\r\n";
    message += this->trc_body;
  }
  return message;
}
