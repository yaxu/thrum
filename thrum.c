#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <lo/lo.h>
#include <sys/time.h>

#define VOTE_PERIOD 2
#define SYNC_TIMEOUT 12
#define SYNC_COUNT 10

#define JAN_1970 0x83aa7e80      /* 2208988800 1970 - 1900 in seconds */

double vote_time = 0;
double assert_time = 0;

lo_address master_address = NULL;
lo_address multicast_address = NULL;
int master_id = -1;
lo_server ms;

int id;
float time_offset = 0;

int winning = 0;
int voting = 0;
int master = 0;
int synced = 0;
double sync_start = 0;
double time_offset_tests[SYNC_COUNT];
int ping_n = 0;
int sync_next = 0;

double t2f (lo_timetag t) {
  return (double) (t.sec - JAN_1970) + (double)t.frac * 0.00000000023283064365;
}

lo_timetag f2t (double f) {
  lo_timetag result;
  result.sec = floor(f) + JAN_1970;
  result.frac = (f - (double) result.sec) / 0.00000000023283064365;
  return(result);
}

double now_f() {
  double result;
  struct timeval tv;
  gettimeofday(&tv, NULL);
  
  result = (double) tv.tv_sec + ((double) tv.tv_usec / 1000000.0) + time_offset;
  return(result);
}

void now_t(lo_timetag *t) {
  int sec_offset = floor(time_offset);
  struct timeval tv;
  gettimeofday(&tv, NULL);

  tv.tv_sec += sec_offset;
  tv.tv_usec += (time_offset - (double) sec_offset) * 1000000;

  if (tv.tv_usec < 0) {
    printf("bump up\n");
    tv.tv_sec--;
    tv.tv_usec += 1000000;
  }
  if (tv.tv_usec >= 1000000) {
    printf("bump down\n");
    tv.tv_sec++;
    tv.tv_usec -= 1000000;
  }

  t->sec = tv.tv_sec + JAN_1970;
  t->frac = tv.tv_usec * 4294.967295;
}

void send_ping() {
  lo_timetag t;
  now_t(&t);
  
  printf("send ping at %f offset %f\n", t2f(t), time_offset);

  int rv = lo_send_from(multicast_address, 
                        lo_server_thread_get_server(ms), 
                        LO_TT_IMMEDIATE,
                        "/app/thrum/ping", "it", id, t);
  if (rv == -1) {
    printf("multicast send error %d: %s\n", lo_address_errno(multicast_address), 
           lo_address_errstr(multicast_address)
           );
  }
}

void send_pong(int slave_id, lo_timetag sent) {
  lo_timetag t;
  now_t(&t);
  printf("send pong\n");
  int rv = lo_send_from(multicast_address, 
                        lo_server_thread_get_server(ms), 
                        LO_TT_IMMEDIATE,
                        "/app/thrum/pong", "itt", slave_id, sent, t);
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
    vote_time = now_f();
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
  assert_time = now_f();

  if (master_id == id) {
    master = 1;
  }
  else if (master) {
    if (master_id < id) {
      master = 0;
    }
  }

  if (master_id >= 0 && synced < SYNC_COUNT) {
    double now = now_f();
    if ((now - sync_start) > SYNC_TIMEOUT) {
      sync_start = now;
      ping_n = 0;
      synced = 0;
      sync_next = 1;
    }
  }

  return(0);
}

int ping_handler(const char *path, const char *types, lo_arg **argv,
                 int argc, lo_message data, void *user_data) {
  if (master) {
    int slave_id = argv[0]->i;
    lo_timetag sent = argv[1]->t;
    printf("got ping from %i, %f\n", slave_id, t2f(sent));
    send_pong(slave_id, sent);
  }
  return(0);
}

int pong_handler(const char *path, const char *types, lo_arg **argv,
                 int argc, lo_message data, void *user_data) {
  if (!master && argv[0]->i == id) {
    lo_timetag sent = argv[1]->t;
    lo_timetag received = argv[2]->t;
    double now = now_f();
    printf("sent: %f received: %f now: %f\n", 
           t2f(sent), t2f(received), now);
    double latency = (now - t2f(sent)) / 2.0;
    // TODO - average over several goes

    time_offset_tests[synced++] = t2f(received) - now - latency;
    printf("[%d] pong with offset %f, latency %f\n", synced, time_offset_tests[synced-1], latency);
    if (synced < SYNC_COUNT) {
      sync_next = 1;
    }
    else {
      int i;
      float total = 0;
      for (i=0; i<SYNC_COUNT; ++i) {
        total += time_offset_tests[i];
      }
      time_offset = total / (float) SYNC_COUNT;
      printf("average: %f\n", time_offset);
    }
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

  if (voting && ((now_f() - vote_time) > VOTE_PERIOD) && winning) {
    master = 1;
    synced = SYNC_COUNT;
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
  lo_timetag t;
  now_t(&t);
  //printf("test: %f %f %f\n", now_f(), t2f(f2t(now_f())), t2f(t));

  initrand();
  setid();
  ms = lo_server_thread_new_multicast("224.0.1.1", "6010", error);
  //st = lo_server_thread_new(NULL, error);
  //lo_server_thread_start(st);
  //lo_server_thread_add_method(ms, NULL, NULL, generic_handler, NULL);
  lo_server_thread_add_method(ms, "/app/thrum/claim", "i", claim_handler, NULL);
  lo_server_thread_add_method(ms, "/app/thrum/assert", "i", assert_handler, NULL);
  lo_server_thread_add_method(ms, "/app/thrum/ping", "it", ping_handler, NULL);
  lo_server_thread_add_method(ms, "/app/thrum/pong", "itt", pong_handler, NULL);
  multicast_address = lo_address_new("224.0.1.1", "6010");
  lo_address_set_ttl(multicast_address, 1); /* set multicast scope to LAN */
  sleep(1);
  lo_server_thread_start(ms);

  while (1) {
    cycle();
    if (sync_next) {
      sync_next = 0;
      send_ping();
    }
    usleep(100000);
  }
  return(0);
}
