/* SPDX-License-Identifier: MPL-2.0 */

#ifndef OSKEY_MICROPHONE_H
#define OSKEY_MICROPHONE_H

#include <stdbool.h>

int app_microphone_init(void);
void app_microphone_set_enabled(bool enabled);
int app_microphone_pause(void);
void app_microphone_resume(void);
bool app_microphone_ready(void);
int app_microphone_entropy_start(void);
void app_microphone_entropy_stop(void);

#endif /* OSKEY_MICROPHONE_H */
