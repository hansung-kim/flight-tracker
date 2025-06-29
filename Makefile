CFLAGS?=-O2 -g -Wall -W $(shell pkg-config --cflags librtlsdr)
LDLIBS+=$(shell pkg-config --libs librtlsdr) -lpthread -lm -lstdc++ -lusb-1.0 -lcurl
CC?=gcc
CXX?=g++
PROGNAME=dump1090

all: dump1090

%.o: %.c
	$(CC) $(CFLAGS) -c $<

%.o: %.cpp
	$(CXX) $(CFLAGS) -c $<

dump1090: dump1090.o anet.o SystemStateMonitor.o
	$(CXX) -g -o dump1090 dump1090.o anet.o SystemStateMonitor.o $(LDFLAGS) $(LDLIBS)

clean:
	rm -f *.o dump1090
