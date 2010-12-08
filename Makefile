CFLAGS	= -Wall -g -DDEBUG -D_FILE_OFFSET_BITS=64
LDFLAGS	= $(shell pkg-config fuse --libs)

targets	= tahoefs
objs	= tahoefs.o inet_stub.o http_stub.o

all: $(targets)

$(targets): $(objs)

clean:
	rm -f *.o *~
	rm -f $(targets)
