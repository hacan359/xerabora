# PS2 RetroAchievements — usage guide

How to run achievements on a real PlayStation 2 with Open PS2 Loader
(OPL) plus a small PC client. This covers what to install, what to turn
on, how the two menu items behave, and how in-game tracking works.

> [!WARNING]
> **Experimental.** This is a hobby experiment, not a finished product.
> Treat it as one, and use it at your own risk. The tested setup is a USB
> stick holding your game images; the internal HDD is untested, and
> without a USB stick you may hit problems.

## Overview

There are two parts:

- **The console** runs a patched OPL (`OPL-RA.ELF`). It hashes the game
  image, asks the PC which achievements to watch, then reads console
  memory every frame during play and streams snapshots to the PC.
- **The PC client** (`xerabora.exe` on Windows, `xerabora-linux-x86_64` on
  Linux) talks to the RetroAchievements server, tells the console what
  to watch, receives the snapshots, and unlocks achievements.

The console and the PC talk over the local network by UDP. No PC address
is configured anywhere — the console finds the client by broadcast.

## What you need

1. A PS2 that boots OPL (FMCB/FHDB, or from a memory card / USB).
2. A **network adapter** on the PS2 (the official Network Adapter for
   fat consoles, or the built-in port on slims) with a working IP —
   DHCP is fine. Configure it once in OPL: **Settings → Network settings**.
3. `OPL-RA.ELF` installed as an app in OPL (see below).
4. The PC client running on a machine on the **same LAN/subnet**, logged
   in to your RetroAchievements account.
5. Your game images (ISO) on a USB stick (the tested path). A share works too; the internal HDD is untested.

