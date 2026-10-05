# Study Mod SDK

This package is the game-neutral binary contract between a recompiled-game
host and an external study plugin. It contains no Shipwright types and exposes
only the versioned C ABI in `include/study_mod/study_mod_api.h`.

Hosts implement the API and export `StudyMod_GetHostApi`. Plugins compile
against this header and negotiate the ABI version, table size, and individual
capability bits at runtime. Adding a function requires appending it to a table;
existing fields may not move or change type.
