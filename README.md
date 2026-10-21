# matrix-lock

An idle screen lock for **labwc (Wayland)** whose lock screen is the Matrix
digital rain: katakana on pure black, drawn by
[neo](https://github.com/st3w/neo) inside
[swaylock-plugin](https://github.com/mstoeckl/swaylock-plugin), with
[swayidle](https://github.com/swaywm/swayidle) deciding when. Built and tested
on LXQt 2.3 + labwc 0.9.3, Ubuntu 26.04.

There is no clock, box or prompt on screen: the rain *is* the lock. Your
session keeps running underneath it.

## How it behaves

The idle clock counts from the last key press or mouse move.

| Idle | What happens |
|---|---|
| 4:30 | Notification "Locking in 30 s"; any input cancels |
| 5:00 | Rain lock. For 60 s a key press or a real mouse move dismisses it without a password |
| 6:00 on | Password needed. Typing shows the ring over the rain; Enter unlocks |
| 25:00 | Monitor off (`wlopm`); any input turns it back on |

`loginctl lock-session`, any suspend and the corner gesture lock
immediately, with no grace. An app that inhibits idle (a video playing) holds
the whole timeline off.

**Corner gesture.** Press Right Ctrl, then within 2 s push the pointer into
the bottom-right corner of the screen. Holding Ctrl while you flick works too;
the key only arms the corner, so Right Ctrl shortcuts in apps are unaffected.
A pointer already parked in the corner when you press the key does not count:
it has to arrive.

## What's in it

| Path | What it is |
|---|---|
| `bin/matrix-lock` | Starts swaylock-plugin with neo in a foot window as the background; extra flags pass through (`--grace 60`) |
| `bin/lock-test` | Test harness: arms automatic unlock timers, then locks, waits, and reports how it ended and what PAM logged |
| `src/matrix-corner.c` | The corner gesture: a 4 px invisible zone in the bottom-right corner, live for 2 s after Right Ctrl; touching it runs `matrix-lock` |
| `src/idle-watch.c` | Diagnostic: shows when the compositor considers you idle, counts down to the lock, and says when an app's idle inhibitor is holding it off |
| `src/wlr-layer-shell-unstable-v1.xml` | Wayland protocol the zone is drawn with (from wlr-protocols, its own permissive license) |
| `config/swayidle/config` | The timeline above |
| `config/matrix-lock/foot.ini` | foot profile for the rain only: 3x glyphs, pure black. Normal foot windows are unaffected |
| `install.sh` | Copies the above into `$HOME`, builds `matrix-corner` and `idle-watch`, adds swayidle to labwc autostart and the Right Ctrl keybind to `rc.xml`, backing up whatever it replaces |

## Build

Dependencies (Ubuntu 26.04):

```bash
sudo apt install swayidle swaylock wlopm foot libnotify-bin fonts-noto-cjk meson wayland-protocols libwayland-dev libxkbcommon-dev libcairo2-dev libpam0g-dev libsystemd-dev scdoc cargo rustc rustfmt autoconf automake libncurses-dev g++ git
```

Sources go in `~/.local/src`. Tested at swaylock-plugin `0541aab`,
windowtolayer `8bc8f30`, neo `6ba93ac`.

```bash
mkdir -p ~/.local/src && cd ~/.local/src && git clone https://github.com/mstoeckl/swaylock-plugin && git clone https://gitlab.freedesktop.org/mstoeckl/windowtolayer && git clone https://github.com/st3w/neo
```

neo, into `~/.local/bin`:

```bash
cd ~/.local/src/neo && ./autogen.sh && ./configure --prefix=$HOME/.local && make && make install
```

[windowtolayer](https://gitlab.freedesktop.org/mstoeckl/windowtolayer), which
turns the foot window into a surface swaylock-plugin can show (its build needs
`rustfmt` and Python 3):

```bash
cd ~/.local/src/windowtolayer && cargo build --release && install -m755 target/release/windowtolayer ~/.local/bin/
```

swaylock-plugin, into `/usr/local`:

```bash
cd ~/.local/src/swaylock-plugin && meson setup build --prefix=/usr/local && ninja -C build && sudo ninja -C build install
```

With that prefix the PAM file lands in `/usr/local/etc/pam.d/`, which PAM does
not read. Put it where PAM looks (it is `auth include login`, your normal
password):

```bash
sudo install -m644 /usr/local/etc/pam.d/swaylock-plugin /etc/pam.d/swaylock-plugin
```

Then:

```bash
./install.sh
```

swayidle starts at your next login. To start it now, run `swayidle -w &`
from a terminal in the session.

## Test safely

A lock you cannot get out of means a black screen and lost windows, so test
every change with `lock-test` first. It arms two systemd user timers that
send the unlock signal (at the given seconds and 60 s later) *before* it
locks, refuses to lock if they fail to arm, and cancels them once you are in.
They fire without your keyboard and without the terminal that started them.

```bash
lock-test selftest
```

Then, stopping at the first failure:

```bash
lock-test swaylock 90
```

```bash
lock-test plain 90
```

```bash
lock-test matrix 90
```

```bash
lock-test matrix 300 --grace 60
```

```bash
lock-test corner 90
```

`corner` arms the timers, then waits 30 s for you to lock with the gesture.
To try the gesture without locking at all, create the dry-run file; a hit then
shows a notification instead. Delete it afterwards, or the gesture stays inert:

```bash
touch "$XDG_RUNTIME_DIR/matrix-corner.dry-run"
```

In each, type a wrong password and Enter first: a
`pam_unix(...:auth): authentication failure` line in the report proves your
keys reach the lock. Then unlock with the real one.

## If the idle lock never comes

Run `idle-watch` in a terminal and walk away. It asks labwc for two 5 s idle
timers: the normal one, which is swayidle's view and stops while any app holds
an idle inhibitor, and an input-only one, which counts keys and mouse alone.
When the normal one fires it notifies you and counts down to the lock; the log
it leaves says what happened:

| Log line | Meaning |
|---|---|
| `idle (normal)` then a countdown | swayidle's clock is running; the lock comes at 0 |
| `INHIBITED` | No input, but an app (a video, an app keeping the computer awake) stops the clock |
| nothing at all | Input never stops: a device is sending events by itself |

```bash
idle-watch
```

## If a lock gets stuck

- **Unlock with SIGUSR1**, from any shell running as your user. Over SSH from
  another machine that is one line, and it needs no Wayland variables:

  ```bash
  ssh you@this-machine pkill -USR1 -x swaylock-plugin
  ```

- **Never SIGTERM or SIGKILL the locker.** labwc keeps the session locked when
  the lock client dies without unlocking, by design, and you are left with a
  black screen. Starting a new `swaylock` takes over that lock, but restarting
  labwc is the only other way out, and it closes every window.
- **Set up SSH before you experiment**, so the way in does not need the
  keyboard at the machine. Key-only is enough for this (`PasswordAuthentication
  no`), and an `authorized_keys` line can be limited to one source machine with
  `from="<address>"`. Ctrl+Alt+Fx to a text console may not work: it did not
  during the lockout that led to `lock-test`.
- **Check the keyboard itself.** That lockout was most likely a compact
  keyboard frozen by Fn-layer chords (trying Ctrl+Alt+Del on a 65% board), not
  the lock: every later test took the password fine.

## Known quirks

- foot prints `wayland.c:1767: no monitors available` each time the lock
  starts, yet the rain appears. swaylock-plugin starts the plugin before it
  creates the output the plugin draws on and appears to restart it; this is
  not verified. If the rain ever fails, you get a black lock that unlocks
  normally.
- swaylock-plugin with no `--command-each` falls back to `swaybg`; without it
  installed you get plain black (that is what `lock-test plain` shows).

## Uninstall

Remove the swayidle lines from `~/.config/labwc/autostart` and the
`Control_R` keybind marked `matrix-lock` from `~/.config/labwc/rc.xml`, then
`pkill swayidle` and `labwc --reconfigure`, and delete
`~/.local/bin/matrix-lock`, `~/.local/bin/matrix-corner`, `~/.local/bin/idle-watch`,
`~/.local/bin/lock-test`, `~/.config/swayidle/config` and
`~/.config/matrix-lock/`.

## License

MIT, for the scripts and config in this repo. swaylock-plugin (MIT),
windowtolayer and neo (GPL-3.0) are not included; they are built from their
own sources under their own licenses.
