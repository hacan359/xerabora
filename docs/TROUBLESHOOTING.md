# Before you report a problem

xeRAbora is alpha software, and the chain it runs on is long: the
image, OPL, our code inside the running game, the IOP module that sends
the frames, the network adapter, your router, the client on a PC or a
phone, and RetroAchievements itself. Any link can fail, and so far most
reports turned out to be a link that was not ours: an old ELF left on a
stick, two clients running at once, a dump the set was not made for, a
game OPL itself cannot run from USB.

Some games work, some do not, and a game that worked for one person
can hang for another. Before writing, get everything to one known
state, run the five steps below, and collect the two logs. A report
with those takes an evening to act on. A report that says "nothing
works" takes a week of questions.

## 1. One version everywhere

Take `OPL-RA.ELF` and the client from the same release, the
[latest one](https://github.com/hacan359/xerabora/releases/latest).
Delete every older `OPL-RA*.ELF` from the stick and the share, and
every `conf_apps.cfg` line that points at one. Older builds hang on
sets that newer ones handle, and the console does not say which build
it is running.

After "RA: test PC connection" the console shows the client's version
(`xerabora/0.1.0-alpha.N`). If it is not the one you just installed,
another client is answering: see the next step.

## 2. One client

Only one client can listen on UDP 18194. A second copy notices the
first and opens its page instead of starting, so a page that opens
does not mean a new client started: the old one, with the old version
or the old key, is still the one talking to the console.

- Windows: Task Manager (Ctrl+Shift+Esc) → Details → end every
  `xerabora.exe`, then start one.
- Linux, macOS: `pkill xerabora`, then start one.
- Android: **Quit** in the notification or on the page, then open the
  app again.

One client on the whole network, too: with the PC and the phone both
running, the console talks to whichever answered first.

## 3. Reset the client

Close the client, then delete its folder:

| | |
|---|---|
| Windows | `%LOCALAPPDATA%\xerabora` (Win+R, paste, Enter) |
| Linux, macOS | `~/.config/xerabora` |
| Android | Settings → Apps → xeRAbora → Storage → Clear data |

It holds the saved login (`credentials`), the Web API key (`apikey`),
the games the client has identified (`games`: serial and hash pairs),
the follow and network switches, custom sounds and `xerabora.log`.
After deleting you sign in and paste the key again.

`games` is the one that bites: the client remembers which image hash
belongs to a serial. Replace the image with another dump and the
console keeps getting the old answer until the file is gone.

## 4. Reset the console

On the stick (and on the share, if you use one) open the `RA` folder
and delete everything in it: `*.wl`, `launch.txt`, `hashes*.txt`.

Each `SERIAL.wl` is the watch list the PC wrote when you ran "RA:
check game support". It is what makes the console stream, and a list
written by an older client, or for another dump, points the console at
the wrong addresses. After deleting, run the check once more for every
game you play.

## 5. Check the image

A plain `.iso` in `DVD` or `CD`. "RA: check game support" tells you in
one line whether RetroAchievements knows this exact dump. "Does not
know this image" means the set was made for another version or region:
nothing to report, the game plays without achievements. ZSO, UL/split
images and HDL partitions are not hashed.

## Then test in this order

| Step | If it fails here |
|---|---|
| **RA: test PC connection** shows the PC's address | Network: same subnet, Windows firewall allowing `xerabora.exe` on private networks, one client running. Not us yet. |
| **RA: check game support** says supported, with counts | "Did not answer": network again. "Does not know this image": the dump. "Could not open a network socket": OPL's ETH settings or the cable. |
| The game starts on **plain OPL** from the same device | If plain OPL cannot run it, neither can we. Report it to OPL, not here. |
| The game starts on **OPL-RA** and LIVE shows snapshots arriving | Stuck on the loading screen with the client silent: this is ours. Collect the logs below. |
| An achievement unlocks and the TV flashes | Unlock on the page, no flash: look for `console acknowledged unlock notice` in `xerabora.log`. Present means the console got it. |

## Collect before writing

Use the debug build for the run you report: `OPL-RA-debug.ELF` from
the same release, next to the other one. It plays the same and writes
two extra files into `RA`, on the share when one is mounted and
otherwise on the game's device: `launch.txt`, one line per step of the
launch, and `hashes*.txt`, what the console hashed.

Then gather:

- console: model (`SCPH-xxxxx`), fat or slim, which network adapter;
- where the game is: USB stick, internal HDD, MMCE, disc; the image's
  file name;
- the release tag you installed, and the client's version as the
  console showed it;
- the client: Windows, Linux, macOS or the Android app;
- the game and its serial (`SLUS_xxx.xx`, from the OPL game list);
- what you did, step by step, and what the screen showed. If it hangs,
  at which second after the launch;
- `xerabora.log` from the client's folder (step 3), or on Android the
  Back button → **Send log**;
- `RA\launch.txt` from the stick or the share;
- whether the game runs on plain OPL from the same device.

## Writing the issue

One issue per game or per problem, in the
[tracker](https://github.com/hacan359/xerabora/issues). Name the game
and its serial in the title. Say what happened and what you expected,
attach the two logs as files, and list the points above. Keep it to
facts: a log line is worth more than an adjective. For a quick question
there is the [Discord](https://discord.gg/JZVNPF7cS2); for a bug, the
tracker, so the next person can find it.

The known limits are in [USAGE.md](USAGE.md#notes-and-limits) and the
FAQ on the [project page](https://hacan359.github.io/xerabora/#faq);
read them first, a few of the symptoms above are listed there as
not-yet-supported.
