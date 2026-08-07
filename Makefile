CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude -std=c11
LDLIBS = -lsqlite3

SRCS = src/main.c src/cli.c src/stats.c src/storage.c src/sha256.c src/importer.c src/adapter_utils.c src/claude_importer.c src/antigravity_importer.c src/git_importer.c src/ui.c src/server.c
OBJS = src/main.o src/cli.o src/stats.o src/storage.o src/sha256.o src/importer.o src/adapter_utils.o src/claude_importer.o src/antigravity_importer.o src/git_importer.o src/ui.o src/server.o
TARGET = agentstat

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDLIBS)

src/main.o: src/main.c
	$(CC) $(CFLAGS) -c $< -o $@

src/cli.o: src/cli.c
	$(CC) $(CFLAGS) -c $< -o $@

src/stats.o: src/stats.c
	$(CC) $(CFLAGS) -c $< -o $@

src/storage.o: src/storage.c
	$(CC) $(CFLAGS) -c $< -o $@

src/sha256.o: src/sha256.c
	$(CC) $(CFLAGS) -c $< -o $@

src/importer.o: src/importer.c
	$(CC) $(CFLAGS) -c $< -o $@

src/adapter_utils.o: src/adapter_utils.c
	$(CC) $(CFLAGS) -c $< -o $@

src/claude_importer.o: src/claude_importer.c
	$(CC) $(CFLAGS) -c $< -o $@

src/antigravity_importer.o: src/antigravity_importer.c
	$(CC) $(CFLAGS) -c $< -o $@

src/git_importer.o: src/git_importer.c
	$(CC) $(CFLAGS) -c $< -o $@

src/ui.o: src/ui.c
	$(CC) $(CFLAGS) -c $< -o $@

src/server.o: src/server.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET)

seed: $(TARGET)
	./$(TARGET) seed

summary: $(TARGET)
	./$(TARGET) summary

list: $(TARGET)
	./$(TARGET) list

chart: $(TARGET)
	./$(TARGET) chart

web: $(TARGET)
	./$(TARGET) web --port 8080

.PHONY: all clean seed summary list chart web
