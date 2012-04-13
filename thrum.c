#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <lo/lo.h>
#include <sys/time.h>

#define VOTE_PERIOD 2

double vote_time = 0;
double assert_time = 0;

lo_address master_address = NULL;
lo_address multicast_address = NULL;
float master_id = -1;
lo_server ms;

float id;
double time_offset = 0;

int winning = 0;
int voting = 0;
int master = 0;



void assert_master() {
  int rv = lo_send_from(multicast_address, 
                        lo_server_thread_get_server(ms), 
                        LO_TT_IMMEDIATE,
                        "/master/assert", "f", id);
  if (rv == -1) {
    printf("multicast send error %d: %s\n", lo_address_errno(multicast_address), 
           lo_address_errstr(multicast_address)
           );
  }
}

void claim_master() {
  int rv = lo_send_from(multicast_address, 
                        lo_server_thread_get_server(ms), 
                        LO_TT_IMMEDIATE,
                        "/master/claim", "f", id);
  printf("claiming\n");
  if (rv == -1) {
    printf("multicast send error %d: %s\n", lo_address_errno(multicast_address), 
           lo_address_errstr(multicast_address)
           );
  }
}

void error(int num, const char *msg, const char *path) {
    printf("liblo server error %d in path %s: %s\n", num, path, msg);
}

double now() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return((double) tv.tv_sec + ((double) tv.tv_usec / 1000000.0) + time_offset);
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
  float remote_id = argv[0]->f;
  printf("got claim from %f\n", remote_id);

  if (! voting) {
    voting = 1;
    vote_time = now();
    if ( (remote_id > id)) {
      winning = 1;
      //claim_master();
    }
    else if (remote_id < id) {
      winning = 0;
    }
  }
  else {
    if (remote_id < id) {
      winning = 0;
    }
  }
  return(0);
}

int assert_handler(const char *path, const char *types, lo_arg **argv,
                 int argc, lo_message data, void *user_data) {
  voting = 0;
  master_id = argv[0]->f;
  printf("got assert from %f\n", master_id);
  master_address = lo_message_get_source(data);
  assert_time = now();
  if (master_id == id) {
    master = 1;
  }
  return(0);
}

void setid() {
  id = (float) rand() / RAND_MAX;
}

void initrand() {
  struct timeval time;
  gettimeofday(&time,NULL);
  srand((time.tv_sec * 1000) + (time.tv_usec / 1000) + getpid());
}

void cycle() {
  if (voting && ((now() - vote_time) > VOTE_PERIOD) && winning) {
    master = 1;
    master_id = id;
    master_address = NULL;
  }
  
  if (master_id<0 && !voting) {
    claim_master();
  }

  if (master) {
    assert_master();
  }
}

int main (int argc, char **argv) {
  initrand();
  setid();
  ms = lo_server_thread_new_multicast("224.0.1.1", "6010", error);
  //st = lo_server_thread_new(NULL, error);
  //lo_server_thread_start(st);
  lo_server_thread_add_method(ms, NULL, NULL, generic_handler, NULL);
  lo_server_thread_add_method(ms, "/master/claim", "f", claim_handler, NULL);
  lo_server_thread_add_method(ms, "/master/assert", "f", assert_handler, NULL);
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
