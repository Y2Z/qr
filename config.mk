# program name
PROG = qr

# program version
VERSION = 2.0.0

#
# Customize below to fit your system
#

# paths
PREFIX = /usr/local

# libraries
LIBS = -lm -lqrencode

# flags
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Wfatal-errors -pedantic-errors -O3 -D_XOPEN_SOURCE=500 -D_POSIX_C_SOURCE=200809L

# compiler
CC = cc
