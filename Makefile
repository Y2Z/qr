# Makefile for qr
# See LICENSE file for copyright and license details.

include config.mk

$(PROG): $(PROG).c config.mk
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVERSION="\"$(VERSION)\"" $(PROG).c $(LDFLAGS) $(LIBS) -o $(PROG)

all: $(PROG)
.PHONY: all

clean:
	@rm -f $(PROG) $(PROG).exe
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

# Font and ImageMagick command used by tests (FONT may be a name or a file path)
FONT ?= FreeMono
CONVERT ?= $$(command -v magick || echo convert)

test: $(PROG)
	@autom4te --no-cache -f -l autotest -o tests tests.at
	@./tests \
        FONT='$(FONT)' \
        CONVERT="$(CONVERT)" \
        VERSION='$(VERSION)' \
        INPUT='Ünic0d3wörd 参 я' \
        EXTRA_LONG_INPUT="参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し参考文献に掲載されている文章等を抜粋し"
.PHONY: test
