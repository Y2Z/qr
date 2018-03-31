# Makefile for qr

CC     = cc
CFLAGS = -std=c99 -s -pedantic -Wall -Wextra -Wfatal-errors -pedantic-errors -O3 -D_XOPEN_SOURCE=500 -D_POSIX_C_SOURCE=200809L
PROG   = qr
LIBS   = -lm -lqrencode

ifeq ($(PREFIX),)
    PREFIX := /usr
endif

$(PROG):
	@$(CC) qr.c $(CFLAGS) $(LIBS) -o $(PROG)

.PHONY: all clean install uninstall

all: $(PROG)

clean:
	@rm -f $(PROG)

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@install -d $(DESTDIR)$(PREFIX)/bin
	@install -m 755 $(PROG) ${DESTDIR}${PREFIX}/bin/$(PROG)

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/$(PROG)
