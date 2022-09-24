# paths
PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man
LOCALINC ?= /usr/local/include
LOCALLIB ?= /usr/local/lib
X11INC ?= /usr/X11R6/include
X11LIB ?= /usr/X11R6/lib
FREETYPEINC ?= /usr/include/freetype2

# includes and libs
INCS += -I${LOCALINC} -I${X11INC} -I${FREETYPEINC} -I${X11INC}/freetype2
LIBS += -L${LOCALLIB} -L${X11LIB} -lm -lfontconfig -lXft -lX11 -lXinerama -lXrender -lXext -lImlib2

# files
PROG = pmenu
SRCS = ${PROG}.c
OBJS = ${SRCS:.c=.o}

all: ${PROG}

${PROG}: ${OBJS}
	${CC} ${LDFLAGS} -o $@ ${OBJS} ${LIBS}

${OBJS}: config.h

.c.o:
	${CC} ${CFLAGS} ${CPPFLAGS} ${INCS} -c $<

clean:
	-rm -f ${OBJS} ${PROG} ${PROG:=.core}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	install -m 755 ${PROG} ${DESTDIR}${PREFIX}/bin/${PROG}
	install -m 644 ${PROG}.1 ${DESTDIR}${MANPREFIX}/man1/${PROG}.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/${PROG}
	rm -f ${DESTDIR}${MANPREFIX}/man1/${PROG}.1

.PHONY: all clean install uninstall
