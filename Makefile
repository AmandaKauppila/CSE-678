# Makefile for client and server

CC = g++
FTPC = ftpc.cpp
FTPS = ftps.cpp
TCPDC = tcpdc.cpp
TCPDS = tcpds.cpp

LIB = -lsocket -lxnet

ALL = ftpc ftps tcpdc tcpds
# setup for stem
all: $(ALL)

server: ftps tcpds

client: ftpc tcpdc

ftpc:	$(FTPC)
	$(CC) -o $@ $(FTPC) $(LIB)

ftps:	$(FTPS)
	$(CC) -o $@ $(FTPS) $(LIB)

tcpdc:	$(TCPDC)
	$(CC) -o $@ $(TCPDC) $(LIB)

tcpds:	$(TCPDS)
	$(CC) -o $@ $(TCPDS) $(LIB)

clean:
	rm $(ALL)

redo:
	make clean
	make
