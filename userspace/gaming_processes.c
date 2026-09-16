// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/magic.h>
#include <linux/types.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>

#include <bpf/bpf.h>

#define BPF_NO_KFUNC_PROTOTYPES
#include "gpu_sched_gaming.h"
#include "gaming_processes.h"

#define ENVIRONMENT_LIMIT (256U * 1024U)
#define RULES_LIMIT (64U * 1024U)
#define MAX_RULES 512U
#define ENVIRONMENT_CACHE_NS 2000000000ULL

enum process_class {
    CLASS_GAME, CLASS_DESKTOP, CLASS_INTERACTIVE, CLASS_COMPUTE,
    CLASS_BACKGROUND, CLASS_HIGH, CLASS_NORMAL, CLASS_LOW, CLASS_IGNORE,
    CLASS_NONE,
};

enum scheduling_mode {
    MODE_GAMING, MODE_WORKSTATION, MODE_LOW_LATENCY, MODE_AI, MODE_SERVER,
    MODE_AUTO,
};

enum rule_selector { SELECT_EXE, SELECT_COMM, SELECT_DIRECTORY };

struct process_rule {
    enum process_class classification;
    enum rule_selector selector;
    char *value;
};

struct tracked_process {
    __u32 identifier;
    struct gpu_gaming_process policy;
    enum process_class classification;
};

struct process_environment {
    __u64 application_id;
    bool wine_prefix;
    enum process_class marker;
};

struct environment_cache_entry {
    __u32 identifier;
    __u64 start_time_ticks;
    uint64_t refreshed_ns;
    char *executable;
    char command[256];
    struct process_environment environment;
};

struct gaming_process_tracker {
    uid_t owner;
    int map_descriptor;
    enum scheduling_mode mode;
    enum scheduling_mode effective_mode;
    bool reported_mode;
    uint64_t scan_time_ns;
    char *home_directory;
    char *environment_buffer;
    struct process_rule rules[MAX_RULES];
    size_t rule_count;
    struct tracked_process current[GPU_GAMING_MAX_PROCESSES];
    struct tracked_process discovered[GPU_GAMING_MAX_PROCESSES];
    size_t current_count;
    struct environment_cache_entry cache[GPU_GAMING_MAX_PROCESSES];
};

static const char *const class_names[] = {
    "game", "desktop", "interactive", "compute", "background",
    "high", "normal", "low", "ignore", "none",
};

static const char *const mode_names[] = {
    "gaming", "workstation", "low-latency", "ai", "server", "auto",
};

static enum process_class parse_class(const char *text)
{
    size_t index;

    for (index = 0; index < CLASS_NONE; index++) {
        if (!strcmp(text, class_names[index]))
            return (enum process_class)index;
    }
    return CLASS_NONE;
}

static int parse_unsigned(const char *text, size_t length, __u64 *output)
{
    __u64 value = 0;
    size_t index;

    if (!length)
        return -EINVAL;
    for (index = 0; index < length; index++) {
        unsigned int digit = (unsigned char)text[index] - '0';

        if (digit > 9 || value > (UINT64_MAX - digit) / 10)
            return -EINVAL;
        value = value * 10 + digit;
    }
    *output = value;
    return 0;
}

static int parse_start_time(const char *text, __u64 *output)
{
    const char *cursor = strrchr(text, ')');
    unsigned int field;

    /* The comm field can contain spaces and parentheses, so tokenization starts after it. */
    if (!cursor || cursor[1] != ' ')
        return -EINVAL;
    cursor += 2;
    for (field = 3; field <= 22; field++) {
        const char *start;

        while (*cursor == ' ')
            cursor++;
        start = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\n')
            cursor++;
        if (cursor == start)
            return -EINVAL;
        if (field == 22)
            return parse_unsigned(start, (size_t)(cursor - start), output);
    }
    return -EINVAL;
}

static struct process_environment parse_environment(const char *buffer, size_t length)
{
    struct process_environment result = { .marker = CLASS_NONE };
    __u64 steam_game_id = 0;
    size_t offset = 0;

