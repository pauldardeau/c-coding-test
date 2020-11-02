//
// server.c - multi-threaded TCP Linux server
// (C) Copyright Paul Dardeau 2020
//

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef __linux__
#include <sys/epoll.h>
#define EPOLL_SUPPORT 1
#else
#if defined(__APPLE__) || \
    defined(__FreeBSD__) || \
    defined(__OpenBSD__) || \
    defined(__NetBSD__)
#define KQUEUE_SUPPORT 1
#include <sys/event.h>
#endif
#endif

#include "common.h"

#define UNUSED_FD -1
#define POLL_WAIT_TIMEOUT_MS 500
#define MAX_THREADS 16

typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);

typedef struct _worker_thread_ctx {
#ifdef EPOLL_SUPPORT
   int epoll_fd;
   struct epoll_event* poll_events;
#elif defined(KQUEUE_SUPPORT)
   int kqueue_fd;
   struct kevent* poll_events;
#endif
   pthread_t thread_handle;
   int thread_index;
   unsigned long long request_count;
} worker_thread_ctx;

static int server_socket = UNUSED_FD;
static int max_connections = 0;
static bool server_running = false;
static worker_thread_ctx* thread_contexts = NULL;
static unsigned long long total_request_count = 0L;
static pthread_mutex_t mutex_global_state;
static bool global_state_mutex_created = false;

bool add_fd_for_read(worker_thread_ctx* thread_context, int fd);

//*****************************************************************************
#ifdef EPOLL_SUPPORT
bool init_epoll(int num_threads, int max_connections) {
   thread_contexts = calloc(num_threads, sizeof(worker_thread_ctx));

   for (int i = 0; i < num_threads; i++) {
      thread_contexts[i].epoll_fd = epoll_create1(0);
      
      if (thread_contexts[i].epoll_fd == -1) {
         fprintf(stderr, "epoll_create1 failed\n");
         return false;
      }
      
      thread_contexts[i].poll_events = calloc(max_connections,
                                              sizeof(struct epoll_event));
   }
      
   return true;
}

//*****************************************************************************

void cleanup_epoll(int num_threads) {
   if (NULL != thread_contexts) { 
      for (int i = 0; i < num_threads; i++) {
         if (0 != thread_contexts[i].epoll_fd) {
            close(thread_contexts[i].epoll_fd);
         }

         if (NULL != thread_contexts[i].poll_events) {
            free(thread_contexts[i].poll_events);
         }
      }
      free(thread_contexts);
   }
}
#endif
//*****************************************************************************
#ifdef KQUEUE_SUPPORT
bool init_kqueue(int num_threads, int max_connections) {
   thread_contexts = calloc(num_threads, sizeof(worker_thread_ctx));

   for (int i = 0; i < num_threads; i++) {
      thread_contexts[i].kqueue_fd = kqueue();
      if (thread_contexts[i].kqueue_fd == -1) {
         fprintf(stderr, "kqueue failed\n");
         return false;
      }
      thread_contexts[i].poll_events = calloc(max_connections,
                                              sizeof(struct kevent));
   }

   return true;
}

//*****************************************************************************

