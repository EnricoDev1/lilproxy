CC := gcc

all: proxy
CFLAGS := -ggdb -O2 -Wall -W -std=c99 -Iinclude -Ilibs -D_GNU_SOURCE
TARGET := proxy
SRC := src/main.c src/args.c src/net.c src/rules.c src/proxy.c src/commands.c src/epoll.c \
	   libs/linenoise.c

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS)
	
clean:
	rm -f proxy
