CFLAGS := -Wall -O2
OBJS := main.o p2s.o sock.o
INCLUDES := p2s.h sock.h
LIBS := -lpthread
TARGET := main
all: $(TARGET)
main: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)
main.o: $(INCLUDES)
ps2.o: p2s.c p2s.h
sock.o: sock.c sock.h
.PHONY: clean
clean:
	$(RM) $(TARGET) $(OBJS)