    while (offset < length) {
        const char *entry = buffer + offset;
        const char *end = memchr(entry, '\0', length - offset);
        const char *value;
        __u64 identifier;

        /* Incomplete records never influence classification. */
        if (!end)
            break;
        value = memchr(entry, '=', (size_t)(end - entry));
        if (value) {
            size_t key_length = (size_t)(value++ - entry);
            size_t value_length = (size_t)(end - value);

            if (key_length == 10 && !memcmp(entry, "SteamAppId", 10) &&
                !parse_unsigned(value, value_length, &identifier) && identifier)
                result.application_id = identifier;
            else if (key_length == 11 && !memcmp(entry, "SteamGameId", 11) &&
                     !parse_unsigned(value, value_length, &identifier) && identifier)
                steam_game_id = identifier;
            else if (value_length &&
                     ((key_length == 10 && !memcmp(entry, "WINEPREFIX", 10)) ||
                      (key_length == 22 && !memcmp(entry, "STEAM_COMPAT_DATA_PATH", 22))))
                result.wine_prefix = true;
            else if (key_length == 15 && !memcmp(entry, "GPU_EXT_PROFILE", 15))
                result.marker = parse_class(value);
        }
        offset = (size_t)(end - buffer) + 1;
    }
    if (!result.application_id)
        result.application_id = steam_game_id;
    return result;
}

static int read_bounded(int directory, const char *name, char *buffer,
                        size_t capacity, size_t *length)
{
    size_t used = 0;
    struct stat status;
    int descriptor = openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    int result = 0;

    if (descriptor < 0)
        return -errno;
    if (fstat(descriptor, &status)) {
        result = -errno;
        close(descriptor);
        return result;
    }
    if (!S_ISREG(status.st_mode)) {
        close(descriptor);
        return -EINVAL;
    }
    while (used < capacity) {
        ssize_t count = read(descriptor, buffer + used, capacity - used);

        if (count < 0) {
            if (errno == EINTR)
                continue;
            result = -errno;
            break;
        }
        if (!count)
            break;
        used += (size_t)count;
    }
    if (!result && used == capacity) {
        char extra;
        ssize_t count;

        do {
            count = read(descriptor, &extra, 1);
        } while (count < 0 && errno == EINTR);
        if (count > 0)
            result = -E2BIG;
        else if (count < 0)
            result = -errno;
    }
    close(descriptor);
    if (result)
        return result;
    buffer[used] = '\0';
    *length = used;
    return 0;
}

static bool directory_contains(const char *directory, const char *path)
{
    size_t length = strlen(directory);

    while (length > 1 && directory[length - 1] == '/')
        length--;
    return !strncmp(directory, path, length) &&
           (length == 1 || path[length] == '/' || path[length] == '\0');
}

static bool name_in_list(const char *name, const char *const *names, size_t count)
{
    size_t index;

    for (index = 0; index < count; index++) {
        if (!strcasecmp(name, names[index]))
            return true;
    }
    return false;
}

#define NAME_IN_LIST(name, names) name_in_list(name, names, sizeof(names) / sizeof((names)[0]))

static bool wine_executable(const char *name)
{
    static const char *const names[] = {
        "wine", "wine64", "wine-preloader", "wine64-preloader",
    };

    return NAME_IN_LIST(name, names);
}

static bool windows_name(const char *name)
{
    size_t length = strlen(name);

    return length >= 4 && !strcasecmp(name + length - 4, ".exe");
}

static bool infrastructure_process(const char *name)
{
    static const char *const names[] = {
        "steam", "steamwebhelper", "steam-runtime-l", "steam-runtime-s",
        "pressure-vessel", "pv-bwrap", "bwrap", "proton", "proton-waitfore",
        "fossilize_replay", "fossilize_replay64", "shadercompilewo",
        "wineboot.exe", "winecfg.exe", "wineserver", "explorer.exe",
        "services.exe", "winedevice.exe", "rundll32.exe", "cmd.exe",
        "conhost.exe", "rpcss.exe", "plugplay.exe", "svchost.exe",
        "start.exe", "winedbg", "winedbg.exe", "steam.exe",
    };

    return NAME_IN_LIST(name, names);
}

