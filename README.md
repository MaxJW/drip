# drip

**Taildrop, by drag and drop.** A Plasma 6 tray applet for sending and receiving
files across your tailnet, on Wayland.

Drag a file at the tray icon and the panel springs open under your cursor,
showing your devices as a row of avatars. Drop on one and it sends. Incoming
files land owned by you, with a notification — or wait for you to accept them,
if you'd rather.

## Why this exists

Taildrop works from the CLI — `tailscale file cp <file> <host>:` to send,
`tailscale file get <dir>` to receive — but nothing tells you a file arrived,
nothing shows which devices are reachable, and there is no way to just throw a
file at a person.

## Install

### Arch, CachyOS, EndeavourOS, Manjaro

```sh
git clone https://github.com/MaxJW/drip && cd drip
makepkg -si -p packaging/PKGBUILD
```

### Anything else with Plasma 6

```sh
git clone https://github.com/MaxJW/drip && cd drip
./packaging/install.sh
```

Everything installs under `$HOME`. The script checks its prerequisites first and
names the exact packages to install for your distro if any are missing, offers
to set you as the Tailscale operator, then builds, installs, starts the engine
and reports whether it can see your devices.

Then add the widget: right-click the system tray → **Configure System Tray** →
**Entries** → set **drip** to *Shown*.

To remove it: `./packaging/install.sh --uninstall`

### Requirements

Plasma 6 on Wayland, Qt 6, KDE Frameworks 6, and Tailscale. You must be the
Tailscale operator — the installer offers to do this, or:

```sh
sudo tailscale set --operator=$USER
```

## Using it

- **Drag onto the tray icon**, hold briefly, and the panel springs open. Keep
  dragging onto a device and release.
- **Drop straight on the tray icon** when exactly one device is reachable.
- **Click an avatar** to pick files with the normal file dialog.
- **Click the folder path** at the bottom to open your downloads in your file
  manager, or the folder button on any history row to reveal that one file —
  for what you sent as well as what you received.
- **Right-click the icon → Configure drip…** for where files go, whether to
  accept automatically, whether to sort by sender, whether to keep history, and
  the size of the device pictures. This is Plasma's own configuration dialog,
  not a second settings UI.

Turning history off is not cosmetic: finished transfers are dropped as they
finish and anything already recorded is discarded. The history section then
stays hidden until something is in flight, and disappears again afterwards.
Files on disk are never touched either way.

Once the panel is open, a drop has to land on a device. There is no "send it
anywhere" fallback, so the rule does not change with how many devices happen to
be awake. The lower half of the panel is history, not a target.

Opening the panel re-asks tailscaled for device state, and keeps asking every
few seconds while it stays open, so a phone you have just woken appears without
closing and reopening it.

## Two things worth knowing

### "Accept" gates your folder, not the network

Taildrop has no pre-transfer handshake. By the time a file appears in the inbox
listing, tailscaled already holds **all of its bytes** — the sender's transfer
has completed. So with auto-accept off:

- **Accept** moves the file out of staging and into your folder.
- **Decline** deletes it from staging. It does not refuse the transfer, and it
  does not stop the sender sending it.

That is why drip says "Pi sent you a file", not "Pi wants to send you a file".
The prompt keeps unwanted files out of your Downloads and tells you who sent
what, but it is not a firewall.

### Dragging onto a tray icon requires a Plasma applet

The StatusNotifierItem protocol has no concept of drag-and-drop, so a standalone
app cannot receive a file dragged onto its tray icon on Wayland, however it is
built. Being an applet is what makes that interaction possible.

## Architecture

Three pieces. The engine is a separate process from the UI on purpose: the
applet runs *inside plasmashell*, where a stuck long-poll or a bad file write
would take the desktop shell with it.

```
   ┌───────────── tailscaled ─────────────┐
   │  unix:/var/run/tailscale/*.sock      │
   └───┬──────────┬──────────┬────────────┘
watch-ipn-bus  files/?waitsec  file-put
       └──────────┼──────────┘
              ╔═══╧═══╗   systemd --user, headless, always on
              ║ dripd ║   socket · inbox · transfers · settings · notifications
              ╚═══╤═══╝
                  │ D-Bus  dev.drip.Daemon
          ┌───────┴────────┐
          │ libdripplugin  │  QML: device & transfer models
          └───────┬────────┘  and the avatar cache
                  │
        ╔═════════╧═════════╗
        ║  dev.drip.applet  ║  tray icon + the panel
        ╚═══════════════════╝
```

`daemon/localapi.cpp` speaks HTTP/1.1 to the tailscaled socket over
`QLocalSocket`, because `QNetworkAccessManager` cannot use unix sockets.
tailscaled answers with `Transfer-Encoding: chunked` even for small responses,
so chunked decoding is mandatory rather than optional.

