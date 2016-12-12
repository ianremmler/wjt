# wjt - dynamic menu
# See LICENSE file for copyright and license details.

include config.mk

SRC = drw.c wjt.c util.c
OBJ = ${SRC:.c=.o}

all: options wjt

options:
	@echo wjt build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

config.h:
	@echo creating $@ from config.def.h
	@cp config.def.h $@

${OBJ}: config.h config.mk drw.h

wjt: wjt.o drw.o util.o
	@echo CC -o $@
	@${CC} -o $@ wjt.o drw.o util.o ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f wjt ${OBJ} wjt-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p wjt-${VERSION}
	@cp LICENSE Makefile README arg.h config.def.h config.mk wjt.1 \
		drw.h util.h ${SRC} \
		wjt-${VERSION}
	@tar -cf wjt-${VERSION}.tar wjt-${VERSION}
	@gzip wjt-${VERSION}.tar
	@rm -rf wjt-${VERSION}

install: all
	@echo installing executables to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f wjt ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/wjt
	@echo installing manual pages to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@sed "s/VERSION/${VERSION}/g" < wjt.1 > ${DESTDIR}${MANPREFIX}/man1/wjt.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/wjt.1

uninstall:
	@echo removing executables from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/wjt
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/wjt.1

.PHONY: all options clean dist install uninstall
