# Host-side wire-format test. Needs a C/C++ compiler and python3; no Arduino.
#
#     make run
#
# Compiles the library against the minimal Arduino.h/Print.h shim in shim/,
# encodes a set of messages with it, and checks the resulting bytes with an
# independent OSC 1.0 decoder (check.py, Python stdlib only).

LIB ?= ../..

CXXFLAGS = -std=c++11 -g -Wall -Wno-unused-variable -I shim -I $(LIB)
CFLAGS   = -g -I shim -I $(LIB)

SRC = $(LIB)/OSCData.cpp $(LIB)/OSCMessage.cpp $(LIB)/OSCBundle.cpp

emit: emit.cpp $(SRC) $(LIB)/OSCMatch.c
	$(CC) $(CFLAGS) -c -o oscmatch.o $(LIB)/OSCMatch.c
	$(CXX) $(CXXFLAGS) -o emit emit.cpp $(SRC) oscmatch.o

.PHONY: run clean
run: emit
	./emit | python3 check.py

clean:
	rm -rf emit emit.dSYM oscmatch.o
