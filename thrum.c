#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <lo/lo.h>
#include <sys/time.h>

#define VOTE_PERIOD 2
#define SYNC_TIMEOUT 2

double vote_time = 0;
double assert_time = 0;

lo_address master_address = NULL;
lo_address multicast_address = NULL;
int master_id = -1;
lo_server ms;

int id;
double time_offset = 0;

int winning = 0;
int voting = 0;
int master = 0;
int synced = 0;
double sync_start = 0;
int ping_n = 0;

double now() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return((double) tv.tv_sec + ((double) tv.tv_usec / 1000000.0) + time_offset);
}

void send_ping() {
  printf("send ping\n");
  int rv = lo_send_from(multicast_address, 
                        lo_server_thread_get_server(ms), 
                        LO_TT_IMMEDIATE,
                        "/app/thrum/ping", "if", id, now());
  if (rv == -1) {
    printf("multicast send error %d: %s\n", lo_address_errno(multicast_address), 
           lo_address_errstr(multicast_address)
           );
  }
}

void send_pong(int slave_id, double sent) {
  printf("send pong\n");
  int rv = lo_send_from(multicast_address, 
                        lo_server_thread_get_server(ms), 
                        LO_TT_IMMEDIATE,
                        "/app/thrum/pong", "iff", slave_id, sent, now());
  if (rv == -1) {
    printf("multicast send error %d: %s\n", lo_address_errno(multicast_address), 
           lo_address_errstr(multicast_address)
           );
  }
}

void send_assert_master() {
  int rv = lo_send_from(multicast_address, 
                        lo_server_thread_get_server(ms), 
                        LO_TT_IMMEDIATE,
                        "/app/thrum/assert", "i", id);
  if (rv == -1) {
    printf("multicast send error %d: %s\n", lo_address_errno(multicast_address), 
           lo_address_errstr(multicast_address)
           );
  }
}

void send_claim_master() {
  int rv = lo_send_from(multicast_address, 
                        lo_server_thread_get_server(ms), 
                        LO_TT_IMMEDIATE,
                        "/app/thrum/claim", "i", id);
  //printf("claiming\n");
  if (rv == -1) {
    printf("multicast send error %d: %s\n", lo_address_errno(multicast_address), 
           lo_address_errstr(multicast_address)
           );
  }
}

void error(int num, const char *msg, const char *path) {
    printf("liblo server error %d in path %s: %s\n", num, path, msg);
}


int generic_handler(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, void *user_data)
{
    int i;
    
    printf("path: <%s>\n", path);
    for (i=0; i<argc; i++) {
      printf("arg %d '%c' ", i, types[i]);
      lo_arg_pp(types[i], argv[i]);
      printf("\n");
    }
    printf("\n");

    return 1;
}

int claim_handler(const char *path, const char *types, lo_arg **argv,
                 int argc, lo_message data, void *user_data) {
  int remote_id = argv[0]->i;
  //printf("got claim\n");
  if (! voting) {
    voting = 1;
    vote_time = now();
    winning = (id <= remote_id);
  }
  else {
    if (id > remote_id) {
      winning = 0;
    }
  }
  return(0);
}

int assert_handler(const char *path, const char *types, lo_arg **argv,
                 int argc, lo_message data, void *user_data) {
  master_id = argv[0]->i;
  //printf("got assert from %d\n", master_id);

  voting = 0;
  master_address = lo_message_get_source(data);
  assert_time = now();

  if (master_id == id) {
    master = 1;
  }
  else if (master) {
    if (master_id < id) {
      master = 0;
    }
  }

  if (master_id >= 0 && synced == 0) {
    if ((now() - sync_start) > SYNC_TIMEOUT) {
      sync_start = now();
      ping_n = 0;
      send_ping();
    }
  }

  return(0);
}

int ping_handler(const char *path, const char *types, lo_arg **argv,
                 int argc, lo_message data, void *user_data) {
  if (master) {
    int slave_id = argv[0]->i;
    double sent = argv[1]->f;
    send_pong(slave_id, sent);
  }
  return(0);
}

int pong_handler(const char *path, const char *types, lo_arg **argv,
                 int argc, lo_message data, void *user_data) {
  if (!master && argv[0]->i == id) {
    double t = now();
    double sent = argv[1]->f;
    double received = argv[2]->f;
    double latency = (t - sent) / 2;
    // TODO - average over several goes
    time_offset = received - t - latency;
    synced = 1;
    printf("synced with offset %f, latency %f", time_offset, latency);
  }
  
  return(0);
}

void setid() {
  id = rand();
}

void initrand() {
  struct timeval time;
  gettimeofday(&time,NULL);
  srand((time.tv_sec * 1000) + (time.tv_usec / 1000) + getpid());
}

void cycle() {
  if (master) {
    printf("master\n");
  }
  else {
    printf("client\n");
  }

  if (voting && ((now() - vote_time) > VOTE_PERIOD) && winning) {
    master = 1;
    synced = 1;
    master_id = id;
    master_address = NULL;
  }
  
  if (master_id<0 && !voting) {
    send_claim_master();
  }

  if (master) {
    send_assert_master();
  }
}

int main (int argc, char **argv) {
  initrand();
  setid();
  ms = lo_server_thread_new_multicast("224.0.1.1", "6010", error);
  //st = lo_server_thread_new(NULL, error);
  //lo_server_thread_start(st);
  //lo_server_thread_add_method(ms, NULL, NULL, generic_handler, NULL);
  lo_server_thread_add_method(ms, "/app/thrum/claim", "i", claim_handler, NULL);
  lo_server_thread_add_method(ms, "/app/thrum/assert", "i", assert_handler, NULL);
  lo_server_thread_add_method(ms, "/app/thrum/ping", "if", ping_handler, NULL);
  lo_server_thread_add_method(ms, "/app/thrum/pong", "iff", pong_handler, NULL);
  multicast_address = lo_address_new("224.0.1.1", "6010");
  lo_address_set_ttl(multicast_address, 1); /* set multicast scope to LAN */
  sleep(1);
  lo_server_thread_start(ms);

  while (1) {
    cycle();
    sleep(1);
  }
  return(0);
}
