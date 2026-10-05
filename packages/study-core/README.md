# Study core

Portable state shared by game adapters lives here. The core currently owns the
Study Mode state machine, selection memory, layout/audio helpers, and durable
account-sync client. It may depend on ordinary portable libraries, but it must
not include Shipwright, libultraship, renderer, or game headers.

Game adapters translate host events into these types and perform the requested
side effects through the Study Mod SDK.
