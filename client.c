//
// client.c - client for multi-threaded TCP Linux server
// (C) Copyright Paul Dardeau 2020
//

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "common.h"

static const char* server_ip = NULL;
static int port_number = 0;
static int num_requests = 0;
static int num_threads = 0;

//*****************************************************************************

int connect_to_server() {
   int fd = socket(AF_INET, SOCK_STREAM, 0);

   if (fd < 0) {
      fprintf(stderr, "error: unable to create socket\n");
      return -1;
   }

   struct sockaddr_in server_addr;
   memset((void *) &server_addr, 0, sizeof(server_addr));

   server_addr.sin_family = AF_INET;
   server_addr.sin_port = htons(port_number);
   server_addr.sin_addr.s_addr = inet_addr(server_ip);

   int rc = connect(fd,
                    (struct sockaddr *) &server_addr,
                    sizeof(server_addr));

   if (rc < 0) {
      fprintf(stderr, "error: unable to connect to server\n");
      close(fd);
      return -1;
   } else {
      return fd;
   }
}

//*****************************************************************************

bool send_request(int fd) {
   bool success = false;
   char request_type[LEN_MSG_REQUEST_BYTES+1];
   memset(request_type, 0, sizeof(request_type));
   strcpy(request_type, "SAMP");  // request type (SAMPLE)
   ssize_t total_bytes_sent = 0;
   bool sending_request = true;
   while (sending_request) {
      const char* req_send_from = request_type + total_bytes_sent;
      int bytes_to_send = LEN_MSG_REQUEST_BYTES - total_bytes_sent;
      ssize_t bytes_sent = send(fd, req_send_from, bytes_to_send, 0);
      if (bytes_sent > 0) {
         total_bytes_sent += bytes_sent;
         if (total_bytes_sent >= LEN_MSG_REQUEST_BYTES) {
            sending_request = false;
            success = true;
         }
      } else {
         if (-1 == bytes_sent) {
            sending_request = false;
            success = false;
         }
      }
   }

   return success;
}

//*****************************************************************************

void random_wait() {
   int limit_ms = 100;  // 0.1 seconds
   int divisor = RAND_MAX / limit_ms;
   int random_ms;

   do { 
      random_ms = rand() / divisor;
   } while (random_ms > limit_ms);

   usleep(random_ms * 1000);
}

//*****************************************************************************

void* run_client_thread(void* data) {
   int fd = connect_to_server();
   if (-1 == fd) {
      fprintf(stderr, "error: unable to connect\n");
      return NULL;
   }

   // run forever?
   if (-1 == num_requests) {
      // run forever
      for (;;) {
         if (!send_request(fd)) {
            close(fd);
            fprintf(stderr, "error: unable to send request\n");
            return NULL;
         }
         random_wait();
      }
   } else {
      // run a fixed number of requests
      for (int i = 0; i < num_requests; i++) {
         if (!send_request(fd)) {
            close(fd);
            fprintf(stderr, "error: unable to send request\n");
            return NULL;
         }
         random_wait();
      }
      close(fd);
   }

   return NULL;
}

//*****************************************************************************

bool run_socket_request_threads() {
   bool success = true;
   int thread_handle_bytes = num_threads * sizeof(pthread_t);
   pthread_t* thread_handles = malloc(thread_handle_bytes);
   memset(thread_handles, 0, thread_handle_bytes); 

   FILE* f = fopen("timed_run.txt", "w");
   if (NULL != f) {
      fprintf(f, "%lu\n", (unsigned long)time(NULL));
   }      

   for (int i = 0; i < num_threads; i++) {
      if (0 != pthread_create(&thread_handles[i],
                              0,
                              run_client_thread,
                              NULL)) {
         fprintf(stderr, "error: pthread_create failed\n");
         success = false;
         break;
      }
   }

   // wait for them to complete
   for (int i = 0; i < num_threads; i++) {
      if (0 != thread_handles[i]) {
         pthread_join(thread_handles[i], NULL);
      }
   }

   if (NULL != f) {
      fprintf(f, "%lu\n", (unsigned long)time(NULL));
      fclose(f);
   }

   free(thread_handles);

   return success;
}

//*****************************************************************************

int main(int argc, char* argv[]) {
   if (argc < 5) {
      fprintf(stderr, "error: missing required arguments\n");
      fprintf(stderr,
              "usage: %s server_ip port_number num_requests num_threads\n",
              argv[0]);
      exit(1);
   }

   int arg_index = 0;
   server_ip = argv[++arg_index];
   port_number = atoi(argv[++arg_index]);

   if (port_number < 1) {
      fprintf(stderr, "error: port number must be positive integer\n");
      exit(1);
   }

   num_requests = atoi(argv[++arg_index]);
   if (num_requests == 0) {
      fprintf(stderr, "error: num requests must be positive integer or -1\n");
      exit(1);
   }

   num_threads = atoi(argv[++arg_index]);
   if (num_threads < 1) {
      fprintf(stderr, "error: num threads must be positive integer\n");
      exit(1);
   }

   if (run_socket_request_threads()) {
      exit(0);
   } else {
      exit(1);
   }
}

