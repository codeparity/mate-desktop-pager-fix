#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <dlfcn.h>

/* GTK & GLib function types */
typedef void (*GtkWidgetSetSizeRequestFunc)(void *widget, int width, int height);
typedef void (*GtkWidgetQueueResizeFunc)(void *widget);
typedef unsigned int (*GTimeoutAddSecondsFunc)(unsigned int interval, int (*func)(void *), void *data);
typedef unsigned long (*GSignalConnectDataFunc)(void *instance, const char *signal, void (*handler)(void), void *data, void (*destroy)(void *, void *), int flags);

/* libwnck function types */
typedef void *(*WnckPagerNewFunc)(void);
typedef void (*WnckPagerSetNRowsFunc)(void *pager, int n_rows);
typedef void (*WnckPagerSetOrientationFunc)(void *pager, int orientation);
typedef void (*WnckPagerSetShowAllFunc)(void *pager, int show_all);
typedef void *(*WnckScreenGetDefaultFunc)(void);
typedef int (*WnckScreenGetWorkspaceCountFunc)(void *screen);

/* Resolved function pointers */
static GtkWidgetSetSizeRequestFunc real_gtk_widget_set_size_request = NULL;
static GtkWidgetQueueResizeFunc real_gtk_widget_queue_resize = NULL;
static GTimeoutAddSecondsFunc real_g_timeout_add_seconds = NULL;
static GSignalConnectDataFunc real_g_signal_connect_data = NULL;

static WnckPagerNewFunc real_wnck_pager_new = NULL;
static WnckPagerSetNRowsFunc real_wnck_pager_set_n_rows = NULL;
static WnckPagerSetOrientationFunc real_wnck_pager_set_orientation = NULL;
static WnckPagerSetShowAllFunc real_wnck_pager_set_show_all = NULL;
static WnckScreenGetDefaultFunc real_wnck_screen_get_default = NULL;
static WnckScreenGetWorkspaceCountFunc real_wnck_screen_get_workspace_count = NULL;

/* Configuration settings */
typedef struct {
    int min_width_per_workspace;
    int min_total_width;
    int max_width;
    time_t last_mtime;
    char config_path[1024];
} PagerConfig;

static PagerConfig g_config = {
    .min_width_per_workspace = 50,
    .min_total_width = 200,
    .max_width = 200,
    .last_mtime = 0,
    .config_path = {0}
};

/* Runtime state */
static void *g_current_pager = NULL;
static int g_n_rows = 1;
static int g_orientation = 0; /* 0 = HORIZONTAL, 1 = VERTICAL */
static int g_show_all = 1;

static void init_symbols(void) {
    if (!real_gtk_widget_set_size_request)
        real_gtk_widget_set_size_request = (GtkWidgetSetSizeRequestFunc)dlsym(RTLD_DEFAULT, "gtk_widget_set_size_request");
    if (!real_gtk_widget_queue_resize)
        real_gtk_widget_queue_resize = (GtkWidgetQueueResizeFunc)dlsym(RTLD_DEFAULT, "gtk_widget_queue_resize");
    if (!real_g_timeout_add_seconds)
        real_g_timeout_add_seconds = (GTimeoutAddSecondsFunc)dlsym(RTLD_DEFAULT, "g_timeout_add_seconds");
    if (!real_g_signal_connect_data)
        real_g_signal_connect_data = (GSignalConnectDataFunc)dlsym(RTLD_DEFAULT, "g_signal_connect_data");

    if (!real_wnck_pager_new)
        real_wnck_pager_new = (WnckPagerNewFunc)dlsym(RTLD_NEXT, "wnck_pager_new");
    if (!real_wnck_pager_set_n_rows)
        real_wnck_pager_set_n_rows = (WnckPagerSetNRowsFunc)dlsym(RTLD_NEXT, "wnck_pager_set_n_rows");
    if (!real_wnck_pager_set_orientation)
        real_wnck_pager_set_orientation = (WnckPagerSetOrientationFunc)dlsym(RTLD_NEXT, "wnck_pager_set_orientation");
    if (!real_wnck_pager_set_show_all)
        real_wnck_pager_set_show_all = (WnckPagerSetShowAllFunc)dlsym(RTLD_NEXT, "wnck_pager_set_show_all");
    if (!real_wnck_screen_get_default)
        real_wnck_screen_get_default = (WnckScreenGetDefaultFunc)dlsym(RTLD_DEFAULT, "wnck_screen_get_default");
    if (!real_wnck_screen_get_workspace_count)
        real_wnck_screen_get_workspace_count = (WnckScreenGetWorkspaceCountFunc)dlsym(RTLD_DEFAULT, "wnck_screen_get_workspace_count");
}

