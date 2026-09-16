// SPDX-License-Identifier: GPL-2.0
/* Including the implementation keeps parser fixtures independent of live processes and BPF maps. */
#include "gaming_processes.c"

#include <assert.h>

static void test_environment(void)
{
    static const char valid[] = "SteamAppId=570\0WINEPREFIX=/some/prefix\0GPU_EXT_PROFILE=game\0";
    static const char fallback[] = "SteamAppId=0\0SteamGameId=18446744073709551615\0";
    static const char invalid[] = "SteamAppId=18446744073709551616\0GPU_EXT_PROFILE=game";
    static const char compat[] = "STEAM_COMPAT_DATA_PATH=/some/compat\0";
    struct process_environment environment;

    environment = parse_environment(valid, sizeof(valid) - 1);
    assert(environment.application_id == 570 && environment.wine_prefix);
    assert(environment.marker == CLASS_GAME);
    environment = parse_environment(fallback, sizeof(fallback) - 1);
    assert(environment.application_id == UINT64_MAX);
    environment = parse_environment(invalid, sizeof(invalid) - 1);
    assert(!environment.application_id && environment.marker == CLASS_NONE);
    environment = parse_environment(compat, sizeof(compat) - 1);
    assert(environment.wine_prefix);
}

static void test_stat(void)
{
    const char valid[] = "42 (name ) with (parentheses)) S 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 98765 0";
    __u64 value = 0;

    assert(!parse_start_time(valid, &value) && value == 98765);
    assert(parse_start_time("42 (short) S 1", &value) == -EINVAL);
    assert(parse_unsigned("18446744073709551616", 20, &value) == -EINVAL);
    assert(parse_unsigned("-1", 2, &value) == -EINVAL);
}

static void test_rules(void)
{
    char valid[] = "  compute directory /home/user/My Applications  ";
    char ignored[] = "# comment";
    char invalid[] = "game exe relative/path";
    char too_long[] = "game comm 1234567890123456";
    struct process_rule rule = { 0 };

    assert(parse_rule_line(valid, &rule) == 1);
    assert(rule.classification == CLASS_COMPUTE && rule.selector == SELECT_DIRECTORY);
    assert(!strcmp(rule.value, "/home/user/My Applications"));
    free(rule.value);
    assert(parse_rule_line(ignored, &rule) == 0);
    assert(parse_rule_line(invalid, &rule) == -EINVAL);
    assert(parse_rule_line(too_long, &rule) == -EINVAL);
    assert(directory_contains("/home/user/Games/", "/home/user/Games/title/game"));
    assert(!directory_contains("/home/user/Games", "/home/user/GamesElsewhere/game"));
}

static void test_modes(void)
{
    static const __u32 expected[][5] = {
        { 2, 2, 1, 0, 0 }, { 1, 2, 2, 1, 0 }, { 2, 2, 2, 0, 0 },
        { 1, 2, 1, 2, 0 }, { 0, 1, 1, 2, 1 },
    };
    struct tracked_process processes[2] = {
        { .classification = CLASS_COMPUTE }, { .classification = CLASS_GAME },
    };
    unsigned int mode;
    unsigned int classification;

    assert(effective_mode(MODE_AUTO, processes, 0) == MODE_WORKSTATION);
    assert(effective_mode(MODE_AUTO, processes, 1) == MODE_AI);
    assert(effective_mode(MODE_AUTO, processes, 2) == MODE_GAMING);
    assert(effective_mode(MODE_SERVER, processes, 2) == MODE_SERVER);
    for (mode = MODE_GAMING; mode < MODE_AUTO; mode++) {
        for (classification = CLASS_GAME; classification <= CLASS_BACKGROUND; classification++)
            assert(class_interleave(mode, classification) == expected[mode][classification]);
        assert(class_interleave(mode, CLASS_HIGH) == 2);
        assert(class_interleave(mode, CLASS_NORMAL) == 1);
        assert(class_interleave(mode, CLASS_LOW) == 0);
    }
}

static void test_classification(void)
{
    struct gaming_process_tracker *tracker = calloc(1, sizeof(*tracker));
    struct process_environment environment = { .marker = CLASS_NONE };
    char rule_text[] = "ignore comm game.exe";

    assert(tracker);
    tracker->home_directory = strdup("/home/user");
    assert(tracker->home_directory);
    assert(classify_process(tracker, "/usr/bin/kwin_wayland", "kwin_wayland", environment) == CLASS_DESKTOP);
    assert(classify_process(tracker, "/usr/bin/python3", "python3", environment) == CLASS_NONE);
    assert(classify_process(tracker, "/usr/bin/ollama", "ollama", environment) == CLASS_COMPUTE);
    assert(classify_process(tracker, "/home/user/Games/title/game", "game", environment) == CLASS_GAME);
    assert(classify_process(tracker, "/home/user/.steam/steam/steamapps/common/Title/game", "game", environment) == CLASS_GAME);
    assert(classify_process(tracker, "/home/user/.steam/steam/steamapps/common/SteamLinuxRuntime_sniper/run", "run", environment) == CLASS_NONE);
    assert(classify_process(tracker, "/usr/bin/wine64-preloader", "game.exe", environment) == CLASS_INTERACTIVE);
    environment.application_id = 570;
    assert(classify_process(tracker, "/usr/bin/wine64-preloader", "game.exe", environment) == CLASS_GAME);
    assert(classify_process(tracker, "/usr/bin/wine64-preloader", "services.exe", environment) == CLASS_NONE);
    environment.marker = CLASS_LOW;
    assert(classify_process(tracker, "/usr/bin/wine64-preloader", "game.exe", environment) == CLASS_LOW);
    assert(parse_rule_line(rule_text, &tracker->rules[0]) == 1);
    tracker->rule_count = 1;
    assert(classify_process(tracker, "/usr/bin/wine64-preloader", "game.exe", environment) == CLASS_IGNORE);
    gaming_process_tracker_destroy(tracker);
}

int main(void)
{
    test_environment();
    test_stat();
    test_rules();
    test_modes();
    test_classification();
    puts("Process parser and classification tests passed.");
    return 0;
}
