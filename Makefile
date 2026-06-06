CC=gcc
PKG_CONFIG_CFLAGS=$(shell pkg-config --cflags gtk+-3.0 json-glib-1.0)
PKG_CONFIG_LIBS=$(shell pkg-config --libs gtk+-3.0 json-glib-1.0)

CFLAG_DEBUG = -DDEBUG_MESSAGES
CFLAGS=-Wall -g $(PKG_CONFIG_CFLAGS) -Iinclude -Iinclude/cpu -Iinclude/memory -Iinclude/disk -Iinclude/ui -Iinclude/gpu
LIBS=$(PKG_CONFIG_LIBS)

SRC_DIR=src
CPU_DIR=$(SRC_DIR)/cpu
MEMORY_DIR=$(SRC_DIR)/memory
DISK_DIR=$(SRC_DIR)/disk
GPU_DIR=$(SRC_DIR)/gpu
UI_DIR=$(SRC_DIR)/ui

SRCS=$(SRC_DIR)/main.c \
     $(CPU_DIR)/cpu_data.c \
     $(UI_DIR)/ui_cpu.c \
     $(MEMORY_DIR)/memory_data.c \
     $(UI_DIR)/ui_memory.c \
     $(DISK_DIR)/disk_data.c \
     $(UI_DIR)/ui_disk.c \
     $(GPU_DIR)/gpu_data.c \
     $(UI_DIR)/ui_gpu.c \
     $(UI_DIR)/ui_about.c \
     $(UI_DIR)/graph_utils.c \
     $(UI_DIR)/ui_app.c \
     $(SRC_DIR)/utils/icon_cache.c \
     $(SRC_DIR)/utils/hotkey.c \
     $(SRC_DIR)/network/network_data.c \
     $(UI_DIR)/ui_network.c

OBJS=$(SRCS:.c=.o)
TARGET=khos-system-monitor

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) *.o

PREFIX ?= /usr/local
DESTDIR ?=

# Detect real user home even when running under sudo
REAL_HOME := $(if $(SUDO_USER),$(shell getent passwd $(SUDO_USER) | cut -d: -f6),$(HOME))

install: $(TARGET)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	# Install desktop entry (system) – filename must match Wayland app_id
	mkdir -p $(DESTDIR)$(PREFIX)/share/applications
	cp khos-system-monitor.desktop $(DESTDIR)$(PREFIX)/share/applications/org.gtk.systemmonitor.desktop
	ln -sf org.gtk.systemmonitor.desktop $(DESTDIR)$(PREFIX)/share/applications/khos-system-monitor.desktop
	# Install desktop entry (user local for DE matching)
	mkdir -p $(REAL_HOME)/.local/share/applications
	cp khos-system-monitor.desktop $(REAL_HOME)/.local/share/applications/org.gtk.systemmonitor.desktop
	ln -sf org.gtk.systemmonitor.desktop $(REAL_HOME)/.local/share/applications/khos-system-monitor.desktop
	# Install icon (scalable svg)
	mkdir -p $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps
	cp khos-sm-logo.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/khos-system-monitor.svg
	# Update desktop databases
	update-desktop-database $(DESTDIR)$(PREFIX)/share/applications 2>/dev/null || true
	update-desktop-database $(REAL_HOME)/.local/share/applications 2>/dev/null || true
	# Update icon cache so desktop environment can find the new icon
	@if [ -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/index.theme ]; then \
	  gtk-update-icon-cache --force --quiet $(DESTDIR)$(PREFIX)/share/icons/hicolor ; \
	else \
	  gtk-update-icon-cache --force --quiet /usr/share/icons/hicolor || true ; \
	fi

.PHONY: uninstall
uninstall:
	# Remove installed binary
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	# Remove desktop entry (system)
	rm -f $(DESTDIR)$(PREFIX)/share/applications/org.gtk.systemmonitor.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/applications/khos-system-monitor.desktop
	# Remove desktop entry (user local)
	rm -f $(REAL_HOME)/.local/share/applications/org.gtk.systemmonitor.desktop
	rm -f $(REAL_HOME)/.local/share/applications/khos-system-monitor.desktop
	# Remove installed icon
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/khos-system-monitor.svg
	# Refresh icon cache
	@if [ -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/index.theme ]; then \
	  gtk-update-icon-cache --force --quiet $(DESTDIR)$(PREFIX)/share/icons/hicolor ; \
	else \
	  gtk-update-icon-cache --force --quiet /usr/share/icons/hicolor || true ; \
	fi

.PHONY: install uninstall 