static void determine_config_path(void) {
    if (g_config.config_path[0] != '\0') return;

    const char *custom = getenv("MATE_PAGER_CONFIG");
    if (custom && *custom) {
        snprintf(g_config.config_path, sizeof(g_config.config_path), "%s", custom);
        return;
    }

    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && *xdg_config) {
        snprintf(g_config.config_path, sizeof(g_config.config_path), "%s/mate-pager.conf", xdg_config);
        return;
    }

    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(g_config.config_path, sizeof(g_config.config_path), "%s/.config/mate-pager.conf", home);
        return;
    }

    snprintf(g_config.config_path, sizeof(g_config.config_path), "/tmp/mate-pager.conf");
}

static char *trim_whitespace(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static int reload_config_if_needed(void) {
    determine_config_path();

    struct stat st;
    int file_exists = (stat(g_config.config_path, &st) == 0);

    if (file_exists) {
        if (st.st_mtime == g_config.last_mtime) {
            return 0; /* unchanged */
        }
        g_config.last_mtime = st.st_mtime;

        FILE *fp = fopen(g_config.config_path, "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                char *trimmed = trim_whitespace(line);
                if (*trimmed == '#' || *trimmed == ';' || *trimmed == '\0')
                    continue;

                char *sep = strchr(trimmed, '=');
                if (!sep) sep = strchr(trimmed, ':');
                if (!sep) continue;

                *sep = '\0';
                char *key = trim_whitespace(trimmed);
                char *val = trim_whitespace(sep + 1);

                if (strcasecmp(key, "min_width_per_workspace") == 0) {
                    g_config.min_width_per_workspace = atoi(val);
                } else if (strcasecmp(key, "min_total_width") == 0 || strcasecmp(key, "min_width") == 0) {
                    g_config.min_total_width = atoi(val);
                } else if (strcasecmp(key, "max_width") == 0 || strcasecmp(key, "max_total_width") == 0) {
                    g_config.max_width = atoi(val);
                }
            }
            fclose(fp);
            fprintf(stderr, "[mate_pager_fix] Loaded config from %s: min_per_ws=%d, min_total=%d, max_width=%d\n",
                    g_config.config_path, g_config.min_width_per_workspace, g_config.min_total_width, g_config.max_width);
        }
    }

    /* Environment variables take precedence if explicitly specified */
    const char *env_per_ws = getenv("MATE_PAGER_MIN_WIDTH_PER_WORKSPACE");
    if (env_per_ws && *env_per_ws) {
        g_config.min_width_per_workspace = atoi(env_per_ws);
    }
    const char *env_total = getenv("MATE_PAGER_MIN_TOTAL_WIDTH");
    if (!env_total || !*env_total) env_total = getenv("MATE_PAGER_MIN_WIDTH");
    if (env_total && *env_total) {
        g_config.min_total_width = atoi(env_total);
    }
    const char *env_max = getenv("MATE_PAGER_MAX_WIDTH");
    if (env_max && *env_max) {
        g_config.max_width = atoi(env_max);
    }

    return 1;
}

