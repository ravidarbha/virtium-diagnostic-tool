#	@(#)virt diag tool Makefile	8.1 (Berkeley) 6/5/93

PROG=	tool_main
MAN8=	route.8
SRCS=	tool_main.c tool.h

CFLAGS+=-I. -Wall -Werror -g -static

DPLIBS += \
	${LIBRTSOCK} \
	${LIBISC} \
	${LIBJUNIPER} \
	${LIBJUNOS-LOG-TRACE} \
	${LIBJUNOS-PRODUCT-INFO} 

CLEANFILES+=tool.h
BINOWN=	root
BINMODE=4555

beforedepend:	tool.h

.include <bsd.prog.mk>
