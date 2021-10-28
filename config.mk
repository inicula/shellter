#flags
CPPSTD = -std=c++20
WFLAGS = -Wall -Wextra -Wpedantic
DEBUGFLAGS = ${CPPSTD} ${WFLAGS} -g -Og -march=native -fno-rtti
RELEASEFLAGS = ${CPPSTD} ${WFLAGS} -Os -march=native -flto -fno-rtti -fno-exceptions

#libs
LIBS = -lfmt -lboost_regex -lreadline

#compiler
CPPC = g++
