#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <vector>
#include <sstream>
#include <string>
#include <algorithm>
#include "resp_parser.h"
#include "store.h"
#include "server.h"

int main(int argc, char **argv)
{
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  std::string master = "";

  int port = 6379;
  for (int i = 1; i < argc; i++)
  {
    if (std::string(argv[i]) == "--port" && i + 1 < argc)
    {
      port = std::stoi(argv[i + 1]);
    }
    if (std::string(argv[i]) == "--replicaof" && i + 1 < argc)
    {
      isReplica = true;
      master = argv[i + 1];
    }
  }

  if (isReplica)
  {
    std::string master_host = master.substr(0, master.find(' '));
    std::string master_port = master.substr(master.find(' ') + 1);
    int replica_fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in master_addr;
    master_addr.sin_family = AF_INET;
    master_addr.sin_port = htons(std::stoi(master_port));
    inet_pton(AF_INET, master_host.c_str(), &master_addr.sin_addr);

    connect(replica_fd, (struct sockaddr *)&master_addr, sizeof(master_addr));

    char buffer[4096];
    send(replica_fd, "*1\r\n$4\r\nPING\r\n", 14, 0); // send a PING to master to trigger replication
    recv(replica_fd, buffer, sizeof(buffer), 0);
    std::string replconf1 = "*3\r\n$8\r\nREPLCONF\r\n$14\r\nlistening-port\r\n$" +
                            std::to_string(std::to_string(port).size()) + "\r\n" +
                            std::to_string(port) + "\r\n";
    send(replica_fd, replconf1.c_str(), replconf1.size(), 0);
    recv(replica_fd, buffer, sizeof(buffer), 0);
    std::string replconf2 = "*3\r\n$8\r\nREPLCONF\r\n$4\r\ncapa\r\n$6\r\npsync2\r\n";
    send(replica_fd, replconf2.c_str(), replconf2.size(), 0);
    recv(replica_fd, buffer, sizeof(buffer), 0);

    std::string sync_command = "*3\r\n$5\r\nPSYNC\r\n$1\r\n?\r\n$2\r\n-1\r\n";
    send(replica_fd, sync_command.c_str(), sync_command.size(), 0);
    recv(replica_fd, buffer, sizeof(buffer), 0);

    std::string data = std::string(buffer);
    if (data.find("+FULLRESYNC") != std::string::npos)
    {
      // master is sending full sync data, we can ignore it for now since we don't have any data to sync
      std::cout << "Received FULLRESYNC from master, ignoring for now\n";
    }
    recv(replica_fd, buffer, sizeof(buffer), 0); // receive the full sync data (which we will ignore for now)
    memset(buffer, 0, sizeof(buffer));

    std::thread t(handle_client, replica_fd);
    t.detach();
  }

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
  {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
  {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
  {
    std::cerr << "Failed to bind to port " << port << "\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0)
  {
    std::cerr << "listen failed\n";
    return 1;
  }

  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);
  std::cout << "Waiting for a client to connect...\n";
  std::cout << "Logs from your program will appear here!\n";

  int client_fd;
  while ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len)) >= 0)
  {
    std::thread t(handle_client, client_fd);
    t.detach();
  }

  close(server_fd);
  return 0;
}