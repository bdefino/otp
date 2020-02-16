CC := cc
CFLAGS := -std=c89 -Wall -Werror
NAME := otp
INSTALL_PATH := /usr/local/bin/$(NAME)
SOURCES := otp.c

install:
	$(CC) $(CFLAGS) -o $(INSTALL_PATH) $(SOURCES)
	chmod 700 $(INSTALL_PATH)

