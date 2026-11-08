# FreeTunnel user guide

Use this guide for installation details, settings and troubleshooting.
For downloads, supported systems and a quick overview, see the [README](README.md#download).
The desktop sections below cover Windows, macOS and Linux; phone and TV instructions
are in [Android and Android TV](#android-and-android-tv).

## Contents

- [Install and first launch](#install-and-first-launch)
- [Android and Android TV](#android-and-android-tv)
- [System tray and closing the window](#system-tray-and-closing-the-window)
- [Importing a configuration](#importing-a-configuration)
- [Split tunnelling](#split-tunnelling)
- [Kill switch](#kill-switch)
- [Settings](#settings)
- [Global hotkeys](#global-hotkeys)
- [External control](#external-control)
- [Updates](#updates)
- [Troubleshooting](#troubleshooting)

## Install and first launch

Desktop builds are **unsigned** (no code-signing certificates), so the OS may
warn on first launch.

### Windows

If SmartScreen warns: **More info** → **Run anyway**.

### macOS

Version 1.2.4 requires macOS 14.4 or newer.

Right-click the app → **Open**. Or clear the quarantine flag:

```sh
xattr -dr com.apple.quarantine /Applications/FreeTunnel.app
```

### Linux

The .deb:

```sh
sudo apt install ./freetunnel-linux-x86_64.deb
```

The AppImage:

```sh
chmod +x freetunnel-x86_64.AppImage && ./freetunnel-x86_64.AppImage
```

### Administrator prompt

The VPN requires elevated privileges:

| Platform | What you see |
| --- | --- |
| Windows | A UAC prompt |
| macOS, Linux | A prompt for an admin password, the first time you connect in a session |

## Android and Android TV

### Installation and VPN permission

Install the APK using your browser or file manager. If Android blocks installation,
allow that app to install unknown apps, then open the APK again. The same APK works
on phones and TVs. On first connection, approve Android's VPN permission prompt.

### Sending configurations to a TV

1. Open **Configurations → Receive from phone** on the TV.
2. Keep your phone on the same local network and scan the QR code, or open the displayed address.
3. Paste one or more `tt://` links separated by spaces or new lines, or select configuration files.
4. Send them and review each configuration on the TV. **Add** is selected by default.

**Sent** means the item reached the TV; it still needs confirmation there.
Invalid items show an error. If only some items fail, retry those items rather
than sending the whole batch again. Keep the receive window open while sending.

### Routing and connection protection

Use the installed-app picker for app rules. Desktop drag-and-drop and hotkeys do
not apply on Android. Changing app rules may rebuild the Android VPN connection;
see [Android routing details](android/README.md#split-tunnelling).

For protection while the app is stopped or Android restarts it, enable Android's
**Always-on VPN** and **Block connections without VPN**, where the device offers
them. The in-app kill switch protects a running session; it does not replace
these system settings.

### Updating

Open **Settings → Maintenance → Check for updates**. Android may ask you to allow
FreeTunnel to install apps before the installer can open. After granting that
permission, return to FreeTunnel to continue the prepared update.

For platform-specific behavior and technical details, see the
[Android documentation](android/README.md).

## System tray and closing the window

The **system tray** (the menu bar on macOS) can connect or disconnect, switch
configs, show the window and quit.

Closing the window keeps FreeTunnel and the VPN running:

| Platform | Closing the window |
| --- | --- |
| Windows | ✕ minimizes the window to the taskbar. Turn on **Minimize to tray** in Settings and ✕ hides it to the tray instead. Alt+F4 quits. |
| Linux (X11/XWayland) with a usable tray | ✕ and the desktop's own close (Alt+F4, a taskbar's Close, Hyprland's `killactive` and `closewindow`) hide the window to the tray immediately. The minimize button still minimizes. |
| macOS | The red button (or ⌘W) hides it. The menu-bar icon or the Dock brings it back. |
| A Linux desktop with no usable system tray, such as GNOME without an AppIndicator extension, or whose tray bar (waybar, say) is not running | ✕ and the desktop's own close quit, since nothing would be left to bring the window back from. |

A window hidden to the tray comes back from the tray icon, or from **Show
FreeTunnel** in its menu. On Windows, ✕ quits if no system tray is available, even with
**Minimize to tray** enabled.

To quit, choose **Quit** in the tray menu (or press Ctrl+Q, or ⌘Q on macOS).

On Linux the desktop's own close is the same request however it is sent, so a
desktop that logs out by asking every window to close gets the answer Alt+F4
does: FreeTunnel stays open, and the desktop has to end it. Choose **Quit**
first if a logout waits on it. And it applies on X11 and XWayland, which is what
the AppImage and the `.deb` run on; a build run natively on Wayland still quits
on it.

On Hyprland and sway, **Show FreeTunnel** (and the tray icon) brings a window that
is open but not in focus to the workspace you are on, and the window may blink once
as it does. To switch to the window's workspace instead, turn on Hyprland's
`misc:focus_on_activate`, or its `focus_on_activate` window rule for FreeTunnel
alone.

Force-killing the window, with Hyprland's `forcekillactive` or `killwindow`,
`hyprctl kill` or `xkill`, ends FreeTunnel, and the VPN with it. No program can
catch that.

## Importing a configuration

- **Configs → ＋**: create a new TOML, import from file, or paste a `tt://` link
  from the clipboard.
- **⌘V or Ctrl+V** on the **Configs** page pastes a `tt://` link the same way.
- **A `tt://` link** opened from a browser or another app imports too.

For a link, pasted or opened, FreeTunnel names the server the link would add and
asks before adding it. It asks the same way for the file a TrustTunnel endpoint
exports with `--format toml`, imported from file (see
[DEEP_LINK.md](DEEP_LINK.md#the-endpoints-toml-export)).

Passwords stay in the OS credential store: Keychain on macOS, Credential Manager
on Windows, libsecret on Linux.

## Split tunnelling

The **Split tunnelling** tab decides what goes through the tunnel. Rules are
manual addresses (a domain, an IP, a subnet) or programs, and both obey the **Mode**
switch at the top of the page:

| Mode | What is listed |
| --- | --- |
| *Bypass VPN* | goes around the tunnel |
| *Through VPN* | is the only thing inside it |

Under *Through VPN* with no rule that can be used, FreeTunnel keeps the full
tunnel and the page says so, rather than send everything around it.

### Geo lists

In the **Geo lists** block, open **Add list** and choose a ready-made list or
**Add your own list**.
Each list has its own **Through VPN / Bypass VPN** choice and can be turned off
without removing it. Lists belong to the selected split profile. Nothing is
added until you choose it.

Custom sources use an HTTPS download address. Choose **Sites** for domains or
**IP addresses** for subnets. Plain text lists contain one entry per line;
V2Ray-format `.dat` files also need a category after `#`, for example
`https://example.org/geosite.dat#youtube`. Regular expressions and keyword
matches are skipped; the card reports how many entries were skipped.

Manual destination rules win over geo lists. If two geo lists disagree,
**Through VPN** wins. Domain rules require a hostname visible to the core
(for example through DNS or TLS); a connection whose hostname cannot be
identified is matched by its IP address.

Use **Update** to refresh a list, or choose automatic updates every 1, 2, 5 or
24 hours. Choose **Never** for manual updates only after the initial download. Updates run
while FreeTunnel is running; opening the app checks for overdue lists. Failed
downloads keep the last valid copy. The list card shows its last update and
any error. A list that has never loaded cannot route traffic yet.

Android and Android TV use the same lists and priorities. Updating active
routing rules there may briefly reconnect the VPN. Android cannot combine
app selection with destination selection in the same way as desktop: if both
are selected, FreeTunnel uses the full tunnel and shows a notice. Very large
combined lists also fall back to the full tunnel with a notice.

### Changes while connected

On desktop, every change on this page, turning split tunnelling on or off included, reaches
a tunnel that is already up without reconnecting it:

- A program rule applies from the program's next connection.
- After a change to the addresses or the mode, connections that were open start
  again under the new rules.

### Program rules

Add a program in any of three ways:

- pick it from the list of what is installed;
- drag its icon onto the page;
- pick the file yourself.

What to expect:

- Nothing extra is installed for this: no driver, no system extension, no
  permission dialog.
- A rule added while the VPN is up applies to the next connection rather than
  the next session. Which program a connection belongs to is worked out by
  asking the operating system who owns the socket.
- On Windows, a rule for an app that updates itself the way Discord and Slack
  do, from a folder named after each version, names the app and keeps matching
  it after its updates.

### Profiles

Rules are grouped into profiles, addresses and programs both, and a config can
be tied to one. A set for work and a set for everything else can then be
switched between without retyping either.

Switching to another config while connected reconnects, with the new config's
profile.

### Excluded routes

**Settings → Excluded routes** lists subnets that stay outside the tunnel for
every config.

- They are added to the routes a config excludes itself, not used in their
  place. A config made in FreeTunnel or imported from a link already keeps
  local networks and multicast outside the tunnel, and emptying the list in
  Settings does not change that.
- Unlike the rules above, a change to this list while connected reconnects.
- A route of every address (`0.0.0.0/0`, `::/0`) is refused here and on the
  Split tunnelling page, since it would take all of that traffic out of the
  tunnel.

## Kill switch

Turn it on in **Settings → Security**. It is for a connection that is not up:
while FreeTunnel connects, or brings back a connection that dropped, connections
that would have gone out over the open network are refused instead. What split
tunnelling sends around the tunnel still goes.

A first connect that keeps failing keeps trying, blocked, until it gets through
or you press **Disconnect**. The status reads "Connecting…" ("Waiting for
network…" while there is none), and the window says why it is failing.

### When it applies

The block belongs to the running VPN session, so it is not a firewall of its
own:

| Situation | The block |
| --- | --- |
| FreeTunnel is connecting, or bringing back a connection that dropped | On |
| You change anything on the **Split tunnelling** page | Stays on: the session stays up, and the block with it ([how changes apply](#changes-while-connected)) |
| While connected, you switch configs, or change the excluded routes, the kill switch itself or, on Windows, **Let the VPN config open ports** | Lifts for the moment it takes to build a session anew |
| The server refuses the login or its certificate, and FreeTunnel starts over | Lifts for a moment, as above |
| The config names its server by a domain name rather than an IP address, and that name cannot be looked up | None: there is no session at all |
| The VPN is off, or an error has stopped it | None: nothing is blocked |

### Windows: ports a config opens

FreeTunnel ignores the ports a config file names to pass the kill switch
(`killswitch_allow_ports`) unless **Let the VPN config open ports** is on. That
setting is under the kill switch in Settings.

- Use it, for example, to reach this computer over Remote Desktop while the kill
  switch is on.
- It is one setting for every config, including any you import later, so turn
  it on only if you trust every config you use.

## Settings

| Section | What is there |
| --- | --- |
| **General** | Language (English, Русский), Theme (System, Light, Dark), Launch at system startup, Connect on startup; on Windows, Minimize to tray |
| **Security** | [Kill switch](#kill-switch); on Windows, [Let the VPN config open ports](#windows-ports-a-config-opens) |
| **Excluded routes** | Subnets that stay outside the tunnel for every config ([details](#excluded-routes)) |
| **Hotkeys** | Enable, then Toggle VPN, Connect and Disconnect ([details](#global-hotkeys)) |
| **Logging** | Enable logging (app events and forwarded core messages appear together); Verbose logs (full VPN core detail, for debugging) |
| **Maintenance** | Check for updates ([details](#updates)), and the version you run |

## Global hotkeys

Set hotkeys for toggle, connect and disconnect in **Settings → Hotkeys**. They
are off until you turn them on there.

- A hotkey needs Ctrl, Alt or Meta (⌘, ⌥ or ⌃ on macOS), unless it is one of
  F1–F12.
- Hotkeys work while the window is minimized or hidden.
- On Linux they need an X11 (Xorg) session: under Wayland the setting is
  unavailable.

## External control

Three commands control the VPN from outside the window:

- `freetunnel://toggle`
- `freetunnel://connect`
- `freetunnel://disconnect`

`tt://…` links import a config; see
[Importing a configuration](#importing-a-configuration).

### From a Stream Deck button or a script

Run FreeTunnel with the command as its argument. A running FreeTunnel acts on
it at once and leaves the window alone, as with the hotkeys; otherwise
FreeTunnel starts and acts on it.

Windows (or wherever you installed it):

```bat
"C:\Program Files\FreeTunnel\FreeTunnel.exe" freetunnel://toggle
```

macOS:

```sh
/Applications/FreeTunnel.app/Contents/MacOS/FreeTunnel freetunnel://toggle
```

Linux, .deb:

```sh
freetunnel freetunnel://toggle
```

Linux, AppImage: the AppImage's path followed by the command.

```sh
/path/to/freetunnel-x86_64.AppImage freetunnel://toggle
```

### Opened as a link

The same URL works opened as a link too: from a browser, a document, `open` on
macOS or `xdg-open`. But any web page can open a link, so:

- a link that would turn the VPN off brings up the window and asks first;
- connecting is not asked about.

See [DEEP_LINK.md](DEEP_LINK.md#freetunnel-control-links-separate).

### One FreeTunnel per user

Each user runs one FreeTunnel. Launching it again, with a command or without,
hands over to the copy already running.

If that copy cannot be reached, as when the keyring that holds its launch key is
locked, the new launch closes without acting on the command rather than start a
second FreeTunnel. Run it again once the keyring is unlocked.

## Updates

FreeTunnel looks for a newer release shortly after it starts, and
**Settings → Maintenance → Check for updates** looks again.

Updates are verified with SHA-256 manifests and Ed25519 signatures before
install. How, and what that guards against: [SECURITY.md](SECURITY.md).

## Troubleshooting

- **A desktop window is blank or menus disappear:** close FreeTunnel completely
  (including its tray icon), then try software rendering in a new terminal. In
  PowerShell: `$env:QT_QUICK_BACKEND="software"; & "C:\path\to\freetunnel.exe"`;
  on Linux: `QT_QUICK_BACKEND=software freetunnel`; on macOS:
  `QT_QUICK_BACKEND=software /Applications/FreeTunnel.app/Contents/MacOS/freetunnel`.
  Use your installed executable path; close that terminal after testing.
  Hardware acceleration remains the default; this workaround only changes
  how the window is drawn.
- **The TV cannot receive a configuration:** keep the receive window open and check
  that both devices are on the same local network. Guest Wi-Fi or client isolation
  can prevent the phone from reaching the TV.
- **A configuration is rejected:** copy the complete `tt://` link or obtain the
  original TOML file from your provider. Check the error shown for that item.
- **The VPN cannot connect:** check the device's date and time, network access and
  the server details supplied by your provider. Use **Logs** to see the reported error.
- **An update cannot be verified:** download it again using the official release
  link in the README. Do not bypass verification.

When reporting a problem, include the app version, device/OS, steps to reproduce
and the relevant error. Remove passwords, authentication keys and private server
addresses before sharing logs or screenshots.

Report ordinary bugs in [Issues](https://github.com/dimmmmmmmer/freetunnel/issues).
For private vulnerability reports, use [SECURITY.md](SECURITY.md).
Build instructions belong in [CONTRIBUTING.md](CONTRIBUTING.md); dependency licenses
and source information are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
