.DEFAULT_GOAL := debug

include config.mk

SRC = main.cpp

clean:
	rm -f shellter

shellter:
	${CPPC} ${WFLAGS} ${RELEASEFLAGS} ${SRC} -o shellter ${LIBS}

debug:
	${CPPC} ${WFLAGS} ${DEBUGFLAGS} ${SRC} -o shellter ${LIBS}

.PHONY: clean shellter debug
