CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -Iinclude -std=c11
LDLIBS ?= -lsqlite3 -lpthread

BIN_DIR = bin
BUILD_DIR = build
TARGET = $(BIN_DIR)/agentstat

SRCS = src/main.c src/cli.c src/stats.c src/storage.c src/sha256.c src/importer.c \
       src/adapter_utils.c src/claude_importer.c src/antigravity_importer.c \
       src/git_importer.c src/ui.c src/server.c

OBJS = $(patsubst src/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# Default build target
all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) -o $(TARGET) $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR) $(BUILD_DIR):
	@mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) agentstat src/*.o

# CLI shortcuts
sync: $(TARGET)
	@./$(TARGET) sync

summary: $(TARGET)
	@./$(TARGET) summary

list: $(TARGET)
	@./$(TARGET) list

chart: $(TARGET)
	@./$(TARGET) chart

usage: $(TARGET)
	@./$(TARGET) usage

code: $(TARGET)
	@./$(TARGET) code

attribution: $(TARGET)
	@./$(TARGET) attribution

web: $(TARGET)
	@./$(TARGET) web --port 8080

build-web:
	@cd web && npm install && npm run build

dev-web:
	@cd web && npm run dev

install: $(TARGET)
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/agentstat
	install -d $(DESTDIR)/usr/local/share/agentstat/web
	cp -r web/dist/* $(DESTDIR)/usr/local/share/agentstat/web/

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/agentstat
	rm -rf $(DESTDIR)/usr/local/share/agentstat

.PHONY: all clean sync summary list chart usage code attribution web build-web dev-web install uninstall
