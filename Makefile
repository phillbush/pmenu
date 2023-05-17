PROG = pmenu
OBJS = ${PROG:=.o}
SRCS = ${OBJS:.o=.c}
MAN  = ${PROG:=.1}

PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man
LOCALINC ?= /usr/local/include
LOCALLIB ?= /usr/local/lib
X11INC ?= /usr/X11R6/include
X11LIB ?= /usr/X11R6/lib
FREETYPEINC ?= /usr/include/freetype2

DEFS = -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -D_BSD_SOURCE -D_DEFAULT_SOURCE
INCS = -I${LOCALINC} -I${X11INC} -I${FREETYPEINC} -I${X11INC}/freetype2
LIBS = -L${LOCALLIB} -L${X11LIB} -lm -lfontconfig -lXft -lX11 -lXinerama -lXrender -lXext -lImlib2

all: ${PROG}

${PROG}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LIBS} ${LDFLAGS}

.c.o:
	${CC} -std=c99 -pedantic ${DEFS} ${INCS} ${CFLAGS} ${CPPFLAGS} -c $<

tags: ${SRCS}
	ctags ${SRCS}

clean:
	-rm -f ${OBJS} ${PROG} ${PROG:=.core}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	install -m 755 ${PROG} ${DESTDIR}${PREFIX}/bin/${PROG}
	install -m 644 ${MAN} ${DESTDIR}${MANPREFIX}/man1/${MAN}

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/${PROG}
	rm -f ${DESTDIR}${MANPREFIX}/man1/${MAN}

.PHONY: all clean install uninstall
