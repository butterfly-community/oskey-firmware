/* SPDX-License-Identifier: Apache-2.0 */

#ifndef OSKEY_AUDIO_H
#define OSKEY_AUDIO_H

/*
 * Configure the board audio codec (sample rate and volume) once at boot.
 * Playback and volume are then driven over zbus commands; the application
 * talks to the codec through the Zephyr audio codec API only.
 * Returns 0 on success or a negative errno.
 */
int app_audio_init(void);

#endif /* OSKEY_AUDIO_H */