static bool steam_game_path(const char *path)
{
    const char *component = strcasestr(path, "/steamapps/common/");
    const char *end;
    size_t length;

    if (!component)
        return false;
    component += strlen("/steamapps/common/");
    end = strchr(component, '/');
    if (!end)
        return false;
    length = (size_t)(end - component);
    if (!length || !strncasecmp(component, "Proton", 6) ||
        !strncasecmp(component, "SteamLinuxRuntime", 17) ||
        !strncasecmp(component, "Steamworks Shared", 17) ||
        !strncasecmp(component, "Steam Controller Configs", 24))
        return false;
    return true;
}

static bool home_game_path(const char *home_directory, const char *path)
{
    size_t length = strlen(home_directory);

    if (!directory_contains(home_directory, path))
        return false;
    while (length > 1 && home_directory[length - 1] == '/')
        length--;
    return !strncmp(path + length, "/Games/", 7) ||
           !strncmp(path + length, "/games/", 7);
}

static enum process_class classify_process(const struct gaming_process_tracker *tracker,
                                           const char *executable, const char *command,
                                           struct process_environment environment)
{
    static const char *const desktop_names[] = {
        "kwin_wayland", "kwin_x11", "gnome-shell", "sway", "Hyprland", "weston",
    };
    static const char *const interactive_names[] = {
        "firefox", "chrome", "chromium", "chromium-browse", "mpv", "vlc", "obs",
    };
    static const char *const compute_names[] = { "ollama", "llama-server", "llama-cli" };
    const char *basename = strrchr(executable, '/');
    size_t index;

    basename = basename ? basename + 1 : executable;
    for (index = 0; index < tracker->rule_count; index++) {
        const struct process_rule *rule = &tracker->rules[index];
        bool matches = rule->selector == SELECT_EXE ? !strcmp(rule->value, executable) :
                       rule->selector == SELECT_COMM ? !strcmp(rule->value, command) :
                       directory_contains(rule->value, executable);

        if (matches)
            return rule->classification;
    }
    if (environment.marker != CLASS_NONE)
        return environment.marker;
    if (infrastructure_process(command) || infrastructure_process(basename))
        return CLASS_NONE;
    if (NAME_IN_LIST(command, desktop_names))
        return CLASS_DESKTOP;
    if (NAME_IN_LIST(command, compute_names))
        return CLASS_COMPUTE;
    if (environment.application_id || steam_game_path(executable) ||
        home_game_path(tracker->home_directory, executable))
        return CLASS_GAME;
    if (wine_executable(basename) ||
        (windows_name(command) && (environment.wine_prefix || windows_name(basename))))
        return CLASS_INTERACTIVE;
    if (NAME_IN_LIST(command, interactive_names))
        return CLASS_INTERACTIVE;
    return CLASS_NONE;
}

static int parse_rule_line(char *line, struct process_rule *rule)
{
    char *classification;
    char *selector;
    char *cursor = line;
    char *end;

    while (isspace((unsigned char)*cursor))
        cursor++;
    if (!*cursor || *cursor == '#')
        return 0;
    classification = cursor;
    while (*cursor && !isspace((unsigned char)*cursor))
        cursor++;
    if (!*cursor)
        return -EINVAL;
    *cursor++ = '\0';
    while (isspace((unsigned char)*cursor))
        cursor++;
    selector = cursor;
    while (*cursor && !isspace((unsigned char)*cursor))
        cursor++;
    if (!*cursor)
        return -EINVAL;
    *cursor++ = '\0';
    while (isspace((unsigned char)*cursor))
        cursor++;
    end = cursor + strlen(cursor);
    while (end > cursor && isspace((unsigned char)end[-1]))
        *--end = '\0';
    rule->classification = parse_class(classification);
    if (rule->classification == CLASS_NONE || !*cursor)
        return -EINVAL;
    if (!strcmp(selector, "exe"))
        rule->selector = SELECT_EXE;
    else if (!strcmp(selector, "comm"))
        rule->selector = SELECT_COMM;
    else if (!strcmp(selector, "directory"))
        rule->selector = SELECT_DIRECTORY;
    else
        return -EINVAL;
    if (rule->selector != SELECT_COMM && *cursor != '/')
        return -EINVAL;
    if (rule->selector == SELECT_COMM && strlen(cursor) > 15)
        return -EINVAL;
    rule->value = strdup(cursor);
    return rule->value ? 1 : -ENOMEM;
}

