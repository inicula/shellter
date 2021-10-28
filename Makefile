.DEFAULT_GOAL := debug

include config.mk

SRC = main.cpp

clean:
	rm -f shellter

shellter:
	${CPPC} ${RELEASEFLAGS} ${SRC} -o shellter ${LIBS}

debug:
	${CPPC} ${DEBUGFLAGS} ${SRC} -o shellter ${LIBS}

.PHONY: clean shellter debug