void cleanup_kqueue(int num_threads) {
   if (NULL != thread_contexts) {
      for (int i = 0; i < num_threads; i++) {
         if (0 != thread_contexts[i].kqueue_fd) {
            close(thread_contexts[i].kqueue_fd);
         }

         if (NULL != thread_contexts[i].poll_events) {
            free(thread_contexts[i].poll_events);
         }
      }
      free(thread_contexts);
   }
}
#endif
//*****************************************************************************
#ifdef EPOLL_SUPPORT
bool epoll_add_fd_for_read(worker_thread_ctx* thread_context, int fd) {
   struct epoll_event ev;
   memset(&ev, 0, sizeof(struct epoll_event));
   ev.events = EPOLLIN | EPOLLRDHUP;
   ev.data.fd = fd;
   
   if (epoll_ctl(thread_context->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
      fprintf(stderr, "epoll_ctl failed in add filter\n");
      if (errno == EBADF) {
         fprintf(stderr, "epoll_ctl EBADF, fd=%d\n", fd);
      } else if (errno == EEXIST) {
         fprintf(stderr, "epoll_ctl EEXIST, fd=%d\n", fd);
      } else if (errno == EINVAL) {
         fprintf(stderr, "epoll_ctl EINVAL, fd=%d\n", fd);
      } else if (errno == ENOMEM) {
         fprintf(stderr, "epoll_ctl ENOMEM, fd=%d\n", fd);
      } else if (errno == ENOSPC) {
         fprintf(stderr, "epoll_ctl ENOSPC, fd=%d\n", fd);
      } else if (errno == EPERM) {
         fprintf(stderr, "epoll_ctl EPERM, fd=%d\n", fd);
      } else {
         fprintf(stderr, "unrecognized error for EPOLL_CTL_ADD\n");
      }
      return false;
   } else {
      return true;
   }
}
#endif
//*****************************************************************************
#ifdef KQUEUE_SUPPORT
bool kqueue_add_fd_for_read(worker_thread_ctx* thread_context, int fd) {
   struct kevent ev;
   memset(&ev, 0, sizeof(struct kevent));
   EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
   
   if (kevent(thread_context->kqueue_fd, &ev, 1, NULL, 0, NULL) < 0) {
      fprintf(stderr, "kevent failed adding read filter");
      return false;
   } else {
      return true;
   }
}
#endif
//*****************************************************************************

bool add_fd_for_read(worker_thread_ctx* thread_context, int fd) {
#ifdef EPOLL_SUPPORT
   return epoll_add_fd_for_read(thread_context, fd);
#elif defined(KQUEUE_SUPPORT)
   return kqueue_add_fd_for_read(thread_context, fd);
#else
   return false;
#endif
}

//*****************************************************************************
#ifdef EPOLL_SUPPORT
bool epoll_remove_fd(worker_thread_ctx* thread_context, int fd) {
   struct epoll_event ev;
   memset(&ev, 0, sizeof(struct epoll_event));

   if (epoll_ctl(thread_context->epoll_fd, EPOLL_CTL_DEL, fd, &ev) < 0) {
      fprintf(stderr, "epoll_ctl failed in delete filter\n");
      if (errno == EBADF) {
         fprintf(stderr, "remove_fd EBADF, fd=%d\n", fd);
      } else if (errno == EEXIST) {
         fprintf(stderr, "remove_fd EEXIST, fd=%d\n", fd);
      } else if (errno == EINVAL) {
         fprintf(stderr, "remove_fd EINVAL, fd=%d\n", fd);
      } else if (errno == ENOENT) {
         fprintf(stderr, "remove_fd ENOENT, fd=%d\n", fd);
      } else if (errno == ENOMEM) {
         fprintf(stderr, "remove_fd ENOMEM, fd=%d\n", fd);
      } else if (errno == ENOSPC) {
         fprintf(stderr, "remove_fd ENOSPC, fd=%d\n", fd);
      } else if (errno == EPERM) {
         fprintf(stderr, "remove_fd EPERM, fd=%d\n", fd);
      } else {
         fprintf(stderr, "unrecognized error for EPOLL_CTL_DEL (remove_fd)\n");
      }
      return false;
   } else {
      return true;
   }
}
#endif
//*****************************************************************************
#ifdef KQUEUE_SUPPORT
bool kqueue_remove_fd(worker_thread_ctx* thread_context, int fd) {
   struct kevent ev;
   memset(&ev, 0, sizeof(struct kevent));
   EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
   
   if (kevent(thread_context->kqueue_fd, &ev, 1, NULL, 0, NULL) < 0) {
      fprintf(stderr, "kevent failed to delete read filter");
      return false;
   } else {
      return true;
   }
}
#endif
//*****************************************************************************

bool remove_fd(worker_thread_ctx* thread_context, int fd) {
#ifdef EPOLL_SUPPORT
   return epoll_remove_fd(thread_context, fd);
#elif defined(KQUEUE_SUPPORT)
   return kqueue_remove_fd(thread_context, fd);
#else
   return false;
#endif
}

//*****************************************************************************

bool increment_total_requests() {
   bool success;
   if (0 == pthread_mutex_lock(&mutex_global_state)) {
      success = true;
      total_request_count++;
      if (0 == (total_request_count % 1000)) {
         printf("%lld\n", total_request_count);
      }
      if (0 != pthread_mutex_unlock(&mutex_global_state)) {
         fprintf(stderr,
                 "error: failure to release lock on global state mutex\n");
      }
   } else {
      success = false;
      fprintf(stderr, "error: failure to obtain lock on global state mutex\n");
   }

   return success;
}

//*****************************************************************************

bool process_socket(int fd) {
   bool success = true;
   int flags = MSG_DONTWAIT;
   ssize_t total_bytes_received = 0;
   bool reading_request = true;
   bool have_request = false;
   char receive_buffer[LEN_MSG_REQUEST_BYTES+1];
   int buffer_len = LEN_MSG_REQUEST_BYTES;
   memset(receive_buffer, 0, sizeof(receive_buffer));

   while (reading_request) {
      char* buffer_read_dest = receive_buffer + total_bytes_received;
      buffer_len -= total_bytes_received;
      ssize_t bytes_received =
         recv(fd, buffer_read_dest, buffer_len, flags);
      if (bytes_received > 0) {
         total_bytes_received += bytes_received;
         if (total_bytes_received >= LEN_MSG_REQUEST_BYTES) {
            reading_request = false;
            have_request = true;
         }
      } else {
         success = false;
         reading_request = false;
      }
   }

   if (have_request) {
      increment_total_requests();
   }

   return success;
}

//*****************************************************************************
#ifdef EPOLL_SUPPORT
void* epoll_socket_servicing_thread(void* data) {
   worker_thread_ctx* thread_context = (worker_thread_ctx*) data;

   while (server_running) {
      int num_events = epoll_wait(thread_context->epoll_fd,
                                  thread_context->poll_events,
                                  max_connections,
                                  POLL_WAIT_TIMEOUT_MS);
      if (num_events > 0) {
         for (int index = 0; index < num_events; index++) {
            struct epoll_event current_event;
            current_event = thread_context->poll_events[index];

            int fd = current_event.data.fd;
            if (fd == 0) {
               continue;
            }

            if (EBADF == fcntl(fd, F_GETFD)) {
               remove_fd(thread_context, fd);
               continue;
            }

            // read/close?
            if (current_event.events & EPOLLRDHUP) {
               process_socket(fd);
               if (!remove_fd(thread_context, fd)) {
                  fprintf(stderr, "error removing fd (EPOLLRDHUP)\n");
               }
               close(fd);
            } else if (current_event.events & EPOLLHUP) {
               // disconnect
               if (!remove_fd(thread_context, fd)) {
                  fprintf(stderr, "error removing fd (EPOLLHUP)\n");
               }
               close(fd);
            } else if (current_event.events & EPOLLIN) {
               // read
               if (!process_socket(fd)) {
                  if (!remove_fd(thread_context, fd)) {
                     fprintf(stderr, "error removing fd (EPOLLIN)\n");
                  }
               }
            }

            if (!server_running) {
               break;
            }
         }
      }
   }

   return NULL;
}
#endif
//*****************************************************************************
#ifdef KQUEUE_SUPPORT
void* kqueue_socket_servicing_thread(void* data) {
   worker_thread_ctx* thread_context = (worker_thread_ctx*) data;
   struct timespec tmout = { 0, POLL_WAIT_TIMEOUT_MS * 1000000};

   while (server_running) {
      int num_events_returned =
         kevent(thread_context->kqueue_fd,
                NULL, 0,
                thread_context->poll_events, max_connections,
                &tmout);
      //printf("[%d] %d events\n", thread_context->thread_index, num_events_returned);
      if (-1 == num_events_returned) {
         fprintf(stderr, "unable to retrieve events from kevent");
         break;
      }

      if (!server_running) {
         break;
      }

      for (int i = 0; i < num_events_returned; i++) {
         struct kevent current_event = thread_context->poll_events[i];
         int fd = current_event.ident;
         if (fd == 0) {
            continue;
         }

         if (EBADF == fcntl(fd, F_GETFD)) {
            remove_fd(thread_context, fd);
            continue;
         }

         if (current_event.flags & EV_EOF) {
            // disconnect
            if (!remove_fd(thread_context, fd)) {
               fprintf(stderr, "error removing fd (EV_EOF)\n");
            }
            close(fd);
         } else if (current_event.filter == EVFILT_READ) {
            // read
            if (!process_socket(fd)) {
               if (!remove_fd(thread_context, fd)) {
                  fprintf(stderr, "error removing fd (EVFILT_READ)\n");
               }
            }
         }

         if (!server_running) {
            break;
         }
      }
   }

   return NULL;
}
#endif
//*****************************************************************************

void* socket_servicing_thread(void* data) {
#ifdef EPOLL_SUPPORT
   return epoll_socket_servicing_thread(data);
#elif defined(KQUEUE_SUPPORT)
   return kqueue_socket_servicing_thread(data);
#else
   return NULL;
#endif
}

//*****************************************************************************

bool start_socket_servicing_threads(int num_threads) {
   for (int i = 0; i < num_threads; i++) {
      thread_contexts[i].thread_index = i;
      if (0 != pthread_create(&thread_contexts[i].thread_handle,
                              0,
                              socket_servicing_thread,
                              (void*)&thread_contexts[i])) {
         fprintf(stderr, "error: pthread_create failed\n");
         return false;
      }
   }

   return true;
}

//*****************************************************************************

void sig_handler(int sig_num) {
   printf("interrupt received. shutting down\n");
   server_running = false;
   shutdown(server_socket, SHUT_RDWR);
   close(server_socket);
   server_socket = UNUSED_FD;
}

//*****************************************************************************

bool init(int num_threads, int max_connections) {
#ifdef EPOLL_SUPPORT
   // initialize for epoll
   if (!init_epoll(num_threads, max_connections)) {
      fprintf(stderr, "error: unable to initialize epoll\n");
      return false;
   }
#elif defined(KQUEUE_SUPPORT)
   // initialize for kqueue
   if (!init_kqueue(num_threads, max_connections)) {
      fprintf(stderr, "error: unable to initialize kqueue\n");
      return false;
   }
#else
   return false;
#endif

   return true;
}

//*****************************************************************************

void run_cleanup(int num_threads) {
#ifdef EPOLL_SUPPORT
   cleanup_epoll(num_threads);
#elif defined(KQUEUE_SUPPORT)
   cleanup_kqueue(num_threads);
#endif
}

//*****************************************************************************

void run_server(int num_threads,
                int port_number,
                int backlog,
                int max_connections) {
   // create server socket
   server_socket = socket(AF_INET, SOCK_STREAM, 0);

   // set reuse addr option
   int val_to_set = 1;
   if (setsockopt(server_socket,
                  SOL_SOCKET,
                  SO_REUSEADDR,
                  (char *) &val_to_set,
                  sizeof(val_to_set)) != 0) {
      fprintf(stderr, "error: unable to set SO_REUSEADDR on server socket\n");
      goto cleanup;
   }

   // bind
   struct sockaddr_in server_addr;
   memset(&server_addr, 0, sizeof(server_addr));
   server_addr.sin_family = AF_INET;
   server_addr.sin_port = htons(port_number);
   server_addr.sin_addr.s_addr = INADDR_ANY;

   if (bind(server_socket,
            (struct sockaddr*) &server_addr,
            sizeof(server_addr)) < 0) {
      fprintf(stderr,
              "error: unable to bind server socket to port %d\n",
              port_number);
      goto cleanup;
   }

   // listen
   if (listen(server_socket, backlog) != 0) {
      fprintf(stderr, "error: unable to listen on server socket\n");
      goto cleanup;
   }

   // initialize for epoll/kqueue
   if (!init(num_threads, max_connections)) {
      fprintf(stderr, "error: unable to initialize\n");
      goto cleanup;
   }

   // create mutex for global state
   pthread_mutexattr_t attr;
   if (0 == pthread_mutexattr_init(&attr)) {
      if (0 == pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK)) {
         int rc = pthread_mutex_init(&mutex_global_state, &attr);
         pthread_mutexattr_destroy(&attr);
         if (0 == rc) {
            global_state_mutex_created = true;
         } else {
            fprintf(stderr, "error: pthread_mutex_init failed\n");
         }
      } else {
         fprintf(stderr, "error: pthread_mutexattr_settype failed\n");
      }
   } else {
      fprintf(stderr, "error: pthread_mutexattr_init failed\n");
   }

   if (!global_state_mutex_created) {
      fprintf(stderr, "error: unable to create mutex for global state\n");
      goto cleanup;
   }

   server_running = true;

   // start background thread to service the sockets
   if (!start_socket_servicing_threads(num_threads)) {
      fprintf(stderr, "error: unable to start worker threads\n");
      goto cleanup;
   }

   printf("listening for connections on port %d (threads=%d, maxconn=%d)\n",
          port_number,
          num_threads,
          max_connections);

   int thread_index = 0;
   unsigned int name_len;
   struct sockaddr_in client_addr;

   while (server_running) {
      int client_fd = accept(server_socket,
                             (struct sockaddr*) &client_addr,
                             &name_len);
      if (client_fd < 0) {
         continue;
      }

      // give to a worker thread in round robin manner
      if (!add_fd_for_read(&thread_contexts[thread_index++], client_fd)) {
         // if we can't track fd with epoll, we're not able to service it
         close(client_fd);
      }

      // did we reach the last thread?
      if (thread_index == num_threads) {
         // move back to the first thread
         thread_index = 0;
      }
   }

   // wait for the worker threads to complete
   for (int i = 0; i < num_threads; i++) {
      if (0 != thread_contexts[i].thread_handle) {
         pthread_join(thread_contexts[i].thread_handle, NULL);
      } else {
         printf("thread_handle is 0\n");
      }
   }

cleanup:
   if (global_state_mutex_created) {
      pthread_mutex_destroy(&mutex_global_state);
   }
   run_cleanup(num_threads);
   if (server_socket != UNUSED_FD) {
      shutdown(server_socket, SHUT_RDWR); 
      close(server_socket);
   }
}

