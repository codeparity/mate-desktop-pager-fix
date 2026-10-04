# MATE Desktop Pager (Workspace Switcher) Size Fix

This project fixes the issue where the **Workspace Switcher (Desktop Pager)** in MATE Desktop is too small / narrow on the panel, and provides full control over its minimum and maximum width.

---

## 🔍 Why is the Pager Too Small by Default?

In MATE Desktop, the Workspace Switcher applet is provided by `wnck-applet` using `libwnck3`:
1. `libwnck3` computes thumbnail dimensions based on screen aspect ratio using a default fallback height (`workspace_size = 16`).
2. On a 1080p (16:9) screen, this results in each workspace column being only **~28 pixels wide**. For 4 workspaces, the entire switcher occupies just **115 pixels**, making workspaces and window thumbnails cramped and difficult to see or click.
3. MATE panel has no built-in GUI option or `dconf` setting to control pager width.
4. Standard GTK3 CSS (`min-width`) is ignored because `WnckPager` does not use GTK's CSS gadget sizing subsystem.

---

## 💡 How the Fix Works

This project provides a lightweight shared library (`libmatepagerfix.so`) and wrapper for `wnck-applet`:
- Hooks into `wnck_pager_new()`, `wnck_pager_set_n_rows()`, and `WnckScreen` workspace events.
- Dynamically sets the GTK size request (`gtk_widget_set_size_request`) on the pager widget based on your configuration.
- The MATE panel allocates the requested width, and `libwnck` automatically scales all workspaces, window icons, and click targets across the configured space.
- Includes a user-level D-Bus service override (`~/.local/share/dbus-1/services/org.mate.panel.applet.WnckletFactory.service`), ensuring the fix is active across system reboots and logins without modifying any system packages or requiring root privileges.

---

## 🚀 Quick Usage

### View Current Status and Size
```bash
./mate-pager-config --get
```

### Change Maximum Width
Set the overall pager maximum width (e.g. 200 pixels):
```bash
./mate-pager-config --max 200
```

### Change Minimum Total Width
Set the overall pager minimum width (e.g. 200 pixels):
```bash
./mate-pager-config --total 200
```

### Change Minimum Width Per Workspace Column
Set width allocated to each workspace (e.g. 50 pixels per workspace):
```bash
./mate-pager-config --workspace 50
```

### Reset to Default Settings
```bash
./mate-pager-config --reset
```

> **Note:** Changes made via `mate-pager-config` or directly to `~/.config/mate-pager.conf` are **picked up automatically within 2 seconds** without needing to restart the panel or log out!

---

## ⚙️ Configuration File: `~/.config/mate-pager.conf`

You can also edit `~/.config/mate-pager.conf` directly in any text editor:

```ini
# MATE Desktop Pager (Workspace Switcher) Size Configuration
# Changes to this file take effect automatically within 2 seconds.

# Minimum width (in pixels) for each workspace column.
min_width_per_workspace = 50

# Minimum total width (in pixels) for the entire pager applet.
min_total_width = 200

# Maximum width (in pixels) for the entire pager applet (0 to disable).
max_width = 200
```

---

## 🛠️ Build and Management

All commands can be run from this directory without `sudo`:

| Command | Description |
|---|---|
| `make` | Compile `libmatepagerfix.so` |
| `make install` | Install the user D-Bus override and apply the fix to the running panel |
| `make uninstall` | Remove the D-Bus override and restore default system pager behavior |
| `make restart` | Restart `wnck-applet` manually |
| `make status` | Display current pager configuration and active window sizes |
| `make clean` | Remove compiled shared library binary |

---

## 📁 Project Structure

- `mate_pager_fix.c`: C hook implementation with dynamic config watcher and libwnck event handlers.
- `libmatepagerfix.so`: Compiled shared library.
- `wnck-applet-wrapper`: Shell wrapper script that injects `libmatepagerfix.so` via `LD_PRELOAD`.
- `mate-pager-config`: CLI utility to view and adjust pager dimensions.
- `Makefile`: Build, install, and uninstall automation.
- `~/.config/mate-pager.conf`: User configuration file.
