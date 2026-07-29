#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>

bool user_button_exists(void);
int user_button_init(void);
void user_button_request(bool active);
#endif
