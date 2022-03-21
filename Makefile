# program name
PROG = pmenu

# paths
PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man
LOCALINC ?= /usr/local/include
LOCALLIB ?= /usr/local/lib
X11INC ?= /usr/X11R6/include
X11LIB ?= /usr/X11R6/lib
FREETYPEINC ?= /usr/include/freetype2
# OpenBSD (uncomment)
#FREETYPEINC = ${X11INC}/freetype2

# includes and libs
INCS += -I${LOCALINC} -I${X11INC} -I${FREETYPEINC}
LIBS += -L${LOCALLIB} -L${X11LIB} -lm -lfontconfig -lXft -lX11 -lXinerama -lXrender -lXext -lImlib2

# flags
#DEBUG += -g -O0
CFLAGS += ${DEBUG} -Wall -Wextra ${INCS} ${CPPFLAGS}
LDFLAGS += ${LIBS}

# compiler and linker
CC ?= cc

SRCS = ${PROG}.c
OBJS = ${SRCS:.c=.o}

all: ${PROG}

${PROG}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LDFLAGS}

${OBJS}: config.h

.c.o:
	${CC} ${CFLAGS} -c $<

clean:
	-rm ${OBJS} ${PROG}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	install -m 755 ${PROG} ${DESTDIR}${PREFIX}/bin/${PROG}
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	install -m 644 ${PROG}.1 ${DESTDIR}${MANPREFIX}/man1/${PROG}.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/${PROG}
	rm -f ${DESTDIR}${MANPREFIX}/man1/${PROG}.1

.PHONY: all clean install uninstall
