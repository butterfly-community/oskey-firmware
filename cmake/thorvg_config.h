#include "config.h"

/* ThorVG checks loader support with #ifdef, while LVGL defines disabled loaders as 0. */
#if !LV_USE_LOTTIE
#undef THORVG_SVG_LOADER_SUPPORT
#undef THORVG_LOTTIE_LOADER_SUPPORT
#endif
