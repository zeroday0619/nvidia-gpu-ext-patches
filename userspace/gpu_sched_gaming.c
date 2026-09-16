// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/magic.h>
#include <linux/types.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#define BPF_NO_KFUNC_PROTOTYPES
#include "gpu_sched_gaming.h"
#include "gaming_processes.h"
#include "gpu_sched_gaming.skel.h"

#define CGROUP_ROOT "/sys/fs/cgroup"
#define MAX_TIMESLICE_US 1000000ULL

static volatile sig_atomic_t exiting;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    exiting = 1;
}

static int print_libbpf(enum libbpf_print_level level, const char *format,
                       va_list arguments)
{
    if (level == LIBBPF_DEBUG)
        return 0;
    return vfprintf(stderr, format, arguments);
}

static int notify_ready(const char *mode, bool observe_only)
{
    const char *socket_path = getenv("NOTIFY_SOCKET");
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    char message[256];
    socklen_t address_length;
    size_t path_length;
    ssize_t sent;
    int message_length;
    int descriptor;
    int result = 0;

    if (!socket_path)
        return 0;
    path_length = strlen(socket_path);
    if (path_length < 2 ||
        (socket_path[0] != '/' && socket_path[0] != '@') ||
        path_length > sizeof(address.sun_path) ||
        (socket_path[0] == '/' && path_length == sizeof(address.sun_path)))
        return -EINVAL;
    memcpy(address.sun_path, socket_path, path_length);
    if (socket_path[0] == '@') {
        address.sun_path[0] = '\0';
        address_length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_length);
    } else {
        address_length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_length + 1);
    }
    message_length = snprintf(message, sizeof(message),
                              "READY=1\nSTATUS=Attached GPU %s policy, mode %s",
                              observe_only ? "observation" : "application", mode);
    if (message_length < 0 || (size_t)message_length >= sizeof(message))
        return -EOVERFLOW;
    descriptor = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (descriptor < 0)
        return -errno;
    /* Readiness follows attachment so service activation reflects a usable policy. */
    do {
        sent = sendto(descriptor, message, (size_t)message_length, MSG_NOSIGNAL,
                      (const struct sockaddr *)&address, address_length);
    } while (sent < 0 && errno == EINTR && !exiting);
    if (sent < 0)
        result = -errno;
    else if (sent != message_length)
        result = -EIO;
    close(descriptor);
    return result;
}

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Usage: %s --auto-detect --uid UID [--mode MODE] [--rules PATH]\n"
            "       [--observe-only] [--check-config]\n"
            "       %s --cgroup PATH [--interleave low|medium|high]\n"
            "       [--timeslice-us MICROSECONDS] [--observe-only]\n"
            "\n"
            "  --auto-detect       Track applications using rules and process identity.\n"
            "  --uid UID           Nonzero host user ID; required for auto-detection.\n"
            "  --rules PATH        Application rules file for automatic mode.\n"
            "  --mode MODE         auto, gaming, workstation, low-latency, ai, or server.\n"
            "                      Default: auto. Automatic mode only.\n"
            "  --check-config      Validate automatic mode rules without loading BPF.\n"
            "  --cgroup PATH       Existing child of /sys/fs/cgroup; alternative scope.\n"
            "  --interleave LEVEL  Cgroup interleave level; default high.\n"
            "  --timeslice-us N    Request 1..1000000 microseconds. Omit to keep\n"
            "                      the native timeslice. Cgroup mode only.\n"
            "  --observe-only      Count matches without requesting changes.\n"
            "  --help              Show this help.\n"
            "\n"
            "Run in the host PID, cgroup, and time namespaces. Cgroup mode also\n"
            "requires the full hierarchy at /sys/fs/cgroup. Container-local roots\n"
            "are unsupported. Start the policy before creating GPU contexts.\n"
            "The loader detaches on SIGINT, SIGTERM, or target identity changes.\n"
            "Detaching does not restore settings on existing GPU contexts.\n",
            program, program);
}

static int parse_positive_number(const char *text, __u64 maximum, __u64 *value)
{
    unsigned long long parsed;
    const char *character;
    char *end;

    if (!text[0])
        return -EINVAL;
    for (character = text; *character; character++) {
        if (*character < '0' || *character > '9')
            return -EINVAL;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno == ERANGE || *end || !parsed || parsed > maximum)
        return -EINVAL;
    *value = parsed;
    return 0;
}