| Purpose | LocalAPI call |
|---|---|
| Peers, users, avatars | `GET /localapi/v0/status` |
| Send-eligible targets | `GET /localapi/v0/file-targets` |
| Send | `PUT /localapi/v0/file-put/{stableNodeID}/{name}` |
| Inbox, long-polled | `GET /localapi/v0/files/?waitsec=25` |
| Fetch one | `GET /localapi/v0/files/{name}` |
| Clear / decline | `DELETE /localapi/v0/files/{name}` |
| Live events | `GET /localapi/v0/watch-ipn-bus?mask=1` |

The inbox uses a 25-second long poll that blocks inside tailscaled, so waiting
for a file costs nothing.

Device state comes from the `watch-ipn-bus` event stream plus a 15-second status
re-ask. Both are needed: the bus gives instant reaction to netmap and state
changes, but it emits nothing when a peer sleeps or wakes, so without the timer
the device list drifts away from what tailscaled believes. `refresh()` coalesces
and the daemon republishes only on a real change, so a poll that finds nothing
costs one small request over a unix socket and wakes no UI.

Two other timers exist, both conditional: the panel re-asks every 4s while it is
open, and the inbox re-asks every 3s while a file sits undecided — an undecided
file leaves the inbox non-empty, which stops `waitsec` from blocking.

Settings appear in Plasma's standard configuration dialog, but the daemon keeps
its own copy in `~/.config/driprc`: dripd receives files whether or not the
widget is on a panel, so it has to answer "where does this go" and "should I ask
first" with no UI running. The applet is the only side that pushes, so there is
one direction of travel and no loop.

Received files are written by `dripd`, which runs as you, so they land as your
user wherever you point it. tailscaled stages arrivals in a root-owned
directory, but the process that pulls a file out is the one that writes it.

drip draws in the active KDE colour scheme and the popup wears the Plasma
theme's own background, so it follows your theme, light mode included.

## Checking it

`dripd --probe` prints what the engine can see and exits. Run this first if
anything looks wrong — it isolates the tailscaled conversation from the UI.

```
$ dripd --probe
socket: /var/run/tailscale/tailscaled.sock

inbox (0 waiting):
  (empty)
self:   desktop  (linux, 100.64.0.1)
owner:  you@example.com
state:  Running

devices (4):
  NAME                 OS        OWNER              ONLINE  CAN RECEIVE
  Pi                   linux     you@example.com    yes     yes
  Work Laptop          macOS     you@example.com    no      Offline
  Phone                iOS       you@example.com    no      Asleep — open Tailscale on it
  Shared iPad          iOS       someone@else.com   yes     Owned by someone else
```

`dripd --send <device> <file>...` sends from the terminal with a progress
readout, which confirms the transport works without involving Plasma.

### Why is my phone greyed out when the admin console says it is online?

The console and Taildrop answer different questions. The console reports that
the control plane heard from the device recently. Taildrop needs the device's
*peerapi* listener to answer right now, and iOS and Android suspend the
Tailscale extension as soon as the app goes to the background.

drip shows what tailscaled shows. You can check the two apart:

```sh
tailscale status | grep phone        # "offline, last seen 1m ago"
tailscale file cp /etc/hostname phone:
# warning: phone is reportedly offline; trying anyway
# 502 Bad Gateway
```

The CLI tries anyway and gets a 502: there is no listener. Open Tailscale on the
phone and it becomes a target within a second or two. drip says "Asleep — open
Tailscale on it" rather than "Offline" for this reason.

## Known limits

- **Plasma only.** The tray drag interaction is a Plasma applet feature, not
  something a standalone app can do on Wayland.
- **One file per transfer.** Taildrop has no directory support; dropping a
  folder is not yet handled.
- **Sender attribution is inferred.** Taildrop's `WaitingFile` carries only a
  name and size, so the sender is derived from recent peer activity on the event
  bus at the moment the file arrives. When nothing plausible is in the window it
  says "Unknown device" rather than naming the wrong person.
- **Avatars are per-account, not per-device.** They come from Tailscale's
  profile picture for whoever owns the node, so all your own machines wear the
  same face, distinguished by the OS badge.
- Devices not seen for 30 days are hidden; they are not plausible drop targets.
- No chat. Taildrop has no text channel; a "message" would be a small file.
- **No Flatpak.** A Plasma applet has to be installed where plasmashell looks
  for plasmoids, and the daemon needs the tailscaled socket. Sandboxing fights
  both.

## Layout

```
common/    helpers shared by both binaries (size formatting, unique paths)
daemon/    the engine: localapi · tailnet · transfers · inbox · settings · notifier · dbus
plugin/    QML module: D-Bus client, list models, avatar cache
applet/    the plasmoid: tray icon, panel, and the standard config page
packaging/ installer, systemd unit, PKGBUILD
```

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).