static void update_pager_size(void *pager) {
    if (!pager) return;
    init_symbols();

    if (g_orientation == 1) {
        /* Vertical orientation: panel width determines width */
        if (real_gtk_widget_set_size_request)
            real_gtk_widget_set_size_request(pager, -1, -1);
        return;
    }

    int n_spaces = 4;
    if (real_wnck_screen_get_default && real_wnck_screen_get_workspace_count) {
        void *screen = real_wnck_screen_get_default();
        if (screen) {
            int cnt = real_wnck_screen_get_workspace_count(screen);
            if (cnt > 0) n_spaces = cnt;
        }
    }

    int spaces_per_row = 1;
    if (g_show_all) {
        int rows = g_n_rows > 0 ? g_n_rows : 1;
        spaces_per_row = (n_spaces + rows - 1) / rows;
    }

    int width_by_workspaces = spaces_per_row * g_config.min_width_per_workspace;
    int target_width = (width_by_workspaces > g_config.min_total_width) ? width_by_workspaces : g_config.min_total_width;

    /* Clamp to max_width if specified */
    if (g_config.max_width > 0 && target_width > g_config.max_width) {
        target_width = g_config.max_width;
    }

    if (target_width > 0 && target_width < 40)
        target_width = 40;

    fprintf(stderr, "[mate_pager_fix] Applying width %d (spaces_per_row=%d, min_per_ws=%d, min_total=%d, max_width=%d)\n",
            target_width, spaces_per_row, g_config.min_width_per_workspace, g_config.min_total_width, g_config.max_width);

    if (real_gtk_widget_set_size_request)
        real_gtk_widget_set_size_request(pager, target_width, -1);

    if (real_gtk_widget_queue_resize)
        real_gtk_widget_queue_resize(pager);
}

static void on_workspace_event(void *screen, void *space, void *user_data) {
    (void)screen;
    (void)space;
    update_pager_size(user_data);
}

static int config_check_timer_cb(void *user_data) {
    if (reload_config_if_needed()) {
        update_pager_size(user_data);
    }
    return 1; /* keep running */
}

/* Intercepted WnckPager APIs */

void *wnck_pager_new(void) {
    init_symbols();
    void *pager = real_wnck_pager_new ? real_wnck_pager_new() : NULL;
    if (!pager) return NULL;

    g_current_pager = pager;
    reload_config_if_needed();
    update_pager_size(pager);

    /* Hook into WnckScreen signals */
    if (real_wnck_screen_get_default && real_g_signal_connect_data) {
        void *screen = real_wnck_screen_get_default();
        if (screen) {
            real_g_signal_connect_data(screen, "workspace_created", (void (*)(void))on_workspace_event, pager, NULL, 0);
            real_g_signal_connect_data(screen, "workspace_destroyed", (void (*)(void))on_workspace_event, pager, NULL, 0);
        }
    }

    /* Periodic timer to check for changes in ~/.config/mate-pager.conf */
    if (real_g_timeout_add_seconds) {
        real_g_timeout_add_seconds(2, config_check_timer_cb, pager);
    }

    return pager;
}

void wnck_pager_set_n_rows(void *pager, int n_rows) {
    init_symbols();
    g_n_rows = n_rows;
    if (real_wnck_pager_set_n_rows)
        real_wnck_pager_set_n_rows(pager, n_rows);
    update_pager_size(pager);
}

void wnck_pager_set_orientation(void *pager, int orientation) {
    init_symbols();
    g_orientation = orientation;
    if (real_wnck_pager_set_orientation)
        real_wnck_pager_set_orientation(pager, orientation);
    update_pager_size(pager);
}

void wnck_pager_set_show_all(void *pager, int show_all) {
    init_symbols();
    g_show_all = show_all;
    if (real_wnck_pager_set_show_all)
        real_wnck_pager_set_show_all(pager, show_all);
    update_pager_size(pager);
}
