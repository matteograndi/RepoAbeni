/*
 *  Copyright (c) 2010 Luca Abeni
 *  Copyright (c) 2010 Csaba Kiraly
 *
 *  This is free software; see lgpl-2.1.txt
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "net_helper.h"

struct nodeID {
  struct sockaddr_in addr;
  int fd;
};

int wait4data(const struct nodeID *s, struct timeval *tout, int *user_fds)
{
  fd_set fds;
  int i, res, max_fd;

  FD_ZERO(&fds);
  if (s) {
    max_fd = s->fd;
    FD_SET(s->fd, &fds);
  } else {
    max_fd = -1;
  }
  if (user_fds) {
    for (i = 0; user_fds[i] != -1; i++) {
      FD_SET(user_fds[i], &fds);
      if (user_fds[i] > max_fd) {
        max_fd = user_fds[i];
      }
    }
  }
  res = select(max_fd + 1, &fds, NULL, NULL, tout);
  if (res <= 0) {
    return res;
  }
  if (s && FD_ISSET(s->fd, &fds)) {
    return 1;
  }

  /* If execution arrives here, user_fds cannot be 0
     (an FD is ready, and it's not s->fd) */
  for (i = 0; user_fds[i] != -1; i++) {
    if (!FD_ISSET(user_fds[i], &fds)) {
      user_fds[i] = -2;
    }
  }

  return 2;
}

struct nodeID *create_node(const char *IPaddr, int port)
{
  struct nodeID *s;
  int res;

  s = malloc(sizeof(struct nodeID));
  memset(s, 0, sizeof(struct nodeID));
  s->addr.sin_family = AF_INET;
  s->addr.sin_port = htons(port);
  res = inet_aton(IPaddr, &s->addr.sin_addr);
  if (res == 0) {
    free(s);

    return NULL;
  }

  s->fd = -1;

  return s;
}

struct nodeID *net_helper_init(const char *my_addr, int port, const char *config)
{
  int res;
  struct nodeID *myself;

  myself = create_node(my_addr, port);
  if (myself == NULL) {
    fprintf(stderr, "Error creating my socket (%s:%d)!\n", my_addr, port);
  }
  myself->fd =  socket(AF_INET, SOCK_DGRAM, 0);
  if (myself->fd < 0) {
    free(myself);

    return NULL;
  }
  fprintf(stderr, "My sock: %d\n", myself->fd);

  res = bind(myself->fd, (struct sockaddr *)&myself->addr, sizeof(struct sockaddr_in));
  if (res < 0) {
    /* bind failed: not a local address... Just close the socket! */
    close(myself->fd);
    free(myself);

    return NULL;
  }

  return myself;
}

void bind_msg_type (uint8_t msgtype)
{
}

int send_to_peer(const struct nodeID *from, struct nodeID *to, const uint8_t *buffer_ptr, int buffer_size)
{
  struct msghdr msg = {0};
  uint8_t my_hdr;
  struct iovec iov[2];
  int res;

  iov[0].iov_base = &my_hdr;
  iov[0].iov_len = 1;
  msg.msg_name = &to->addr;
  msg.msg_namelen = sizeof(struct sockaddr_in);
  msg.msg_iovlen = 2;
  msg.msg_iov = iov;

  do {
    iov[1].iov_base = buffer_ptr;
    if (buffer_size > 1024 * 60) {
      iov[1].iov_len = 1024 * 60;
      my_hdr = 0;
    } else {
      iov[1].iov_len = buffer_size;
      my_hdr = 1;
    }
    buffer_size -= iov[1].iov_len;
    buffer_ptr += iov[1].iov_len;
    res = sendmsg(from->fd, &msg, 0);
  } while (buffer_size > 0);

  return res;
}

int recv_from_peer(const struct nodeID *local, struct nodeID **remote, uint8_t *buffer_ptr, int buffer_size)
{
  int res, recv;
  struct sockaddr_in raddr;
  struct msghdr msg = {0};
  uint8_t my_hdr;
  struct iovec iov[2];

  iov[0].iov_base = &my_hdr;
  iov[0].iov_len = 1;
  msg.msg_name = &raddr;
  msg.msg_namelen = sizeof(struct sockaddr_in);
  msg.msg_iovlen = 2;
  msg.msg_iov = iov;

  *remote = malloc(sizeof(struct nodeID));
  if (*remote == NULL) {
    return -1;
  }

  recv = 0;
  do {
    iov[1].iov_base = buffer_ptr;
    if (buffer_size > 1024 * 60) {
      iov[1].iov_len = 1024 * 60;
    } else {
      iov[1].iov_len = buffer_size;
    }
    buffer_size -= iov[1].iov_len;
    buffer_ptr += iov[1].iov_len;
    res = recvmsg(local->fd, &msg, 0);
    recv += (res - 1);
  } while ((my_hdr == 0) && (buffer_size > 0));
  memcpy(&(*remote)->addr, &raddr, msg.msg_namelen);
  (*remote)->fd = -1;

  return recv;
}

int node_addr(const struct nodeID *s, char *addr, int len)
{
  char buff[96];
  int n;

  if (!inet_ntop(AF_INET, &(s->addr.sin_addr), buff, 96))
    return 1;
  n = snprintf(addr, len, "%s:%d", buff, ntohs(s->addr.sin_port));
  return len < n;
}

struct nodeID *nodeid_dup(struct nodeID *s)
{
  struct nodeID *res;

  res = malloc(sizeof(struct nodeID));
  if (res != NULL) {
    memcpy(res, s, sizeof(struct nodeID));
  }

  return res;
}

int nodeid_equal(const struct nodeID *s1, const struct nodeID *s2)
{
  return (memcmp(&s1->addr, &s2->addr, sizeof(struct sockaddr_in)) == 0);
}

int nodeid_dump(uint8_t *b, const struct nodeID *s, size_t max_write_size)
{
  if (max_write_size < sizeof(struct sockaddr_in)) return -1;

  memcpy(b, &s->addr, sizeof(struct sockaddr_in));

  return sizeof(struct sockaddr_in);
}

struct nodeID *nodeid_undump(const uint8_t *b, int *len)
{
  struct nodeID *res;
  res = malloc(sizeof(struct nodeID));
  if (res != NULL) {
    memcpy(&res->addr, b, sizeof(struct sockaddr_in));
    res->fd = -1;
  }
  *len = sizeof(struct sockaddr_in);

  return res;
}

void nodeid_free(struct nodeID *s)
{
  free(s);
}

int node_ip(const struct nodeID *s, char *ip, int len)
{
  return inet_ntop(AF_INET, &(s->addr.sin_addr), ip, len) != 0;
}

int node_port(const struct nodeID *s)
{
  return ntohs(s->addr.sin_port);
}
