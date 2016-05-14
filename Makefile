CC?=	cc
CFLAGS?=	-O2 -g -pipe
SRCS=	indexinfo.c
OBJS=	${SRCS:.c=.o}
PREFIX?=	/usr/local
PACKAGE_NAME=	indexinfo
PACKAGE_VERSION=	0.2.5
CFLAGS+=	-DPACKAGE_NAME=\"${PACKAGE_NAME}\" \
		-DPACKAGE_VERSION=\"${PACKAGE_VERSION}\" \
		-D_BSD_SOURCE

all: indexinfo

indexinfo: ${OBJS}
	${CC} ${OBJS} ${LDFLAGS} -o $@

install: indexinfo
	install -m 755 indexinfo ${DESTDIR}${PREFIX}/bin

install-strip: indexinfo
	install -s -m 755 indexinfo ${DESTDIR}${PREFIX}/bin

.c.o:
	${CC} ${CFLAGS} -c $<

clean:
	rm -f ${OBJS} indexinfo
