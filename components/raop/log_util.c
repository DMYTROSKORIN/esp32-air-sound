// Log levels and the millisecond clock the RAOP code expects the platform to provide.
#include "log_util.h"
#include "esp_timer.h"

log_level raop_loglevel = lINFO;
log_level util_loglevel = lWARN;

u32_t _gettime_ms_(void) { return (u32_t)(esp_timer_get_time() / 1000ULL); }
