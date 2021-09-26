CC = g++
CFLAGS=
TARGET = wg_proxy
SRCS := $(wildcard *.cpp) 
OBJS := $(patsubst %cpp, %o, $(SRCS)) 

all: $(TARGET) 

$(TARGET): $(OBJS) 
	$(CC) $(CFLAGS) -o $@ $^ -L/root/mbedtls-2.16.6/library -lpthread -lrt -lz -lm -luv

%.o:%.cpp
	$(CC) $(CFLAGS) -std=c++11 -DLINUX_PLATFORM -c $<

clean: 
	rm -f *.o $(TARGET)

