# Makefile for qr

CC     = cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Wfatal-errors -pedantic-errors -O3 -D_XOPEN_SOURCE=500 -D_POSIX_C_SOURCE=200809L
PROG   = qr
LIBS   = -lm -lqrencode

FONT   ?= FreeMono

ifeq ($(PREFIX),)
    PREFIX := /usr/local
endif

$(PROG):
	@$(CC) qr.c $(CFLAGS) $(LIBS) -o $(PROG)

all: $(PROG)
.PHONY: all

clean:
	@rm -f $(PROG)
	@rm -rf tests tests.dir tests.log
.PHONY: clean

install: all
	@echo installing executable file to $(DESTDIR)$(PREFIX)/bin
	@install -d $(DESTDIR)$(PREFIX)/bin
	@install -m 755 $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)
.PHONY: install

uninstall:
	@echo removing executable file from $(DESTDIR)$(PREFIX)/bin
	@rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)
.PHONY: uninstall

test: $(PROG)
	@autom4te --language=autotest -o tests tests.at
	@./tests \
	  FONT=$(FONT) \
	  INPUT='Ünic0d3wörd 参 я' \
	  EXTRA_LONG_INPUT="参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し"
.PHONY: test
