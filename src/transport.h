#ifndef OSKEY_TRANSPORT_H
#define OSKEY_TRANSPORT_H

#ifdef CONFIG_OSKEY_RUST
void app_transport_init(void);
#else
static inline void app_transport_init(void)
{
}
#endif

#endif
