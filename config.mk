# qr version
VERSION = 1.0.0

# program name
PROG = qr

# font used in tests
FONT = FreeMono

# Customize below to fit your system

# paths
PREFIX = /usr/local

# libs
LIBS = -lm -lqrencode

# flags
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Wfatal-errors -pedantic-errors -O3 -D_XOPEN_SOURCE=500 -D_POSIX_C_SOURCE=200809L

# compiler
CC = cc