static int validate_namespaces(void)
{
    const char *names[] = { "cgroup", "pid", "time" };
    struct stat current_status;
    struct stat initial_status;
    char current_path[64];
    char initial_path[64];
    FILE *offsets;
    char clock_name[32];
    long long seconds;
    long long nanoseconds;
    unsigned int seen_clocks = 0;
    int fields;
    int result = 0;
    size_t index;

    /* Matching PID 1 rejects common private namespaces, but requires host /proc. */
    for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        snprintf(current_path, sizeof(current_path), "/proc/self/ns/%s", names[index]);
        snprintf(initial_path, sizeof(initial_path), "/proc/1/ns/%s", names[index]);
        if (stat(current_path, &current_status) || stat(initial_path, &initial_status))
            return -errno;
        if (current_status.st_dev != initial_status.st_dev ||
            current_status.st_ino != initial_status.st_ino)
            return -EXDEV;
    }
    /* Process start times and BPF boottime must share the same clock origin. */
    offsets = fopen("/proc/self/timens_offsets", "re");
    if (!offsets)
        return -errno;
    while ((fields = fscanf(offsets, "%31s %lld %lld", clock_name,
                            &seconds, &nanoseconds)) == 3) {
        unsigned int clock_bit;

        if (!strcmp(clock_name, "monotonic"))
            clock_bit = 1;
        else if (!strcmp(clock_name, "boottime"))
            clock_bit = 2;
        else {
            result = -EINVAL;
            break;
        }
        if ((seen_clocks & clock_bit) || seconds || nanoseconds) {
            result = -EXDEV;
            break;
        }
        seen_clocks |= clock_bit;
    }
    if (!result && (fields != EOF || seen_clocks != 3 || ferror(offsets)))
        result = -EINVAL;
    fclose(offsets);
    return result;
}

static int root_mount_identifier(void)
{
    FILE *mounts;
    char *line = NULL;
    size_t capacity = 0;
    int result = -ENODEV;

    mounts = fopen("/proc/self/mountinfo", "re");
    if (!mounts)
        return -errno;
    while (getline(&line, &capacity, mounts) >= 0) {
        char root[PATH_MAX];
        char mount_path[PATH_MAX];
        char filesystem[32];
        char *separator;
        int identifier;

        separator = strstr(line, " - ");
        if (!separator ||
            sscanf(line, "%d %*u %*u:%*u %4095s %4095s", &identifier,
                   root, mount_path) != 3 ||
            sscanf(separator + 3, "%31s", filesystem) != 1)
            continue;
        if (!strcmp(mount_path, CGROUP_ROOT) && !strcmp(filesystem, "cgroup2")) {
            if (strcmp(root, "/")) {
                result = -EXDEV;
                break;
            }
            result = identifier;
        }
    }
    if (ferror(mounts))
        result = -EIO;
    free(line);
    fclose(mounts);
    return result;
}

static int cgroup_identifier(int directory, int expected_mount, __u64 *identifier)
{
    struct file_handle *handle;
    int mount_identifier;
    int result = 0;

    handle = calloc(1, sizeof(*handle) + sizeof(*identifier));
    if (!handle)
        return -ENOMEM;
    handle->handle_bytes = sizeof(*identifier);
    if (name_to_handle_at(directory, "", handle, &mount_identifier, AT_EMPTY_PATH)) {
        result = -errno;
    } else if (handle->handle_bytes != sizeof(*identifier) ||
               mount_identifier != expected_mount) {
        result = -EXDEV;
    } else {
        memcpy(identifier, handle->f_handle, sizeof(*identifier));
        if (!*identifier)
            result = -EINVAL;
    }
    free(handle);
    return result;
}

static int verify_identity(const char *path, int expected_mount, __u64 expected_identifier)
{
    __u64 identifier = 0;
    int directory;
    int result;

    directory = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0)
        return -errno;
    result = cgroup_identifier(directory, expected_mount, &identifier);
    close(directory);
    if (result)
        return result;
    return identifier == expected_identifier ? 0 : -ESTALE;
}

