<p align="center">
  <img src="docs/banner.png" alt="OPL + RA" width="720">
</p>

# xeRAbora

RetroAchievements on a real PlayStation 2.

A fork of [Open PS2 Loader](https://github.com/ps2homebrew/Open-PS2-Loader)
reads the running game's memory every frame and streams it over the
network. A small program on your PC receives the stream, runs
[rcheevos](https://github.com/RetroAchievements/rcheevos), and unlocks
achievements on your RetroAchievements account. No emulator, no modchip
beyond what OPL already needs, no changes to the game.

> [!WARNING]
> **Experimental.** This is a hobby experiment, not a finished product.
> Treat it as one, and use it at your own risk. Run it from a USB stick
> that holds your game images: that is the only setup tested so far. The
> internal HDD is untested, and without a USB stick you may hit problems.

## What you need

- A PS2 with a network adapter and a way to run OPL (FMCB, FHDB or similar).
- Your game images on a USB stick, or the original disc in the drive. Both
  are tested; a share is optional and the internal HDD is untested.
- A PC on the same local network, Windows or Linux.
- A [RetroAchievements](https://retroachievements.org) account.

## Setup

1. Download `OPL-RA.ELF` and `xerabora.exe` (Windows) or `xerabora-linux-x86_64`
   (Linux) from the [releases](../../releases).
2. Put `OPL-RA.ELF` where you keep your OPL and launch it instead of OPL.
   It behaves like OPL 1.2.0 with two extra items in each game's menu.
3. **Turn on notifications.** In OPL open **Settings → Notifications**, set
   it **On**, **Save Changes**, and cold-boot the console. It ships off by
   default, and the RA menu items show their result as an on-screen notice,
   so leave it off and you see nothing.
4. Run `xerabora` on the PC. It asks for your RetroAchievements username and
   password once, then saves a login token in your user profile
   (`%LOCALAPPDATA%\xerabora` on Windows, `~/.config/xerabora` on Linux). The password
   is not stored.
5. On the console, select a game, open its menu (triangle) and choose
   **RA: test PC connection**. A notice tells you whether `xerabora` answered,
   from which address and how fast. If nothing answers, check that the PC
   is on the same network and that the firewall allows inbound UDP 18194.
6. In the same menu choose **RA: check game support**. The console hashes
   the disc image and asks the PC whether RetroAchievements knows it; the
   notice shows the game title and the achievement counts (total, unlocked,
   unsupported). Supported games get an `RA` prefix in the list and a badge
   on the cover art.
7. Start the game. Within about 30 seconds the PC shows the first snapshot
   and the number of achievements it is tracking. Unlocks appear in the
   PC window and on your profile.

**Playing from the original disc.** Put the disc in the drive, press START
for OPL's main menu and choose **RA: check disc support**, then **RA: launch
disc**. The check reads the disc through the console's own driver and asks
the PC the same way it does for an image; the launch boots the disc under
OPL's in-game hooks, so telemetry and unlocks work exactly as from USB.
In this mode OPL's virtual memory cards, per-game compatibility patches
and cheats are not available -- they live inside the part of OPL that
emulates the drive, which a real disc does not use.

Disc mode is experimental. It has been verified with Shadow of the
Colossus only; a game it cannot boot may hang the console hard (black
screen, the reset button does nothing) -- power the console off and back on
to recover. Transformers: The Game is one known case.

The full walkthrough is in [docs/USAGE.md](docs/USAGE.md).

Softcore only. The console can write to game memory (OPL's cheat engine),
so `xerabora` does not claim hardcore mode.

`xerabora` plays a short sound when the console connects, when the stream
stops for five seconds, and when an achievement unlocks. `--no-sound`
turns them off. To use your own, put `connect.wav`, `disconnect.wav` or
`achievement.wav` into the `sounds` folder next to the saved login
(`%LOCALAPPDATA%\xerabora\sounds` or `~/.config/xerabora/sounds`). On Linux the
sounds go through `paplay`, `aplay` or `pw-play`, whichever is installed.

## How it works

```
PS2 game ──► ee_core reads watched addresses every frame (VBLANK)
         ──► SIF DMA ──► raudp (IOP) builds UDP frames, bypasses the TCP/IP stack
         ──► Ethernet ──► xerabora on the PC ──► rcheevos ──► retroachievements.org
```

- **Image check** (OPL menu): the console computes the RetroAchievements
  hash of the disc image and broadcasts `RAQ1 <hash>`. `xerabora` asks the RA
  server for the achievement set, extracts every memory address the set
  reads, and sends that *watch list* back in chunks. The console stores it
  next to the game and in memory.
- **In game**: `ee_core` copies the watched values into a snapshot every
  frame and DMAs it to the IOP. `raudp` finds the PC with one broadcast,
  then sends each snapshot as one or two 1472-byte UDP packets, built by
  hand and handed straight to the network driver so the game's own traffic
  is never blocked.
- **On the PC**: `xerabora` reassembles snapshots, exposes them to rcheevos as
  a sparse memory space, and rc_client evaluates the achievements.

The console side does not follow pointer chains (indirect reads).
`xerabora` disables the achievements that actually dereference one after
the game loads and reports how many; arithmetic over plain addresses
(AddSource chains and the like) is fine. Most PS2 sets lose few or none:
Shadow of the Colossus none of 96, Transformers: The Game 5 of 76.

The wire protocol is documented in [`client/src/protocol.h`](client/src/protocol.h).

Known limits:

- The PC must be on the same subnet as the console. The console learns the
  PC's MAC address from the discovery reply; a PC behind a router is heard
  but cannot be answered.
- The image check reads plain ISO images whose boot executable sits in the
  root directory (all retail PS2 discs do), named either `Title.iso` or the
  OPL Manager way, `SLUS_123.45.Title.iso`. ZSO and UL-format images are
  not hashed.
- A disc the drive cannot read all the way through -- one the console will
  not boot from its own browser either -- fails the check with "could not
  read SYSTEM.CNF"; that is the disc or the laser, not the software.
- Telemetry starts when the game opens its controller through libpad, which
  is how OPL's in-game hooks attach. A game with unusual input code may
  never start sending; the PC then shows no snapshots.
- Reads see RAM, not the EE data cache, so a value the game wrote very
  recently can lag by a fraction of a frame.

## Building

**OPL fork** needs the ps2dev toolchain. The simplest route is the same
container the OPL project uses:

```
docker run --rm -v "$PWD":/src -w /src/opl ghcr.io/ps2homebrew/ps2homebrew:main make PADEMU=0 all
```

`make RA_DEBUG=1 all` builds the debug variant: it writes the IOP module
load results into the money counter of Need for Speed: Underground 2, the
game used during development. Before the network is up, that counter is
the only in-game diagnostic channel.

**PC client** (`client/`) builds on its own, without the OPL fork: it
needs only rcheevos and the headers in `protocol/`. `make` on Linux needs
gcc and libcurl development headers. `make windows` cross-compiles a
static `xerabora.exe` with MinGW-w64 (`gcc-mingw-w64-x86-64` on
Debian/Ubuntu); HTTPS goes through WinHTTP, so the Windows build has no
external dependencies.

Both the OPL fork and rcheevos are git submodules. Clone with
`--recurse-submodules`, or run `git submodule update --init --recursive`
after cloning. Building the client needs only `third_party/rcheevos`; the
`opl` submodule is there to build the console loader.

## Repository layout

| path | contents |
|---|---|
| `opl/` | submodule &rarr; the [OPL+RA fork](https://github.com/hacan359/Open-PS2-Loader/tree/ra). RA additions: `src/ra*.c`, `ee_core/src/ra.c`, `modules/network/raudp`, `modules/network/ps2ips-ra`, small changes in `SMSTCPIP` and `smap-ingame` |
| `client/` | the PC client: a small, self-contained RA client (see [Reusing the client](#reusing-the-client)) |
| `protocol/` | the wire protocol &mdash; [`PROTOCOL.md`](protocol/PROTOCOL.md) plus the shared `ra_snap.h` / `ra_watch.h` |
| `third_party/rcheevos` | submodule, rcheevos, unmodified |
| `tools/` | development helpers |

## Reusing the client

The PC client is deliberately generic: a small UDP server that speaks a
documented protocol and drives rcheevos. It knows nothing about OPL, and
nothing about the PS2 beyond the memory it is handed. Any agent that
reads a console's memory and speaks the protocol in
[`protocol/PROTOCOL.md`](protocol/PROTOCOL.md) can reuse it to run
RetroAchievements, on the PS2 or on other hardware.

The split is clean. The client identifies a game by its hash (the RA
server resolves the console and title), derives the watch list from the
achievement set, reassembles snapshots, and unlocks. The
console-specific work, reading memory and computing the RA image hash,
lives in the agent. The OPL fork in this repo is one such agent; another
console needs a new agent, not a new client.

## Notes for OPL developers

- `modules/network/ps2ips-ra` is ps2sdk's `ps2ips.irx` with two fixes.
  In `do_recvfrom` the shared RPC buffer was overwritten before the DMA
  destination was read, so UDP receive on the EE never worked. In both
  `do_recv` and `do_recvfrom` the 144-byte `rests_pkt` was DMA'd into
  the EE's 128-byte `_intr_data`, zeroing the socket `close` pointer in
  `libps2ips` right behind it; every socket then leaked and the menu had
  one network operation per boot. The transfer is now capped at 128
  bytes. EE code receiving through `ps2ips` should still use 64-byte
  aligned buffers and lengths that are multiples of 64 (see `src/ranet.c`).
  Both candidates for upstreaming to ps2sdk.
- Non-blocking receive on the menu (netman) lwIP stack: setting the
  socket non-blocking with `fcntl(O_NONBLOCK)` did not take effect on
  hardware; `recvfrom` blocked the EE I/O thread forever when no reply
  came. `src/ranet.c` therefore passes `MSG_DONTWAIT` (value `0x08`, the
  lwIP/IOP value — not the EE `<sys/socket.h>` `0x80`, since the flag is
  forwarded verbatim over RPC to `lwip_recvfrom` on the IOP) on every
  receive, which lwIP honours per call.
- `modules/network/common/smstcpip.h` declared `lwip_recvfrom` with six
  arguments; the SMSTCPIP implementation takes eight. Fixed here.
- The fork loads the network modules in every game mode, including USB,
  and builds `cdvdman` with `USE_DEV9=1` for the BDM variant.

## License

The OPL fork keeps OPL's license (Academic Free License 3.0). The PC
client is released under the MIT license. rcheevos is MIT.

The OPL logo belongs to the Open PS2 Loader project and the RA mark to
RetroAchievements; they appear here to show what this project connects.
