# Kenshi Bridge

A small plugin for [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) that connects a running Kenshi to external tools such as [Cactus](https://github.com/Mrgoldd11/Cactus) (a mod editor for Kenshi). Inside the game it opens a local server on `127.0.0.1:47801` and:

- reports state: selected character, position, faction, current dialogue, in-game time;
- runs commands: teleport, move the camera to a point, game speed, pause, list towns with border radii;
- records a **dialogue trace**: which dialogue the game chose for an event, which rivals were rejected and why (conditions failed, or lost on score).

It does **not** change game data (its `.mod` is empty) and listens on `127.0.0.1` only, so nothing outside your computer can connect.

**Optional.** Cactus works fine without it. The Bridge only adds live game features. Cactus (closed source freeware) and Kenshi Bridge (open source) share no code; they talk only through the local protocol described in [README_RU.md](README_RU.md) (protocol section, JSON over TCP; an English version is planned).

## Status

Built for **Kenshi 1.0.65 (Steam or GOG)**, RE_Kenshi 0.3.5, KenshiLib 0.5.0. Checked in the running game: ping, state, teleport, camera, speed, pause, dialogue trace, town list. **Not yet checked with the real game:** the Cactus window and its MCP tools talking to the Bridge (they were tested against a stand-in server). Treat this as a Beta.

## Install (from the release)

1. Install RE_Kenshi and start the game through it at least once.
2. Download `KenshiBridge-0.1.0-win-x64.zip` from [Releases](../../releases/latest) and copy the `KenshiBridge` folder into `<Kenshi>\mods\`.
3. Enable **KenshiBridge** in the game launcher's mod tab.
4. Check `<Kenshi>\RE_Kenshi_log.txt` for `[KenshiBridge] 0.1.0 listening on 127.0.0.1:47801`.

Uninstall: disable the mod in the launcher and delete the folder.

## Build from source

Needs the Visual Studio 2010 (v100, x64) toolchain, KenshiLib and boost, see the build section of [README_RU.md](README_RU.md) (`scripts\build.cmd`, tests: `scripts\test.cmd`).

## License

GPLv3 (see `LICENSE`), the same as KenshiLib. Not affiliated with Lo-Fi Games.
