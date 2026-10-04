CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -fPIC
LDFLAGS ?= -shared -ldl

LIB_TARGET = libmatepagerfix.so
SRC = mate_pager_fix.c

USER_DBUS_DIR = $(HOME)/.local/share/dbus-1/services
DBUS_SERVICE = org.mate.panel.applet.WnckletFactory.service
CONFIG_FILE = $(HOME)/.config/mate-pager.conf
CURRENT_DIR = $(shell pwd)

.PHONY: all clean install uninstall restart status

all: $(LIB_TARGET)

$(LIB_TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: $(LIB_TARGET)
	@echo "Installing D-Bus service override..."
	@mkdir -p $(USER_DBUS_DIR)
	@chmod +x $(CURRENT_DIR)/wnck-applet-wrapper $(CURRENT_DIR)/mate-pager-config
	@printf "[D-BUS Service]\nName=org.mate.panel.applet.WnckletFactory\nExec=%s/wnck-applet-wrapper\n" "$(CURRENT_DIR)" > "$(USER_DBUS_DIR)/$(DBUS_SERVICE)"
	@if [ ! -f $(CONFIG_FILE) ]; then \
		echo "Creating default configuration in $(CONFIG_FILE)..."; \
		mkdir -p $(HOME)/.config; \
		printf "# MATE Desktop Pager Configuration\nmin_width_per_workspace = 75\nmin_total_width = 300\n" > "$(CONFIG_FILE)"; \
	fi
	@echo "Reloading D-Bus session configuration..."
	@dbus-send --session --type=method_call --dest=org.freedesktop.DBus / org.freedesktop.DBus.ReloadConfig 2>/dev/null || true
	@echo "Restarting wnck-applet..."
	@killall -v wnck-applet 2>/dev/null || true
	@echo "Installation complete. Pager min width is now active!"

uninstall:
	@echo "Removing D-Bus service override..."
	@rm -f $(USER_DBUS_DIR)/$(DBUS_SERVICE)
	@echo "Reloading D-Bus session configuration..."
	@dbus-send --session --type=method_call --dest=org.freedesktop.DBus / org.freedesktop.DBus.ReloadConfig 2>/dev/null || true
	@echo "Restarting wnck-applet with default system settings..."
	@killall -v wnck-applet 2>/dev/null || true
	@echo "Uninstallation complete. Original system pager restored."

restart:
	@echo "Restarting wnck-applet..."
	@killall -v wnck-applet 2>/dev/null || true

status:
	@./mate-pager-config --get

clean:
	rm -f $(LIB_TARGET)
