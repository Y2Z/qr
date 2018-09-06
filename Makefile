# Makefile for qr

CC     = cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Wfatal-errors -pedantic-errors -O3 -D_XOPEN_SOURCE=500 -D_POSIX_C_SOURCE=200809L
PROG   = qr
LIBS   = -lm -lqrencode

ifeq ($(PREFIX),)
    PREFIX := /usr/local
endif

$(PROG):
	@$(CC) qr.c $(CFLAGS) $(LIBS) -o $(PROG)

.PHONY: all clean install uninstall

all: $(PROG)

clean:
	@rm -f $(PROG)
	@rm -rf tests tests.dir tests.log

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@install -d $(DESTDIR)$(PREFIX)/bin
	@install -m 755 $(PROG) ${DESTDIR}${PREFIX}/bin/$(PROG)

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/$(PROG)

tests:
	@autom4te --language=autotest -o tests tests.at

test: $(PROG) tests
	@./tests INPUT="Ünic0d3wörd 参考文献に掲載されている文章等を抜粋し паучок-синячок"
