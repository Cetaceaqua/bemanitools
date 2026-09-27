#ifndef KPMHOOK_READER_HOOK_H
#define KPMHOOK_READER_HOOK_H

#include <stdbool.h>
#include "kpmhook/config-io.h"

void kpm_reader_hook_init(const struct kpmhook_config_io *cfg);
void kpm_reader_hook_fini(void);
bool kpm_reader_is_card_present(void);

#endif /* KPMHOOK_READER_HOOK_H */
