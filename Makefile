#variables
CC = gcc
CFLAGS = -Wall -Werror -pedantic -std=gnu18
LOGIN = tlesniak
SUBMITPATH = /home/cs537-1/handin/tlesniak/P3

#targets
all: wsh

wsh: wsh.c
	$(CC) $(CFLAGS) -o $@ $<

run: wsh
	./wsh

pack: 
	tar -cvzf $(LOGIN).tar.gz wsh.c Makefile README

submit: pack
	cp $(LOGIN).tar.gz $(SUBMITPATH)

.PHONY: all run pack submit