# SPDX-License-Identifier: MPL-2.0

add_subdirectory(audio)
add_subdirectory(video)
add_subdirectory_ifdef(CONFIG_OSKEY_QMI8658A_FIFO sensor/qmi8658a_fifo)
