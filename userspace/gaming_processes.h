/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GAMING_PROCESSES_H
#define GAMING_PROCESSES_H

#include <sys/types.h>

struct gaming_process_tracker;

/* A negative map descriptor allows configuration validation without BPF access. */
int gaming_process_tracker_create(uid_t owner, int map_descriptor,
                                  const char *rules_path, const char *mode,
                                  struct gaming_process_tracker **output);
int gaming_process_tracker_refresh(struct gaming_process_tracker *tracker,
                                   unsigned int *active);
void gaming_process_tracker_destroy(struct gaming_process_tracker *tracker);

#endif
