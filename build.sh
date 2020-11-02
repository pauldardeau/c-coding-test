#!/bin/sh
gcc -O3 -Wall server.c -o server -lpthread
gcc -O3 -Wall client.c -o client -lpthread
