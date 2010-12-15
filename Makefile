CFLAGS	= -Wall -g -DDEBUG
CFLAGS	+= $(shell pkg-config fuse --cflags)
CFLAGS	+= $(shell pkg-config json --cflags)
LDFLAGS	+= $(shell curl-config --cflags)
LDFLAGS	= $(shell pkg-config fuse --libs)
LDFLAGS	+= $(shell curl-config --libs)
LDFLAGS	+= $(shell pkg-config json --libs)

targets	= tahoefs
objs	= tahoefs.o http_stub.o json_stub.o filecache.o

all: $(targets)

$(targets): $(objs)

clean:
	rm -f *.o *~
	rm -f $(targets)