//*****************************************************************************

int main(int argc, char* argv[]) {
   if (argc < 4) {
      fprintf(stderr, "error: missing required arguments\n");
      fprintf(stderr, "usage: %s port_number num_threads max_connections\n", argv[0]);
      exit(1);
   }

   int backlog = 500;
   int arg_index = 0;

   const int port_number = atoi(argv[++arg_index]);

   if (port_number < 1) {
      fprintf(stderr, "error: port number must be positive integer > 1024\n");
      exit(1);
   }

   if (port_number < 1025) {
      fprintf(stderr, "error: use port number above 1024 (unprivileged)\n");
      exit(1);
   }

   const int num_threads = atoi(argv[++arg_index]);

   if (num_threads < 1) {
      fprintf(stderr, "error: num threads must be positive integer <= %d\n",
                      MAX_THREADS);   
      exit(1);
   }

   if (num_threads > MAX_THREADS) {
      fprintf(stderr, "error: num threads cannot exceed %d\n", MAX_THREADS);
      exit(1);
   }

   max_connections = atoi(argv[++arg_index]);
   if (max_connections < 1) {
      fprintf(stderr, "error: max connections must be positive integer\n");
      exit(1);
   }

   signal(SIGINT, sig_handler);

   run_server(num_threads, port_number, backlog, max_connections);
}