static int load_rules(struct gaming_process_tracker *tracker, const char *path)
{
    char *buffer;
    char *cursor;
    size_t length;
    unsigned int line_number = 0;
    int result;

    if (!path)
        return 0;
    buffer = malloc(RULES_LIMIT + 1);
    if (!buffer)
        return -ENOMEM;
    result = read_bounded(AT_FDCWD, path, buffer, RULES_LIMIT, &length);
    if (result)
        goto cleanup;
    if (memchr(buffer, '\0', length)) {
        result = -EINVAL;
        goto cleanup;
    }
    cursor = buffer;
    while (cursor) {
        char *line = strsep(&cursor, "\n");
        struct process_rule rule = { 0 };

        line_number++;
        result = parse_rule_line(line, &rule);
        if (result < 0)
            break;
        if (!result)
            continue;
        if (tracker->rule_count == MAX_RULES) {
            free(rule.value);
            result = -E2BIG;
            break;
        }
        tracker->rules[tracker->rule_count++] = rule;
        result = 0;
    }
cleanup:
    if (result)
        fprintf(stderr, "gpu-ext: rules error at line %u: %s\n", line_number, strerror(-result));
    free(buffer);
    return result;
}

static int inspect_process(struct gaming_process_tracker *tracker, int directory,
                           struct tracked_process *process)
{
    char status_buffer[4096];
    char executable[PATH_MAX + 1];
    char command[256];
    struct process_environment environment;
    struct environment_cache_entry *cache =
        &tracker->cache[process->identifier % GPU_GAMING_MAX_PROCESSES];
    struct stat status;
    __u64 initial_start;
    __u64 final_start;
    size_t length;
    ssize_t link_length;
    int result;
    bool cached;

    if (fstat(directory, &status))
        return -errno;
    if (status.st_uid != tracker->owner)
        return 0;
    result = read_bounded(directory, "stat", status_buffer, sizeof(status_buffer) - 1, &length);
    if (result || (result = parse_start_time(status_buffer, &initial_start)))
        return result;
    link_length = readlinkat(directory, "exe", executable, PATH_MAX);
    if (link_length < 0)
        return -errno;
    if (link_length == PATH_MAX)
        return -ENAMETOOLONG;
    executable[link_length] = '\0';
    if (link_length >= 10 && !strcmp(executable + link_length - 10, " (deleted)"))
        executable[link_length - 10] = '\0';
    result = read_bounded(directory, "comm", command, sizeof(command) - 1, &length);
    if (result)
        return result;
    if (length && command[length - 1] == '\n')
        command[length - 1] = '\0';
    cached = cache->executable && cache->identifier == process->identifier &&
             cache->start_time_ticks == initial_start &&
             !strcmp(cache->executable, executable) && !strcmp(cache->command, command) &&
             tracker->scan_time_ns - cache->refreshed_ns < ENVIRONMENT_CACHE_NS;
    if (cached) {
        environment = cache->environment;
    } else {
        result = read_bounded(directory, "environ", tracker->environment_buffer,
                              ENVIRONMENT_LIMIT, &length);
        if (result)
            return result;
        environment = parse_environment(tracker->environment_buffer, length);
    }
    result = read_bounded(directory, "stat", status_buffer, sizeof(status_buffer) - 1, &length);
    if (result || (result = parse_start_time(status_buffer, &final_start)))
        return result;
    if (fstat(directory, &status))
        return -errno;
    if (initial_start != final_start || status.st_uid != tracker->owner)
        return 0;
    if (!cached) {
        char *saved_executable = strdup(executable);

        if (!saved_executable)
            return -ENOMEM;
        free(cache->executable);
        cache->executable = saved_executable;
        cache->identifier = process->identifier;
        cache->start_time_ticks = initial_start;
        cache->refreshed_ns = tracker->scan_time_ns;
        cache->environment = environment;
        strcpy(cache->command, command);
    }
    process->classification = classify_process(tracker, executable, command, environment);
    if (process->classification == CLASS_NONE || process->classification == CLASS_IGNORE)
        return 0;
    process->policy.start_time_ticks = initial_start;
    process->policy.application_id = environment.application_id;
    process->policy.flags = GPU_GAMING_PROCESS_INTERLEAVE;
    return 1;
}

