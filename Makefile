PROG = pmenu
OBJS = ${PROG:=.o} ctrlfnt.o
SRCS = ${OBJS:.o=.c}
MAN  = ${PROG:=.1}
PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man

_cflags = \
	-D_GNU_SOURCE -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 \
	-I./ -I/usr{,/local,/X11R6}/include{,/freetype2} \
	-std=c99 ${CPPFLAGS} ${CFLAGS}

_ldflags = \
	-lm -lfontconfig -lXft -lX11 -lXinerama -lXrender -lXext -lImlib2 \
	-L/usr{,/local,/X11R6}/lib ${LDFLAGS}

.PHONY: all
all: ${PROG}
${PROG}: ${OBJS}
	${CC} -o $@ ${OBJS} ${_ldflags}

.PHONY: debug
debug:
	@${MAKE} ${MAKEFLAGS} \
	CFLAGS+="-g -O0 -DDEBUG=1 -Wall -Wextra -Wpedantic ${DEBUG}" \
	${PROG}

pmenu.o: ctrlfnt.h
.c.o:
	${CC} ${_cflags} -o $@ -c $<

.PHONY: tags
tags: ${SRCS}
	ctags ${SRCS}

.PHONY: lint
lint: ${SRCS} ${MAN}
	-mandoc -T lint -W warning ${MAN}
	-clang-tidy -checks=clang-analyzer-* ${SRCS} -- ${_cflags}

.PHONY: clean
clean:
	-rm -f ${OBJS} ${PROG} ${PROG:=.core}

.PHONY: install
install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	install -m 755 ${PROG} ${DESTDIR}${PREFIX}/bin/${PROG}
	install -m 644 ${MAN} ${DESTDIR}${MANPREFIX}/man1/${MAN}

.PHONY: uninstall
uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/${PROG}
	rm -f ${DESTDIR}${MANPREFIX}/man1/${MAN}