A network share is **not required** — USB alone works (see "Running from
USB only" below). The share only makes watch-list storage and diagnostic
logs more reliable.

## Installing OPL-RA.ELF

`OPL-RA.ELF` is a full OPL build (OPL 1.2.0 plus the RA additions), so
there are two ways to run it.

### Folder layout

Whatever the medium (USB stick, SMB share, HDD), use OPL's standard
layout. A USB stick, for example:

```
E:\ (mass0:)
├─ conf_apps.cfg          <- app list (see below)
├─ APPS\
│   ├─ OPL-RA.ELF
│   └─ OPL-RA-debug.ELF   <- optional, prints UDP debug logs
├─ DVD\                   <- your .iso images go here
├─ CD\   CFG\   LNG\   THM\   ART\   CHT\   VMC\
```

Game images are `.iso` files in `DVD\` (or `CD\` for CD-sized titles);
OPL lists whatever it finds there.

### conf_apps.cfg (so the app shows up)

`conf_apps.cfg` lives in the **root** of the config device and lists the
apps OPL shows under **Apps**. One `Name=path` per line. The path prefix
must match the device:

```
OPL+RA=mass0:APPS/OPL-RA.ELF
OPL+RA debug=mass0:APPS/OPL-RA-debug.ELF
```

- USB / block devices: `mass0:APPS/OPL-RA.ELF` (forward slashes)
- SMB share: `smb0:APPS\OPL-RA.ELF` (back slashes)
- Internal HDD: `hdd0:...`

Save it as plain ASCII with normal (CRLF) line endings — a mangled file
(everything on one line, stray characters) makes the entries disappear.

**If the OPL+RA app does not appear under Apps:** OPL is reading its
configuration from a different device than the one holding
`conf_apps.cfg`. Point OPL at the right device in
**Settings → (OPL) → save/config device**, or just launch the ELF
directly (below), which does not need `conf_apps.cfg` at all.

### The two ways to launch

1. **Directly from FMCB/FHDB/wLaunchELF:** point it at
   `mass0:/APPS/OPL-RA.ELF` (or the `smb0:` / `hdd0:` path). It boots
   like normal OPL. `conf_apps.cfg` is not involved.
2. **As an app inside a base OPL:** the base OPL reads `conf_apps.cfg`
   and shows **OPL+RA** under Apps; selecting it launches the RA build.
   This needs the config-device note above to be right.

Launching directly (option 1) is the most reliable and is recommended if
the app entry ever fails to appear.

## Notifications

The RA menu items report their result as an on-screen notice in the top
right corner, and they do so regardless of OPL's **Notifications**
setting (which is off by default). Turning that setting on only adds
OPL's own notices.

## The two menu items

Highlight a game in the list and open its context menu. Two items:

### RA: test PC connection

Broadcasts for up to 3 seconds and reports whether the PC client
answered. Use it to confirm the network path before playing.

- Success: `PC client found at 192.168.1.87 xerabora/1.0.0` /
  `Reply in N ms, console 192.168.1.142`
- No client: `No PC client answered within 3 seconds` — check that the
  client is running and UDP 18194 is open on the PC.
- No network: `The console could not open a network socket` — check the
  cable and the ETH device in Network settings.

### RA: check game support

Hashes the selected image and asks the PC whether RetroAchievements
knows it. This also downloads the watch list the console needs, so run
it once per game before playing (the list is cached afterwards).

- Supported: `<game title>: N achievements` — you are ready to play.
- Unsupported: `RetroAchievements does not know this image`.
- Client busy identifying: `The PC is still identifying the image` — try
  again in a few seconds.
- No answer: `The PC client did not answer` — run "test PC connection".

## Playing from the original disc

Two more items live in OPL's **main menu** (press START in the game list):

### RA: check disc support

Reads `SYSTEM.CNF` and the boot executable off the disc through the
console's own driver, hashes them and asks the PC, exactly like the
image check. Run it once per disc; the watch list is then in memory (and
saved next to your other games when a USB stick or share is available).

- `Could not read SYSTEM.CNF from the disc` — the drive gave up on that
  disc. If the console does not boot it from its own browser either, it is
  the disc or the laser, not the software.
- `No disc in the drive` / `The disc tray is open` — as it says.

### RA: launch disc

Boots the disc under OPL's in-game hooks. Telemetry and unlocks then work
as from USB. Not available in this mode: OPL's virtual memory cards,
per-game compatibility patches and cheats, and the in-game power-off
combo. START+SELECT to leave the game works.

Tested with Shadow of the Colossus: identified, launched, 60 snapshots a
second for a full session.

## Playing and unlocking

Start the game normally from OPL. The in-game hook begins streaming
memory snapshots to the PC after a short delay. Keep the PC client
running: it evaluates the snapshots and unlocks achievements on the
server as you earn them, exactly like an emulator. The client window
logs each unlock.

Achievements that depend on pointer chains are not supported on the
console and are disabled automatically (the client logs how many).

## The PC client

First run asks for your RetroAchievements login and password once; the
token is then saved (`%LOCALAPPDATA%\xerabora` on Windows,
`~/.config/xerabora` on Linux). Useful flags:

- `--logout` — forget the saved token.
- `--port N` — listen on a different UDP port (default 18194).
- `XERABORA_PASSWORD` — environment variable for non-interactive login.

On Windows the client needs an inbound firewall rule for UDP (the
installer/first run usually prompts; if broadcasts never arrive, add an
"Allow inbound UDP" rule for `xerabora.exe`, all ports, on the active
profile).

## Running from USB only (no share)

This works. The console brings the network up on demand when you use a
RA menu item, independent of any SMB share:

- The watch list is saved next to the game on the USB device (and, if a
  share exists, a second copy there).
- The diagnostic log falls back to `RA/hashes.txt` on the USB device
  when there is no share.
- With no SMB session, the console actually has *more* free network
  sockets, not fewer.

Requirement is only a configured ETH adapter with an IP. If you boot
purely from USB, still set up Network settings once so the console has
an address (DHCP is enough).

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| OPL+RA not listed under Apps | `conf_apps.cfg` is on a different device than OPL's config device, uses the wrong path prefix, or is malformed. Fix the path/device, or launch the ELF directly. |
| No on-screen messages at all | Turn on **Notifications** and cold-boot. |
| "test" works, "check" says no answer | Client not running, or not on the same subnet. |
| "could not open a network socket" | ETH device off/misconfigured, or no cable/link. |
| Client shows nothing when you press a menu item | Windows firewall blocking inbound UDP to `xerabora.exe`. |
| Achievements don't unlock in game | Client must stay running; run "check game support" once first so the watch list is loaded. |

## Notes and limits

- Console and PC must be on the same subnet (the client learns the
  console's MAC from its request).
- Images may be named `Title.iso` or the OPL Manager way,
  `SLUS_123.45.Title.iso`. ZSO/UL images and ELFs outside the image root
  are not covered.
- Games that load their own network modules (DEV9/SMAP) may take the
  interface away from the telemetry when launched from a disc; from USB
  the same games are fine.
- Games without `libpad` may not start telemetry (the hook rides the
  in-game reset path).
