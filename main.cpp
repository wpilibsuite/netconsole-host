#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <sys/select.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>
#include <vector>

const int NETCONSOLE_PORT = 6666;
const int NETCONSOLE_IN_PORT = 6667;
const std::streamsize BUFFER_SIZE = 512;

typedef std::vector<decltype(in_addr().s_addr)> addr_vector;

/**
 * There are way too many points of failure, so this is used to die
 */
class nec_error : public std::exception {
 public:
  nec_error(std::string name) : exception() { perror(name.c_str()); }
};

addr_vector broadcast_addrs() {
  addr_vector addrs;
  ifaddrs* ifap = nullptr;

  getifaddrs(&ifap);
  for (ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
    // Only care about IPv4 at the moment
    if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET &&
        (ifa->ifa_flags & IFF_BROADCAST) == IFF_BROADCAST) {
      // add the broadcast IP
      addrs.push_back(
          reinterpret_cast<sockaddr_in*>(ifa->ifa_broadaddr)->sin_addr.s_addr);
    }
  }
  freeifaddrs(ifap);

  return addrs;
}

/**
 * This class manages closing the socket properly
 */
class nec_socket {
  int fd = -1;
  sockaddr_in dest, src;
  addr_vector addrs;

 public:
  nec_socket() : dest(), src(), addrs() {
    dest.sin_family = AF_INET;
    dest.sin_port = (in_port_t)htons(NETCONSOLE_PORT);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    src.sin_family = AF_INET;
    src.sin_port = (in_port_t)htons(NETCONSOLE_IN_PORT);
    src.sin_addr.s_addr = htonl(INADDR_ANY);

    addrs = broadcast_addrs();

    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) throw nec_error("socket");
  }

  int setup() {
    int enable = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable)) < 0)
      throw nec_error("socket options");
    if (bind(fd, reinterpret_cast<sockaddr*>(&src), sizeof(src)) < 0)
      throw nec_error("socket bind");
    return fd;
  }

  ssize_t read(void* buffer) {
    ssize_t len = recvfrom(fd, buffer, BUFFER_SIZE, 0, nullptr, nullptr);
    if (len == -1) throw nec_error("recvfrom");
    return len;
  }

  void write(const void* buffer, size_t length) {
    // send it to each interface
    for (auto addr : addrs) {
      dest.sin_addr.s_addr = addr;
      if (sendto(fd, buffer, length, 0, reinterpret_cast<sockaddr*>(&dest),
                 sizeof(dest)) < 0)
        throw nec_error("sendto");
    }
  }

  ~nec_socket() { close(fd); }
};

/**
 * This class manages our pipes
 */
class pipeset {
  int writepipe[2] = {-1, -1}, /* parent -> child */
      readpipe[2] = {-1, -1};  /* child -> parent */
 public:
  pipeset() {
    if (pipe(readpipe) < 0 || pipe(writepipe) < 0)
      throw nec_error("pipe creation");
  }

#define SERVER_READ readpipe[0]
#define CHILD_WRITE readpipe[1]
#define CHILD_READ writepipe[0]
#define SERVER_WRITE writepipe[1]
#define CLOSE_PIPESET(x) \
  if (x != -1) {         \
    close(x);            \
    x = -1;              \
  }

  void close_child() {
    CLOSE_PIPESET(CHILD_WRITE)
    CLOSE_PIPESET(CHILD_READ)
  }

  void close_server() {
    CLOSE_PIPESET(SERVER_WRITE)
    CLOSE_PIPESET(SERVER_READ)
  }

  void redirect_std() {
    dup2(CHILD_READ, STDIN_FILENO);
    dup2(CHILD_WRITE, STDOUT_FILENO);
    dup2(STDOUT_FILENO, STDERR_FILENO);
    close_child();
  }

  inline int read_fd() const { return SERVER_READ; }
  inline int write_fd() const { return SERVER_WRITE; }

#undef CLOSE_PIPESET
#undef SERVER_WRITE
#undef CHILD_READ
#undef CHILD_WRITE
#undef SERVER_READ

  ~pipeset() {
    close_child();
    close_server();
  }
};

int forkChild(int argc, char* argv[]) {
  std::cout << "➔ Launching «'" << argv[1];
  for (int i = 2; i < argc; i++) {
    std::cout << "' '" << argv[i];
  }
  std::cout << "'»" << std::endl;
  std::cout.flush();

  execvp(argv[1], &argv[1]);

  // if we are still here, something went wrong...
  throw nec_error("execvp");
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cout << argv[0] << " program [arguments ...]" << std::endl;
    return 1;
  }

  try {
    pipeset pipes;  // create a set of pipes so we can communicate with ...
    pid_t child = fork();  // our child

    if (child < 0)
      throw nec_error("fork");
    else if (child == 0) {
      // child
      pipes.close_server();
      pipes.redirect_std();
      return forkChild(argc, argv);
    } else
      pipes.close_child();

    nec_socket socket;  // create a new udp socket so we can broadcast
    int raw_socket = socket.setup();  // and set it up

    // create the file descriptor lists
    fd_set fds;
    FD_ZERO(&fds);
    int maxfd = std::max(raw_socket, pipes.read_fd()) + 1;
    char buffer[BUFFER_SIZE];

    setlinebuf(stdin);
    setlinebuf(stdout);

    // repeatedly select on the fd's
    while (true) {
      FD_SET(raw_socket, &fds);
      FD_SET(pipes.read_fd(), &fds);

      if (select(maxfd, &fds, nullptr, nullptr, nullptr) < 0)
        throw nec_error("select");

      // check for incoming client data
      if (FD_ISSET(pipes.read_fd(), &fds)) {
        ssize_t len = read(pipes.read_fd(), buffer, BUFFER_SIZE);
        if (len == 0)
          return 0;  // closed pipe
        else if (len < 0)
          throw nec_error("read");
        // TODO: we might want to make this optional
        std::cout.write(buffer, len);
        socket.write(buffer, static_cast<size_t>(len));
      }

      // also check for incoming socket data
      if (FD_ISSET(raw_socket, &fds)) {
        if (write(pipes.write_fd(), buffer,
                  static_cast<size_t>(socket.read(buffer))) < 0)
          throw nec_error("write");
      }
    }
  } catch (nec_error&) {
    // nec_error already printed out, just exit 1
    return 1;
  }
  return 0;
}