static bool fatal_process_error(int result)
{
    return result == -ENOMEM || result == -EMFILE || result == -ENFILE || result == -EIO;
}

static int scan_processes(struct gaming_process_tracker *tracker, size_t *count)
{
    struct statfs filesystem;
    struct timespec scan_time;
    DIR *directory;
    int descriptor;
    int result = 0;

    *count = 0;
    if (clock_gettime(CLOCK_MONOTONIC, &scan_time))
        return -errno;
    tracker->scan_time_ns = (uint64_t)scan_time.tv_sec * 1000000000ULL +
                             (uint64_t)scan_time.tv_nsec;
    descriptor = open("/proc", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0)
        return -errno;
    if (fstatfs(descriptor, &filesystem)) {
        result = -errno;
        close(descriptor);
        return result;
    }
    if (filesystem.f_type != PROC_SUPER_MAGIC) {
        close(descriptor);
        return -ENODEV;
    }
    directory = fdopendir(descriptor);
    if (!directory) {
        result = -errno;
        close(descriptor);
        return result;
    }
    for (;;) {
        struct tracked_process process = { 0 };
        struct dirent *entry;
        __u64 identifier;
        int process_directory;

        errno = 0;
        entry = readdir(directory);
        if (!entry) {
            if (errno)
                result = -errno;
            break;
        }
        if (parse_unsigned(entry->d_name, strlen(entry->d_name), &identifier) ||
            !identifier || identifier > INT_MAX)
            continue;
        process.identifier = (__u32)identifier;
        process_directory = openat(descriptor, entry->d_name,
                                   O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (process_directory < 0) {
            result = -errno;
        } else {
            result = inspect_process(tracker, process_directory, &process);
            close(process_directory);
        }
        if (result < 0) {
            if (fatal_process_error(result))
                break;
            result = 0;
            continue;
        }
        if (!result)
            continue;
        if (*count == GPU_GAMING_MAX_PROCESSES) {
            result = -E2BIG;
            break;
        }
        tracker->discovered[(*count)++] = process;
        result = 0;
    }
    closedir(directory);
    return result;
}

static enum scheduling_mode effective_mode(enum scheduling_mode mode,
                                           const struct tracked_process *processes,
                                           size_t count)
{
    bool compute_found = false;
    size_t index;

    if (mode != MODE_AUTO)
        return mode;
    for (index = 0; index < count; index++) {
        if (processes[index].classification == CLASS_GAME)
            return MODE_GAMING;
        if (processes[index].classification == CLASS_COMPUTE)
            compute_found = true;
    }
    return compute_found ? MODE_AI : MODE_WORKSTATION;
}

static __u32 class_interleave(enum scheduling_mode mode, enum process_class classification)
{
    static const __u32 priorities[][5] = {
        { 2, 2, 1, 0, 0 }, { 1, 2, 2, 1, 0 }, { 2, 2, 2, 0, 0 },
        { 1, 2, 1, 2, 0 }, { 0, 1, 1, 2, 1 },
    };

    if (classification <= CLASS_BACKGROUND)
        return priorities[mode][classification];
    if (classification == CLASS_HIGH)
        return NV_INTERLEAVE_LEVEL_HIGH;
    if (classification == CLASS_NORMAL)
        return NV_INTERLEAVE_LEVEL_MEDIUM;
    return NV_INTERLEAVE_LEVEL_LOW;
}

static int compare_processes(const void *left, const void *right)
{
    const struct tracked_process *first = left;
    const struct tracked_process *second = right;

    return (first->identifier > second->identifier) - (first->identifier < second->identifier);
}

static const struct tracked_process *find_process(const struct tracked_process *processes,
                                                  size_t count, __u32 identifier)
{
    struct tracked_process key = { .identifier = identifier };

    return bsearch(&key, processes, count, sizeof(*processes), compare_processes);
}

static void report_process(const char *action, const struct tracked_process *process,
                           enum scheduling_mode mode)
{
    printf("gpu-ext: %s pid=%u appid=%" PRIu64 " class=%s mode=%s interleave=%u\n",
           action, process->identifier, (uint64_t)process->policy.application_id,
           class_names[process->classification], mode_names[mode], process->policy.interleave);
}

int gaming_process_tracker_create(uid_t owner, int map_descriptor,
                                  const char *rules_path, const char *mode,
                                  struct gaming_process_tracker **output)
{
    struct gaming_process_tracker *tracker;
    struct passwd *password;
    size_t index;
    int result;

    if (!output || !owner)
        return -EINVAL;
    *output = NULL;
    if (!mode)
        mode = "auto";
    for (index = 0; index <= MODE_AUTO; index++) {
        if (!strcmp(mode, mode_names[index]))
            break;
    }
    if (index > MODE_AUTO)
        return -EINVAL;
    errno = 0;
    password = getpwuid(owner);
    if (!password)
        return errno ? -errno : -ENOENT;
    if (!password->pw_dir || password->pw_dir[0] != '/' || !strcmp(password->pw_dir, "/"))
        return -EINVAL;
    tracker = calloc(1, sizeof(*tracker));
    if (!tracker)
        return -ENOMEM;
    tracker->owner = owner;
    tracker->map_descriptor = map_descriptor;
    tracker->mode = (enum scheduling_mode)index;
    tracker->home_directory = strdup(password->pw_dir);
    tracker->environment_buffer = malloc(ENVIRONMENT_LIMIT + 1);
    if (!tracker->home_directory || !tracker->environment_buffer) {
        gaming_process_tracker_destroy(tracker);
        return -ENOMEM;
    }
    result = load_rules(tracker, rules_path);
    if (result) {
        gaming_process_tracker_destroy(tracker);
        return result;
    }
    *output = tracker;
    return 0;
}

int gaming_process_tracker_refresh(struct gaming_process_tracker *tracker,
                                   unsigned int *active)
{
    enum scheduling_mode mode;
    size_t count;
    size_t index;
    int result;

    if (!tracker || tracker->map_descriptor < 0 || !active)
        return -EINVAL;
    result = scan_processes(tracker, &count);
    if (result)
        return result;
    qsort(tracker->discovered, count, sizeof(tracker->discovered[0]), compare_processes);
    mode = effective_mode(tracker->mode, tracker->discovered, count);
    for (index = 0; index < count; index++)
        tracker->discovered[index].policy.interleave =
            class_interleave(mode, tracker->discovered[index].classification);
    for (index = 0; index < tracker->current_count; index++) {
        const struct tracked_process *previous = &tracker->current[index];
        const struct tracked_process *next = find_process(tracker->discovered, count,
                                                          previous->identifier);

        if (next && next->policy.start_time_ticks == previous->policy.start_time_ticks)
            continue;
        if (bpf_map_delete_elem(tracker->map_descriptor, &previous->identifier) && errno != ENOENT)
            return -errno;
        report_process("removed", previous, tracker->effective_mode);
    }
    for (index = 0; index < count; index++) {
        const struct tracked_process *next = &tracker->discovered[index];
        const struct tracked_process *previous = find_process(tracker->current, tracker->current_count,
                                                              next->identifier);

        if (previous && !memcmp(&previous->policy, &next->policy, sizeof(next->policy)))
            continue;
        if (bpf_map_update_elem(tracker->map_descriptor, &next->identifier, &next->policy, BPF_ANY))
            return -errno;
        report_process(previous ? "updated" : "added", next, mode);
    }
    if (!tracker->reported_mode || tracker->effective_mode != mode) {
        printf("gpu-ext: effective-mode=%s requested-mode=%s\n", mode_names[mode],
               mode_names[tracker->mode]);
        tracker->reported_mode = true;
    }
    tracker->effective_mode = mode;
    memcpy(tracker->current, tracker->discovered, count * sizeof(tracker->current[0]));
    tracker->current_count = count;
    *active = (unsigned int)count;
    fflush(stdout);
    return 0;
}

void gaming_process_tracker_destroy(struct gaming_process_tracker *tracker)
{
    size_t index;

    if (!tracker)
        return;
    for (index = 0; index < tracker->rule_count; index++)
        free(tracker->rules[index].value);
    for (index = 0; index < GPU_GAMING_MAX_PROCESSES; index++)
        free(tracker->cache[index].executable);
    free(tracker->home_directory);
    free(tracker->environment_buffer);
    free(tracker);
}
