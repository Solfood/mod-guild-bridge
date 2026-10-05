# mod-guild-bridge

AzerothCore module for the guildmaster project: writes world events to each world's own guildmaster database
and runs the orders of the app and the world controller (in-world only: it never starts, stops or schedules
anything). All documentation lives in the `Solfood/guildmaster` repo
(`docs/superpowers/specs/2026-10-04-bridge-data-contract.md`, thread `docs/threads/GM-BRIDGE.md`).
Unit tests: `bash tests/unit/run.sh` (macOS or Linux, needs a C++20 compiler).
