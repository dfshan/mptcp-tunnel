CFLAGS := -Wall -O1
OBJS := main.o p2s.o s2p.o sock.o
INCLUDES := p2s.h s2p.h sock.h
LIBS := -lpthread -lconfuse
TARGET := main
all: $(TARGET)
main: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)
main.o: $(INCLUDES)
p2s.o: p2s.c p2s.h
s2p.o: s2p.c s2p.h
sock.o: sock.c sock.h
.PHONY: clean
clean:
	$(RM) $(TARGET) $(OBJS)