static int print_statistics(struct gpu_sched_gaming_bpf *skeleton)
{
    static const char *const labels[GPU_GAMING_STAT_COUNT] = {
        [GPU_GAMING_STAT_MATCHED] = "Matched GPU context initializations",
        [GPU_GAMING_STAT_INTERLEAVE_REQUESTED] = "Interleave requests",
        [GPU_GAMING_STAT_TIMESLICE_REQUESTED] = "Timeslice requests",
        [GPU_GAMING_STAT_SETTER_ERRORS] = "Setter errors",
        [GPU_GAMING_STAT_BIND_MATCHED] = "Matched bind events",
        [GPU_GAMING_STAT_BIND_INTERLEAVE_MATCHED] = "Bind events with requested interleave",
    };
    int processor_count = libbpf_num_possible_cpus();
    __u64 *values;
    __u32 key;
    int processor;

    if (processor_count <= 0)
        return processor_count < 0 ? processor_count : -EINVAL;
    if ((size_t)processor_count > SIZE_MAX / sizeof(*values))
        return -EOVERFLOW;
    values = calloc((size_t)processor_count, sizeof(*values));
    if (!values)
        return -ENOMEM;
    puts("Statistics count callbacks and requests; they do not establish hardware effects or FPS gains.");
    for (key = 0; key < GPU_GAMING_STAT_COUNT; key++) {
        unsigned long long total = 0;

        if (bpf_map_lookup_elem(bpf_map__fd(skeleton->maps.stats), &key, values)) {
            int result = -errno;

            free(values);
            return result;
        }
        for (processor = 0; processor < processor_count; processor++)
            total += values[processor];
        printf("%s: %llu\n", labels[key], total);
    }
    free(values);
    return 0;
}

