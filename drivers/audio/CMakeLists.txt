# Project-local drivers build into the application image. They stay outside
# src/ and are gated by their own Kconfig symbols.
target_sources_ifdef(CONFIG_AUDIO_ES8311 app PRIVATE es8311.c)
target_sources_ifdef(CONFIG_AUDIO_ES7210 app PRIVATE es7210.c)
