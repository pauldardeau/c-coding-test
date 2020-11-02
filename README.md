# C Coding Exam

Overview
========
While interviewing for a job that required C coding skills, I was given
this take-home programming exam that was to be completed over a 3-4 day period.

Exam Instructions (as provided by company I interviewed with)
=============================================================
The assignment is to implement a simple client/server application in C, then provide an abbreviated analysis on service capacity. 

This exercise will be graded as a whole, with primary attention given to your documented thought, technicality of the implementation, craft, and analysis.  Our preconceived notion of application correctness will be secondary to your definition--in other words, this document will frame some starting expectations, but it is left to the applicant to describe the direction they took the prompt.

Requirements:

1) Exist in userspace--please no kernel modules or drivers.

2) Consist of dependencies provided by the OS and libc.  Please don't use e.g., libuv or Boost.

3) Work on Linux (it is OK if it also works on Windows).

4) Please implement your solution in an ANSI C standard, such as C99 (or even -stdu=gnu99), but not in K&R or C++.


Design:

1) Server will bind to a TCP or UDP port and listen for client communications.

2) Client performs some kind of auth transaction with the server, such as providing a valid username.ada

3) Client may send multiple messages or transactions to the server.  The purpose of this exchange is unspecified, except that these transactions should mutate some global state such as an append-only log.

4) Server may accept multiple client connections.  Whether those connections are serviced in serial or parallel is unspecified.

Analysis:

1) Please provide a test that demonstrates your server's ability to handle 1000 concurrent clients.  The definition of "concurrent" is something we ask you to specify and justify.  Any analysis on transaction throughput is appreciated.


My Written Response (as provided with my coding solution)
=========================================================
My response consists of everything below (till bottom of page).

## High-Level Software Implementation
- Server software written in modern C, implemented in single source file (server.c)
- Client software written in modern C, implemented in single source file (client.c)
- Both server and client are multi-threaded with Posix threads (pthreads) library
- Server uses epoll mechanism to handle socket file descriptors
- Server's main thread accepts new client connections and then adds descriptor
(handoff) to one of the thread's epoll set of descriptors
- Each thread has its own (unique) set of file descriptors
- Server does simple round-robin distribution of new descriptors to worker threads
- Number of threads specified as command-line arguments (for both client and server)
- Server processing of each request modifies global state (increments total request counter)
- Socket type is TCP stream


## Load Test Configuration
- Server run with 8 worker threads (main thread for accepting new connections)
- 4 separate physical client nodes all connected by wire to switch
- Each client run with 250 threads, where each thread drives its own socket connection
- Each client configured to send 5000 requests per thread (total of 1,250,000 requests)
- Each client thread sleeps for random period of time between 0-100 milliseconds in between requests
- Grand total of 5 million requests are made (4 clients * 1,250,000)
- A multi-threaded Python script is used to orchestrate the clients (e.g., launch and
run them in parallel)
- Client and server executables run with non-root user account
- No system tweaking or adjustments were made


## Performance Data

Client     |    Run time (seconds) |  Connections |  Requests  |   Throughput Rate (req/s)
-------    |    ------------------ |  ----------- |  --------- |   -----------------------
Client #1  |    254                |  250         |  1,250,000 |   4,921
Client #2  |    254                |  250         |  1,250,000 |   4,921
Client #3  |    253                |  250         |  1,250,000 |   4,940
Client #4  |    256                |  250         |  1,250,000 |   4,882
---------  |    ------------------ |  ----------- |  --------- |   -----------------------
Aggregate  |    256                |  1,000       |  5,000,000 |   19,531


## Discussion
- Server successfully handled 1,000 concurrent TCP socket connections
- Of course, idle connections would only consume file descriptors
- Maxinum number of socket connections is hard-limited by server OS (depending on user's
resource constraints and global limits)
- Throughput (requests/second) is usually a much more interesting and valuable metric
- Server was able to run 19,531 requests per second for the 1,000 connected client sockets


## Software
- Server - Debian 10
- Client #1 - Debian 10
- Client #2 - Debian 10
- Client #3 - Debian 10
- Client #4 - Ubuntu 18.04.5


## Hardware
- Server - Lenovo ThinkPad x220, Intel i7-2640M 2.8GHz, 1GbE wired ethernet, 16GB RAM
- Client #1 - Lenovo ThinkPad x220, Intel i5-2520M 2.5GHz, 1GbE wired ethernet, 16GB RAM
- Client #2 - Lenovo ThinkPad x220, Intel i5-2540M 2.6GHz, 1GbE wired ethernet, 16GB RAM
- Client #3 - Lenovo ThinkPad x220, Intel i5-2520M 2.5GHz, 1GbE wired ethernet, 16GB RAM
- Client #4 - HP Pavilion, AMD E1-2500 1.4GHz, 100MbE wired ethernet, 4GB RAM
- Network switch - 8-port 1GbE Netgear