int main(int argument_count, char **arguments)
{
    static const struct option options[] = {
        { "auto-detect", no_argument, NULL, 'a' },
        { "uid", required_argument, NULL, 'u' },
        { "rules", required_argument, NULL, 'r' },
        { "mode", required_argument, NULL, 'm' },
        { "check-config", no_argument, NULL, 'v' },
        { "cgroup", required_argument, NULL, 'c' },
        { "interleave", required_argument, NULL, 'i' },
        { "timeslice-us", required_argument, NULL, 't' },
        { "observe-only", no_argument, NULL, 'o' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };
    struct gpu_sched_gaming_bpf *skeleton = NULL;
    struct gaming_process_tracker *tracker = NULL;
    struct bpf_link *link = NULL;
    struct statfs filesystem;
    struct sigaction action = { .sa_handler = handle_signal };
    const char *requested_path = NULL;
    const char *interleave_name = "high";
    const char *rules_path = NULL;
    const char *mode = "auto";
    char *path = NULL;
    const char *character;
    __u64 identifier = 0;
    __u64 timeslice_us = 0;
    __u64 user_identifier = 0;
    __u32 level = 1;
    __u32 interleave = NV_INTERLEAVE_LEVEL_HIGH;
    bool observe_only = false;
    bool automatic = false;
    bool mode_set = false;
    bool check_config = false;
    bool interleave_set = false;
    bool timeslice_set = false;
    bool attached = false;
    int directory = -1;
    int processes;
    int mount_identifier = -1;
    long clock_ticks = 100;
    int option;
    int result = 0;
    unsigned int active_processes = 0;

    while ((option = getopt_long(argument_count, arguments, "h", options, NULL)) != -1) {
        switch (option) {
        case 'a':
            if (automatic)
                goto invalid_arguments;
            automatic = true;
            break;
        case 'u':
            if (user_identifier || parse_positive_number(optarg, UINT32_MAX, &user_identifier))
                goto invalid_arguments;
            break;
        case 'r':
            if (rules_path || !optarg[0])
                goto invalid_arguments;
            rules_path = optarg;
            break;
        case 'm':
            if (mode_set)
                goto invalid_arguments;
            mode_set = true;
            if (strcmp(optarg, "auto") && strcmp(optarg, "gaming") && strcmp(optarg, "workstation") &&
                strcmp(optarg, "low-latency") && strcmp(optarg, "ai") &&
                strcmp(optarg, "server"))
                goto invalid_arguments;
            mode = optarg;
            break;
        case 'v':
            if (check_config)
                goto invalid_arguments;
            check_config = true;
            break;
        case 'c':
            if (requested_path)
                goto invalid_arguments;
            requested_path = optarg;
            break;
        case 'i':
            if (interleave_set)
                goto invalid_arguments;
            interleave_set = true;
            if (!strcmp(optarg, "low"))
                interleave = NV_INTERLEAVE_LEVEL_LOW;
            else if (!strcmp(optarg, "medium"))
                interleave = NV_INTERLEAVE_LEVEL_MEDIUM;
            else if (!strcmp(optarg, "high"))
                interleave = NV_INTERLEAVE_LEVEL_HIGH;
            else
                goto invalid_arguments;
            interleave_name = optarg;
            break;
        case 't':
            if (timeslice_set || parse_positive_number(optarg, MAX_TIMESLICE_US, &timeslice_us))
                goto invalid_arguments;
            timeslice_set = true;
            break;
        case 'o':
            if (observe_only)
                goto invalid_arguments;
            observe_only = true;
            break;
        case 'h':
            usage(stdout, arguments[0]);
            return EXIT_SUCCESS;
        default:
            goto invalid_arguments;
        }
    }
    if ((automatic == (requested_path != NULL)) || optind != argument_count)
        goto invalid_arguments;
    if (automatic) {
        if (!user_identifier || interleave_set || timeslice_set)
            goto invalid_arguments;
    } else if (requested_path[0] != '/' || user_identifier || rules_path ||
               mode_set || check_config) {
        goto invalid_arguments;
    }

    if (check_config) {
        result = gaming_process_tracker_create((uid_t)user_identifier, -1, rules_path,
                                               mode, &tracker);
        if (result)
            fprintf(stderr, "Application configuration is invalid: %s\n", strerror(-result));
        else
            puts("Application configuration is valid; no BPF policy was loaded or attached.");
        goto cleanup;
    }

    result = validate_namespaces();
    if (result) {
        fprintf(stderr, "Host namespace check failed: %s\n", strerror(-result));
        goto cleanup;
    }
    if (automatic) {
        clock_ticks = sysconf(_SC_CLK_TCK);
        if (clock_ticks <= 0 || clock_ticks > 1000000) {
            result = -EINVAL;
            fprintf(stderr, "The host clock tick frequency is unsupported.\n");
            goto cleanup;
        }
    } else {
        mount_identifier = root_mount_identifier();
        if (mount_identifier < 0) {
            result = mount_identifier;
            fprintf(stderr, "A full cgroup2 hierarchy is required at %s: %s\n",
                    CGROUP_ROOT, strerror(-result));
            goto cleanup;
        }
        path = realpath(requested_path, NULL);
        if (!path) {
            result = -errno;
            fprintf(stderr, "Cannot resolve cgroup path: %s\n", strerror(-result));
            goto cleanup;
        }
        if (strncmp(path, CGROUP_ROOT "/", sizeof(CGROUP_ROOT)) ||
            !path[sizeof(CGROUP_ROOT)]) {
            result = -EINVAL;
            fprintf(stderr, "The cgroup must be a child of %s, not the hierarchy root.\n", CGROUP_ROOT);
            goto cleanup;
        }
        for (character = path + sizeof(CGROUP_ROOT); *character; character++) {
            if (*character == '/')
                level++;
        }
        directory = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (directory < 0) {
            result = -errno;
            fprintf(stderr, "Cannot open cgroup: %s\n", strerror(-result));
            goto cleanup;
        }
        if (fstatfs(directory, &filesystem)) {
            result = -errno;
            fprintf(stderr, "Cannot inspect cgroup filesystem: %s\n", strerror(-result));
            goto cleanup;
        }
        if (filesystem.f_type != CGROUP2_SUPER_MAGIC) {
            result = -EINVAL;
            fprintf(stderr, "The target is not on cgroup2.\n");
            goto cleanup;
        }
        processes = openat(directory, "cgroup.procs", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (processes < 0) {
            result = -errno;
            fprintf(stderr, "Cannot read target cgroup.procs: %s\n", strerror(-result));
            goto cleanup;
        }
        close(processes);
        result = cgroup_identifier(directory, mount_identifier, &identifier);
        if (result) {
            fprintf(stderr, "Cannot obtain a cgroup ID from the full hierarchy mount: %s\n", strerror(-result));
            goto cleanup;
        }
    }
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        result = -errno;
        fprintf(stderr, "Cannot install signal handlers: %s\n", strerror(-result));
        goto cleanup;
    }
    libbpf_set_print(print_libbpf);
    skeleton = gpu_sched_gaming_bpf__open();
    if (!skeleton) {
        result = -(errno ? errno : EIO);
        fprintf(stderr, "Cannot open BPF skeleton: %s\n", strerror(-result));
        goto cleanup;
    }
    skeleton->rodata->gaming_cgroup_id = identifier;
    skeleton->rodata->gaming_cgroup_level = level;
    skeleton->rodata->gaming_interleave = interleave;
    skeleton->rodata->gaming_timeslice_us = timeslice_us;
    skeleton->rodata->observe_only = observe_only;
    skeleton->rodata->automatic_detection = automatic;
    skeleton->rodata->gaming_user_id = (__u32)user_identifier;
    skeleton->rodata->clock_ticks_per_second = (__u32)clock_ticks;
    result = gpu_sched_gaming_bpf__load(skeleton);
    if (result) {
        fprintf(stderr, "Cannot load BPF policy: %s\n", strerror(-result));
        goto cleanup;
    }
    if (automatic) {
        result = gaming_process_tracker_create((uid_t)user_identifier,
                                               bpf_map__fd(skeleton->maps.game_processes),
                                               rules_path, mode, &tracker);
        if (!result)
            result = gaming_process_tracker_refresh(tracker, &active_processes);
        if (result) {
            fprintf(stderr, "Cannot initialize application tracking: %s\n", strerror(-result));
            goto cleanup;
        }
    } else {
        result = verify_identity(path, mount_identifier, identifier);
    }
    if (result || exiting) {
        if (result)
            fprintf(stderr, "Cgroup identity changed before attach: %s\n", strerror(-result));
        goto cleanup;
    }
    link = bpf_map__attach_struct_ops(skeleton->maps.gaming_ops);
    if (!link) {
        result = -(errno ? errno : EIO);
        fprintf(stderr, "Cannot attach GPU policy: %s\n", strerror(-result));
        if (result == -EEXIST || result == -EBUSY)
            fprintf(stderr, "An existing GPU policy may own struct_ops; no policy was replaced.\n");
        goto cleanup;
    }
    attached = true;
    if (exiting)
        goto cleanup;
    result = notify_ready(automatic ? mode : "cgroup", observe_only);
    if (result) {
        fprintf(stderr, "Cannot notify service readiness; detaching: %s\n", strerror(-result));
        goto cleanup;
    }
    if (automatic) {
        printf("Attached %s policy for UID %u with mode %s.\n",
               observe_only ? "observation" : "application", (unsigned int)user_identifier, mode);
        printf("Initially tracked application processes: %u.\n", active_processes);
        puts("Application discovery runs every 250 ms. Existing GPU contexts are not modified retroactively.");
    } else {
        printf("Attached %s policy to %s (ID %llu, ancestor level %u).\n",
               observe_only ? "observation" : "gaming", path,
               (unsigned long long)identifier, level);
    }
    if (!observe_only && !automatic) {
        printf("Requested interleave: %s.\n", interleave_name);
        if (timeslice_us)
            printf("Requested timeslice: %llu microseconds.\n", (unsigned long long)timeslice_us);
        else
            puts("Native timeslice preserved.");
    }
    puts("Policy ready. Press Ctrl+C to detach.");
    fflush(stdout);
    fflush(stderr);

    while (!exiting) {
        struct timespec interval = {
            .tv_sec = automatic ? 0 : 1,
            .tv_nsec = automatic ? 250000000 : 0,
        };

        result = automatic ? gaming_process_tracker_refresh(tracker, &active_processes) :
                             verify_identity(path, mount_identifier, identifier);
        if (result) {
            fprintf(stderr, "%s; detaching: %s\n",
                    automatic ? "Application tracking failed" : "Target cgroup disappeared or changed",
                    strerror(-result));
            break;
        }
        if (nanosleep(&interval, NULL) && errno != EINTR) {
            result = -errno;
            fprintf(stderr, "Cannot wait for policy shutdown: %s\n", strerror(-result));
            break;
        }
    }

cleanup:
    if (link) {
        int destroy_result = bpf_link__destroy(link);

        if (destroy_result) {
            fprintf(stderr, "Cannot cleanly detach GPU policy: %s\n", strerror(-destroy_result));
            if (!result)
                result = destroy_result;
        }
    }
    if (attached) {
        int statistics_result = print_statistics(skeleton);

        if (statistics_result) {
            fprintf(stderr, "Cannot read policy statistics: %s\n", strerror(-statistics_result));
            if (!result)
                result = statistics_result;
        }
        if (!observe_only)
            puts("Restart affected application processes to restore native context settings.");
    }
    gaming_process_tracker_destroy(tracker);
    gpu_sched_gaming_bpf__destroy(skeleton);
    if (directory >= 0)
        close(directory);
    free(path);
    return result ? EXIT_FAILURE : EXIT_SUCCESS;

invalid_arguments:
    fprintf(stderr, "Invalid or duplicate argument.\n");
    usage(stderr, arguments[0]);
    return EXIT_FAILURE;
}
