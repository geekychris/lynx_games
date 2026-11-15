# lynx_games

Atari Lynx games. This repo is the `games/` submodule of a lynxdev checkout
(github.com/geekychris/lynxdev) and is built from there: the SDK, the cc65
toolchain, the emulator and the `./lynx` CLI all live in lynxdev. Read
lynxdev's CLAUDE.md for the platform rules (log channel, memory map, Suzy,
costs, MCP tools).

## Porting into lynx_games

- **Where a game lives**: `<game>/` at the root of this repo, i.e.
  `lynxdev/games/<game>/`. Makefile: `NAME := <game>`, `LYNX_SDK ?= ../../sdk`,
  `include $(LYNX_SDK)/project.mk`. Create one from lynxdev with
  `./lynx new --game <game>`.
- **Paths**: test.py and tools find the lynxdev root by walking up to the
  directory with `lynx` and `sdk/` (or `$LYNX_ROOT`); see
  rock_blaster/test.py `_lynxdev_root()`. Pictures go in `<game>/images/`,
  test output in `<game>/build/` (ignored).
- **Test** (from the lynxdev root): `./lynx build <game>`, `./lynx test <game>`
  (runs `<game>/test.py`), `./lynx shot <game> -o games/<game>/images/x.png`;
  MCP `build("<game>")`, `emu_boot("<game>")`. `./lynx selftest` in lynxdev runs
  every game too.
- **PRs go to geekychris/lynx_games** (branch in `lynxdev/games`, push, PR,
  merge when the game's test and lynxdev's `./lynx selftest` pass). After the
  merge, bump the submodule pointer in lynxdev: `cd games && git checkout main
  && git pull && cd .. && git add games && git commit` -> PR to lynxdev.
- Each game: README (controls, what's kept/cut from the Amiga original, how
  it's tested), test.py that plays it and asserts via `lynx_log` lines, a
  screenshot in images/, and a row in this README's Games table (and in
  lynxdev's README).
