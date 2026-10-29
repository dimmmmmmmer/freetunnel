# FreeTunnel for Android

Android VPN client using the official, checksum-pinned **TrustTunnel 1.1.7**
adapter, for phones, tablets and Android TV (Android 8.0 or later). Official
releases are the `android-X.Y.Z` releases of
[dimmmmmmmer/freetunnel](https://github.com/dimmmmmmmer/freetunnel), signed with
the certificate pinned in `release-certificate.sha256`; a fork's release name or
certificate is not an official release or an update compatibility contract.

## Features

- Confirmed import of TOML, `tt://` and TrustTunnel QR-page links. Import only
  adds a profile; it never starts VPN or replaces another profile implicitly.
  Links of format versions 0 to 2 are read; TrustTunnel 1.3 writes version 2.
  Subscriptions are not followed yet: a link or file with only a subscription
  is refused, and from other links and files only the server's own settings
  are imported. The subscription address is not kept; the rest of a file is
  kept as written. Servers that need an auth key are refused until the app
  moves to TrustTunnel core 1.3. The rules for each version 2 field are the
  desktop's, in [DEEP_LINK.md](../DEEP_LINK.md); for TOML, only an empty
  `client_random_auth_key` counts as no key. That is stricter than the
  desktop, which also reads a blank key, or one that is not a string, as no
  key. The endpoint writes only an empty key or a hex one, so the two differ
  only on a file edited by hand. Of a server's certificate, only
  the certificates are kept: other PEM data is left out and the import says
  so. A whole chain in one block, as the desktop's
  FreeTunnel 1.1.9 and earlier wrote one, is kept as it is. A certificate
  with no certificate in it is refused. The TOML file a TrustTunnel endpoint
  exports, with the server's settings at the top and no `[endpoint]` table,
  is imported as a link with the same settings would be, and refused as one
  would be when it lacks the server's host name, addresses, user name or
  password. It is named after its `name`, or, when it has none, as other
  TOML files are: after the file, or "From clipboard" when pasted.
- TOML, Android adapter and native configuration checks before saving.
  The editor shows the password as dots and prevents screen capture.
- AES-GCM encrypted profiles with Android Keystore. Read failures preserve
  ciphertext and prevent overwriting inaccessible storage. Backup/device transfer
  are excluded; the existing storage format and alias remain compatible.
- A private VPN-consent activity and Quick Settings tile. Connection state is
  process-local, so a stopped process cannot leave a persisted Connected state.
- Kill switch enabled by default. For protection while the process or TUN is
  absent, enable Android **Always-on VPN** and **Block connections without VPN**
  (see [Always-on VPN](#always-on-vpn-and-block-connections-without-vpn) below: the
  app's Settings says nothing about them, as the desktop's says nothing).
  Android controls crash restart; neither START_STICKY nor Always-on is a watchdog.
- Replacement TUN established before releasing the previous one. Unsupported
  IPv6 is blocked; supported IPv6 imported from links uses global `2000::/3`.
  VPN is unmetered on Android 10+; fatal errors show a separate notification.
- Russian and English UI, chosen under Settings → General → Language (English or
  Русский, each named in itself); until one is chosen the app speaks the device's.
  TCP ping measures reachability of the first endpoint
  address, not VPN throughput or tunnel latency: every config at once, with
  3 seconds each, "…" until it answers, then the time or "—" for any failure.
- The desktop's look: its colours, which `tests/test_android_theme_matches_desktop.sh`
  checks against `qml/Main.qml`, and its icons, which
  [scripts/android-icons-from-svg.py](../scripts/android-icons-from-svg.py) makes
  from `assets/icons` and `assets/logo.svg` (and the arrows and ticks ✓ ↓ ↑ ← from
  the outlines of those glyphs in Inter, which the desktop draws them in, at its sizes), and its typeface, Inter, which
  `tests/test_android_fonts.sh` holds to the desktop's two weights (see
  [License](#license)). The theme is System (the default,
  which follows the device), Light or Dark, under Settings → General → Theme.

## Home and Configs

Both are the desktop's pages ([HomePage.qml](../qml/pages/HomePage.qml),
[ConfigsPage.qml](../qml/pages/ConfigsPage.qml)), on a phone and a TV alike.

- **Home.** The logo is the only control for the connection: it connects,
  cancels a connection on its way up, or disconnects. It is at half strength
  with no session, at 0.7 while connecting or disconnecting and whole while
  connected, sits 12 dp lower with no session and rises when one starts, pulses
  while connecting (reconnecting too) and disconnecting, and shrinks to 0.96 while pressed or
  while OK is held. Under it, while there is a session, one line: Connecting…,
  Disconnecting…, or the time the connection has been up, `HH:MM:SS` as the
  desktop shows it. While the kill switch blocks traffic the service is
  reconnecting by itself, with its backoff, and Home says what the desktop's
  says while its tunnel reconnects: Connecting…, in the same colour, with the
  logo at 0.7 and pulsing. There is no link to reconnect now, as there is no
  such control on the desktop: nothing but the logo is a button on Home, and a
  tap on it disconnects, as it does a tunnel that is up or on its way up.
  The notification keeps its Reconnect and Disconnect actions. The connection coming up is shown by the logo and the line alone: there is no
  notice for a check of the config or for a rebuild of the VPN, as there is none
  on the desktop, whose notices are errors and what an import did. With no
  config the logo, "Add a config" and + all open Configs with its add menu.
- **Server choice is on Home.** The config's name with a ▾ opens a dropdown
  under it with a row for each config, three showing at once, the selected one
  in the accent. A row selects that config, and reconnects to it if a session
  is up. Back, a tap outside and the selector itself close it; Back does not
  leave Home. + beside it opens Configs with its add menu.
- **Speeds.** Two tiles at the foot, ↓ and ↑ in MB/s, from this app's own
  traffic counters (`TrafficStats`) once a second while Home shows a connected
  session. The app carries the tunnel's traffic, so the figures include its
  overhead; a device without those counters shows 0.0. Nothing but the timer and
  the two numbers changes once a second, so the focus and anything being done
  on the page are left alone.
- **Configs.** A row for each config: its logo (whole for the selected one,
  dim for the others), its name, the ping, a connected or connecting… badge on the
  selected one while a session is up, and icons to export, edit and delete.
  There are no text buttons. A tap on the row selects it. The export icon opens
  a menu: Export .toml… (the system's file picker, so a TV with none says so)
  and Copy deep-link, which puts a `tt://` link on the clipboard, marked as
  sensitive on Android 13 and newer. Both carry the password, which the notice
  after says. The link is the one the desktop's export writes, byte for byte
  (DeepLinkImport.encode, pinned by a pair of links in both apps' tests).
  On a phone a row is moved by pressing and holding it and then dragging; a TV
  has no way to, as the desktop has no keys for it. The selection stays on the
  same config.

## Settings and Logs

Settings and Logs are the desktop's pages ([SettingsPage.qml](../qml/pages/SettingsPage.qml),
[LogsPage.qml](../qml/pages/LogsPage.qml)), with what only the desktop has left out (Launch at system
startup, Hotkeys, the Windows-only ports row, Verbose logs, the log's path) and a few rows that only Android
has. Their words are the desktop's English and its Russian wherever the control is the same:
`tests/test_android_strings_match_desktop.sh` holds every string to `i18n/freetunnel_ru.ts`, through a
table of the strings the desktop has too, and fails for one that is neither in it nor on its
Android-only list.

**Settings**, top to bottom:

- **General**: *Language* (English or Русский; it recreates the activity, which comes back on Settings;
  Android 13 and up keeps the choice itself, with its own per-app language setting listing the same two,
  and before that the app wraps its contexts, the VPN service's notifications and the tile's words
  too, in the saved one), *Theme* (System, Light or Dark) and *Connect on startup*, with a line between the rows.
- **Security**: *Kill switch*, with its subtitle, and the amber banner while storage fails. Nothing of
  Android's own is added to it: Always-on VPN is explained in
  [Always-on VPN](#always-on-vpn-and-block-connections-without-vpn) below, not in the app.
- **Excluded routes**: subnets that bypass the tunnel at the routing level, whatever the split rules say, as
  chips (the **×** takes one out at once) and a field that takes several at once, separated by spaces, commas
  or semicolons. What is no subnet is named, and a **/0**, which is every address and would take all its
  traffic out of the tunnel, is refused and says why. *Restore defaults* and *Clear all* each ask first. The
  list is the preference `excluded_routes` (JSON); where none is saved it is the defaults in
  `data/split/default-excluded-routes.txt`, one file for this app and the desktop, copied into the APK's
  assets and compiled into the desktop (`scripts/check-apk-data.sh` checks the APK has it). A cleared list is
  `[]`, which is not absent. `RoutingConfig.prepare` adds the routes to the config's own
  `listener.tun.excluded_routes`: subnets only, never a /0, none twice, IPv6 ones only where the session has
  IPv6, and the result is checked as the config is, so routes that together cover an address family are
  refused. A change builds a running VPN again. **A bare address is kept as a /32 (or /128)**: the core's TOML needs the prefix.
- **Logging**: *Enable logging*.
- **Maintenance**: the one row of the desktop, with no switch for checking on launch (the app does that by
  itself, silently, as the desktop does; see Authenticated Android updates): the update line, a link over its
  words alone, with its picture at the right. Idle it says **Check for updates**
  with the version; up to date adds a ✓; checking shows three hopping dots and does nothing when clicked, while
  keeping a remote's focus; an offer says **Version X is available** with an arrow down that bends into ↻ under
  the focus or pointer and a click downloads; a download says **Downloading… N%** beside a turning ↻, and a
  click does nothing, as the desktop's cannot stop one either; a downloaded update says so in Android's own words and a click installs it (after the question, with the
  VPN on); an error says why, worded as the desktop words a failed check, beside a ↻ that tries again. The line is
  the only control, so the texts that tell what to do next say "this line", and there is no *Install update* button
  on the screen (that is what a screen reader adds to the line, and the red button of the question). Outside the
  official build the section says it doesn't come from the official channel.
- The footer, **FreeTunnel 1.0.0 · TrustTunnel core 1.1.7**, each name a link to its page (a toast where there is no
  browser, as on most TVs; the core's version is read from the AAR's name by the build). There is no screen for
  the third-party notices, as the desktop has none: they are files in the APK (see License below).

**Logs**: **Clear** and **Copy** as links as wide as their words (Copy says *Log copied*, and asks the
clipboard not to show what it copied, as the log lists where connections went); *Logging is off.* with **Turn on**;
the log in a box that fills the page and scrolls in itself, *Logs will appear after connecting* in the middle of
it while it is empty; and **Auto-scroll** at the foot. The box follows the file once a second, by changing its text
alone, and holds off while text is selected. On a TV the text is not selectable, so the arrows scroll the box, and Up
from its top goes to the link just above it (Copy, or Turn on), not to a tab.

The log is the file the VPN service writes, one entry to a line, `yyyy-MM-dd HH:mm:ss<TAB>LEVEL<TAB>message`
as the desktop writes it, and the page shows `HH:mm:ss LEVEL message`; a line written by an earlier version is shown
as it was. The service logs its own events at INFO, WARN or ERROR and every connection the core reports
(`onConnectionInfo`) at INFO, one line each, so with logging on the log lists where the traffic went. The file is kept to its last 128 KiB
when it passes 256 KiB and the page shows its last 1000 lines. Turn logging off in Settings to write nothing.

## Split tunnelling

The Split page is the desktop's (`qml/pages/SplitPage.qml`): the **Split Tunneling** switch,
the **Mode** (Bypass VPN or Through VPN), a notice where it applies, the **Profile** chips, the
**Rules** of the profile being edited as chips with the field to add them, and its
**Applications**.

**Profiles.** A profile is a name with its own domain rules and its own applications, in the order
they were added. *Default* is always there, first, and cannot be deleted. The **+** chip opens a field
for a new name (*profile name, then Enter*; at most 16 characters; Escape, Back, or going to another
control drops it). A name that differs from another only in letter case is refused, as the desktop
refuses it. A custom profile's **×** deletes it after a question, and the configs that used it use
Default from then on.

The chips choose only which profile the page shows and edits. **Which profile a config uses is
chosen in its editor** (*Split profile*); a config that names none, or one that was deleted, uses
Default. The app, the Quick Settings tile and Android's Always-on VPN all build the session with
the profile of the config that is selected, so they route alike. Editing that profile while split
tunnelling is on builds a running session again (the rules reach a session only that way);
editing another profile, or choosing which to edit, restarts nothing.

**Rules.** *Recommended for Russia* adds the list to the profile being edited, skipping what it has.
The list is `data/split/recommended-russia.txt`, one file for this app and the desktop: the build copies
it into the APK's assets, and the desktop has it compiled in. A new installation starts with split
tunnelling on and Default holding that list, as the desktop's does. An update from 1.0.0 puts the one
list of domains and the one of applications it kept into Default, sorted. Words typed into the field are
added if they are rules, and the ones that are not are named in the desktop's words. Applications
are added from the picker, one to a visit; one that is no longer installed is listed by its package
name, so that it can be removed.

**Storage.** The profiles are the preferences' `split_profiles`, as JSON. Each saved config has a
`profile` of its own (absent for Default). 1.0.0's `domains` and `apps` stay, written with every change
as a copy of the lists of the profile the selected config uses, so that going back to 1.0.0 finds
a working set.

| Mode | Rules | Behavior |
| --- | --- | --- |
| Bypass VPN | Apps and/or destinations | Selected traffic bypasses VPN |
| Through VPN | Apps only | Native general mode plus Android app allowlist |
| Through VPN | Destinations only | Native selective mode without app filtering |
| Through VPN | Empty / all selected apps removed | Full VPN with a notice |
| Through VPN | Apps and destinations | Full VPN with a notice |

The adapter cannot express the union of Android app and destination rules: those
filters intersect. Full VPN protects both selections explicitly, and the page says so in a notice
that names the config and its profile; use one rule type for selective routing. The notices come from
the same code that builds the session (`RoutingConfig.prepare`), so they say what it will do.
Imported modes/exclusions are preserved when split
settings are disabled, and changing modes does not reverse imported exclusions; the page lists
the rules the selected config brings with it.
Destination `/0` rules and whole-family bypass exclusions are rejected; default
routes remain valid in `included_routes`. Domains support IDN and whitespace,
commas or semicolons as separators.

## Android TV

The same APK installs on Android TV and Google TV, sideloaded, and shows on
the TV launcher with its banner. It works with a remote:

- The arrows reach every control, and the one with the focus looks as the
  desktop's control does under the pointer, which the desktop has in place of a
  focus ring: a tab and a row fill with the surface colour (a tab also grows to
  1.08, and the current one keeps its infoBg), a link and Home's config selector
  change colour and underline, a dropdown's value and ← go from textDim to the
  text colour, a chip goes to surfaceHover, a button to its hover colour (the
  red one darker red, Save darker accent), a ChipX and a config row's icons
  turn danger or the text colour while the row fills, and a text field's or a
  select box's border turns accent. What the desktop gives no hover takes the
  nearest thing it has: a toggle's row fills as a config row does (the toggle
  draws nothing for it), and the Home logo is held at 1.05, the pulse's peak,
  with no tile behind it, which would wash out its greys. What is selected stays
  as it is: the edited profile chip and the primary button are accent with
  accentText, the red button is red with a white label, a field is inputBg,
  with a remote or without one. Nothing is drawn round a control, and no focus
  is wider than what it holds, except a row of a list. When a page updates,
  such as on a ping result or a change of connection state, the focus and the
  scroll position stay where they were; when the control that had the focus is
  gone (a deletion's last chip, Clear all, the last config) it goes to what is
  beside it, not to the tab at the top.
- Home opens on the logo, which is its control. Back goes to Home first, onto
  that control, and leaves the app from there.
- The configuration editor opens on the name. Up and Down leave a field of one
  line for the control above or below it, as they leave any control (Android
  13 and later would keep them inside the field for a keyboard's arrows); the
  log's box is left with Up at its top and Down at its end.
- Every dialog, menu and picker is the app's own overlay, in the same look and
  with the same focus (see Dialogs, menus and notices): a remote never meets a
  system dialog with a ring of its own. Back closes an overlay first.
- Pages keep clear of the screen's edges, which some TVs crop, and the log is
  not selectable, so the arrows scroll through it.
- The split-tunnelling app picker also lists apps made only for TVs.

Most TVs have no Always-on VPN, no Quick Settings tile and no notification
shade; there the kill switch protects only while FreeTunnel runs. Where a TV has no file picker,
or no screen for allowing VPN connections, the app says so instead of closing.
A configuration gets onto a TV with Receive from phone, the first item in the
menu under the + on Configs on a TV, through From file… where there is a file
picker, or through Create new…, the form, which a phone's keyboard (the Google
TV app's, for example) fills in.

The banner is [art/tv-banner.svg](art/tv-banner.svg), rendered into
`app/src/main/res/drawable-xhdpi/banner.png` by
[scripts/render-android-tv-banner.sh](../scripts/render-android-tv-banner.sh).
Its font comes from fontconfig, so the PNG is committed: after changing the
SVG, run the script and commit both.

### Dialogs, menus and notices

The app draws every dialog, menu, picker, confirmation and notice itself, in
its own window and in the desktop's look: the same colours, radii, type, buttons
and behaviour, in the light and the dark theme. There is no system dialog,
popup, menu or toast of the app's own anywhere, and
[tests/test_android_tv_dialogs.sh](../tests/test_android_tv_dialogs.sh) fails
the build on one, and on an overlay added by any code but the one helper that
traps the focus and handles Back. Only screens Android owns are Android's: the
VPN consent, the package installer, the file picker, Android's own settings,
and the Quick Settings tile's and the consent screen's notices, which have no
window of ours to appear in.

- **Add menu**, under the + on Configs: Receive from phone (on a TV, first),
  Paste from clipboard, From file…, and Create new…. Paste reads a whole TOML
  as well as `tt://` links.
- **Import confirmation**, for a link, a file, the clipboard and what a phone
  sent: the desktop's question, "Add a VPN server from this link?" or "…file?",
  then "Server: …", then where a phone sent it from, then what is wrong with
  it. As on the desktop, the question does not say the name the config would
  have: it comes with the link, and the server is what the config connects to.
  Cancel has the focus first, and the red Import answers
  only 400 ms after the question appears, so that an OK held on a remote
  cannot answer what a phone or a web page asked. A name already in the list
  (the desktop's collision, and only that: the same server under another name
  is added as a new config) offers Cancel, Add copy and Replace, and says the
  name of the one it would replace; Replace puts the new config where the old
  one was. Questions wait in line, and none takes
  another's place. Afterwards a toast says "Config added: …" and the page is
  where it was.
- **Editor**: the desktop's form on a card with ← and the title: Name, Server
  host, Address(es), Username and Password side by side (the password as dots),
  Protocol and DNS servers, Custom SNI and Client random, the Split profile
  (where a config's profile is chosen), the switches Allow IPv6, Skip
  certificate check and Anti-DPI, and the Certificate in a box 70 high with
  "Load from file…" (the system's file picker, so a TV with none says so; a
  file that is empty or over 1 MB says so, in the desktop's words), then
  Cancel and Save, which scroll with the form. The checks and their words are
  the desktop's. An edit changes the endpoint's values in the config and
  leaves the rest of the file as it was written (the tunnel's routes, the rules
  it brings, every other key). A file whose endpoint cannot be edited in place
  (an inline table, say) is kept as it is but for the endpoint, which the form's
  replaces, with the endpoint's other keys put back in it, as the desktop's
  keepWhatTheFormDoesNotShow keeps them; where even that cannot be shown to lose
  nothing, the save is refused with a message and the config stays as it was.
  A new config is what a link with those settings makes. A name left empty is
  the host's, cut to 50 characters; a name that was typed over 50 characters is
  refused ("The name is too long: 50 characters at most"). A field's placeholder goes while
  it has the focus, as the desktop's does, and the cursor is the text colour.
  Back, ← and Cancel ask "Discard unsaved changes?" when something was typed.
  The window refuses screenshots while it is open. A keyboard that comes up
  takes room from the card, which scrolls, and a turn of the screen keeps the
  card and what was typed in it. What an import left out is a toast.
- **Delete** and **Install update** ask with the same confirmation; Delete
  names the config, cut in the middle to 36 characters.
- **Mode** and **Theme** open a list under their value, with the current choice
  in the accent and a check mark.
- **App picker**: a search box and a row for each installed app, its name over
  its package. A tap adds that one app and closes it. The search box has the
  focus first, as the desktop's does, and on a phone the keyboard; Down goes on
  to the rows. The same list is under the field on Split, below the
  applications: what is typed there names the installed apps that match, up to
  24, above the field with three rows showing, and Enter or a tap adds the
  highlighted one (Up and Down move it, Back or Escape put the list away first).
- **Notices** are the desktop's toast: one at a time, a new one replacing the
  old, at the bottom or, while an overlay is open, at the top, for 3 to 10
  seconds according to its length, and a tap dismisses it. It is over the
  cards and under the popups and the confirmations, as the desktop's is. A storage that
  cannot be read or written is an amber banner at the top of Configs and in
  Settings, and a toast.

### Accepted differences from the desktop

The apps match qml/ screen for screen, in the light and the dark theme, in
English and in Russian; `UiTourDeviceTest` takes a picture of each screen for
the comparison. What differs on purpose, and is not to be changed to match it:

- **A TV's overscan.** The page keeps 48 clear of the sides and 27 of the top
  and bottom on a TV, as Android TV asks of an app, which some sets crop. The
  desktop has no such margin, so a TV's config rows start 48 further in, a
  card ends 27 above the bottom where the desktop's ends 12, and the empty
  Configs page's words sit 13 higher. Everything else is measured from the
  desktop's own numbers, which the margin is taken out of (the nav's padding,
  the speed tiles' 44).
- **The check for updates on launch is held to once in 12 hours.** The desktop
  checks silently 1.2 s after every launch; this app's activity starts far more
  often than a desktop program does, and everyone behind one VPN exit shares
  GitHub's unauthenticated API limit. The time of the last automatic check is
  kept, an attempt counts whatever it ends with, and **Check for updates** in
  Settings is never held to it.
- **Configs, add menu on a TV: Receive from phone**, first. A TV has no
  practical way to paste or to pick a file, so this is how a config gets onto
  it (see Receive from phone below). The desktop's menu has three items.
- **Split, the application field's placeholder**: "application name, then
  Enter". The desktop's says to drop an application there or paste its full
  path; an Android app has neither a drag-and-drop from a file manager nor a
  path to give, so its own words are right, and the app picker's rows are
  packages.
- **Receive from phone, the phone's page: the file field's own words** ("Choose
  File", "No file chosen"). The browser draws that control, in the phone's
  language and not the TV's. Putting the app's words on it needs a script to
  say which file was chosen, and the page's CSP forbids scripts.
- **Receive from phone, the phone's page: the paste box is 16 px monospace.**
  The desktop's certificate box, which it copies in everything else, uses 12 px.
  A browser on an iPhone zooms the page in on a field under 16 px when it takes
  the focus, so the page uses 16 and iPhones do not zoom in.
- **What Android draws itself, and the phone's page, are not in Inter.** The app
  is set in Inter, as the desktop is, but the notification, the Quick Settings
  tile's label and the system's own toasts are drawn by Android in its font, as
  the desktop's tray menu, file dialogs and notifications are in its system's.
  The page a phone is served keeps the phone's own font, because its CSP forbids
  loading one. The log, the certificate box and the TV's address are monospace,
  as the desktop's are.

### Receive from phone

The TV shows a QR code and an address such as
`http://192.168.1.23:41234/K7QM-4XHD`. A phone, tablet or laptop on the same
network opens it, with its camera or by typing the address into a browser, and
pastes a `tt://` link or a TOML file into the page there, or picks the file
itself: a TOML file, or a text file that holds a link. No app is needed on it.
The page is the app's own look, light or dark as the phone is, and after each
send it says so and shows the form again, so the phone can send another. The
TV shows each text it received, one at a time in the order they were sent,
with the address it came from, and adds it only once you confirm, as for any
other import; the card stays open under the question, with "Received: N" in
its corner, until you close it. The card has no button: its ← and Back close
it, as the desktop's cards that have a ← are closed, and a remote's first stop
on it is that arrow. Its hint is two short sentences (the warning about the
network is on the phone's page and below). A TOML file without a name of its own is named
after its file, as one picked on the TV is; a link names itself. If choosing a
file does nothing, as in the browser some QR code scanner apps open pages in,
open the address in Chrome or Safari instead.

- It listens only on the TV's own address on the home network (10/8,
  172.16/12 or 192.168/16, over Wi-Fi or Ethernet), never on a public,
  carrier-grade NAT or IPv6-only address, and only while its card is open:
  5 minutes at most from when the card opens, which no send extends, and not
  past Home or a screensaver. The screen stays on while the card is open, and
  Back, the arrow, the time, three wrong codes and leaving the app stop
  the port. What it had already received stays queued for your confirmation
  when the port closes in any of those ways but the last: leaving the app
  drops what is still waiting for its turn.
- It takes links and files until you close it, at most 10 waiting at a time.
  The address carries an 8-character code (40 bits, Crockford's base32), and
  3 sends with a wrong code, counted over all the sends of the session, stop
  it. Every address shows the same page, so only a send tests a code.
- A send is a form post of at most 512 KiB, a file included, that names the
  TV's address as its host. The page loads nothing and runs no script, and
  nothing about a transfer is logged. A send with nothing in it, with both
  pasted text and a file, with a file that is not UTF-8 text, or over 512 KiB,
  such as a photo chosen by mistake, shows the page again with a note; it is
  not a wrong code, and the TV goes on waiting. So does a send to the right
  code while 10 texts wait for your answer: the page tells the phone to wait
  and send again, and it is no wrong code either; each answer on the TV makes
  room for one more. Before it closes, the TV reads
  the rest of a send it refused as too large, for 2 seconds at most, so that
  the browser can show that page instead of a connection error.
- The link or file crosses the home network unencrypted: a browser has no way
  to encrypt a page on a local `http://` address. Someone already watching that
  network while the card is open could read the VPN password in a text, or the
  code in the address and send their own configuration, between your sends as
  well as before them. The TV's confirmation of each text shows the server and
  the sender's address, so check both every time, and use it only on a network
  you trust. The server, and the
  name of the config it would replace if one has that name, are shown there on
  one line, without characters that reverse or hide text, with at most two marks
  such as accents on a letter, and cut to 80 characters, so that nothing in
  them can cover the sender's address. The question does not say the name the
  new config would get, as the desktop's does not. What is saved is the name as
  the file has it. A text is checked before it is asked about, and that check is
  answered whatever else the TV is doing meanwhile (a tap on the logo, a
  config chosen, Replace building the VPN again for the text before it): no
  text is lost to another check, and none makes another stale.
- It does not work while the phone's VPN is on, over mobile data, or on a guest
  network that keeps its devices apart: if the page does not open, turn off the
  phone's VPN (the card does not say so; its hint is short on purpose).
- The text box on the phone's page is the desktop's certificate box, in 16 px
  monospace and not the desktop's 12: a browser on an iPhone zooms in on any
  field smaller than 16 px when it takes the focus. The page's viewport is not
  locked (`maximum-scale`) to stop that instead, because the lock would take
  pinch-zoom from Chrome on Android.

## Build and signing

Requires JDK 17, Android SDK 35, Build Tools 35.0.0, Bash, curl and Python 3.
From the repository root:

```sh
scripts/prepare-trusttunnel-android.sh
./android/gradlew -p android --no-daemon :app:testDebugUnitTest :app:lintDebug :app:assembleDebug :app:assembleRelease
```

The public AAR download needs no GitHub PAT. Its SHA-256 is checked during
preparation and every build:
`5398df61f74610d1d63819059bb596560feb51882e9ca78609df32fc4c7840f7`.
Gradle 8.11.1 wrapper and distribution checksums are pinned too.

Debug APK: `android/app/build/outputs/apk/debug/app-debug.apk` (debug signing).
Gradle builds release APKs unsigned, and never sees a keystore:
`scripts/sign-android-release.sh` signs them with Build Tools 35.0.0 alone
(`apksigner`, with `zipalign` and `aapt2` to check the result), in CI and
locally. For a signed release APK on your own machine, supply
`ANDROID_KEYSTORE_PATH`, `ANDROID_KEYSTORE_PASSWORD` and `ANDROID_KEY_PASSWORD`
(and `ANDROID_KEY_ALIAS` if the key is not `freetunnel`) through the
environment, then run:

```sh
scripts/build-android-release.sh
```

The version is set once, as `appVersion` in `android/app/build.gradle`
(`X.Y.Z`); `versionCode` is derived from it (`X*10000 + Y*100 + Z`). Official
releases are tagged `android-X.Y.Z`, and the tag must equal `appVersion`. The
preview was `0.9.0`; the first official release is `1.0.0`.

The script checks the required variables, runs the release tests and lint with
Gradle's environment cleared of them, builds the APK unsigned, and signs it
through `sign-android-release.sh`, which checks its alignment, package, version
and signature and that it is signed with the keystore's own certificate. The
signed APK is `android/app/build/outputs/apk/release/app-release.apk`. No AAB
is built: the project publishes to no app store. Never commit keystores or
credentials. The official project supplies its own signing identity;
compatibility with fork-signed builds is not required. Android itself enforces
signing identity on installed updates.

## Releasing

Official releases are tagged `android-X.Y.Z` and published by
[android-release.yml](../.github/workflows/android-release.yml). They are never
marked GitHub's latest release, which stays the desktop app's.

### One-time setup

The release keystore is the app's identity: Android installs an update only if
it is signed with the same key. If the keystore or its password is lost,
installed apps can never be updated again. Run these from the repository root,
in one shell, so that `KS` names the keystore throughout.

1. Generate the keystore outside the repository. keytool asks for a password;
   a PKCS12 keystore has one password, for the store and the key alike. The
   file is PKCS12 whatever its extension.

   ```sh
   KS=~/freetunnel-android-signing/freetunnel-release.jks
   mkdir -p -m 700 "$(dirname "$KS")"
   keytool -genkeypair -storetype PKCS12 -keystore "$KS" -alias freetunnel \
     -keyalg RSA -keysize 4096 -validity 10000 -dname 'CN=FreeTunnel, O=FreeTunnel'
   ```

2. Back up the keystore file and its password in two separate places, for
   example a password manager and an encrypted offline drive. Never commit
   it.
3. The `android-release` environment, which only `android-*` tags may deploy
   to, has existed since 2026-10-23. These commands create it again if it is
   ever deleted. Do that before the next tag: a workflow that names an
   environment that does not exist creates it, with no protection at all.

   ```sh
   echo '{"deployment_branch_policy": {"protected_branches": false, "custom_branch_policies": true}}' |
     gh api -X PUT repos/dimmmmmmmer/freetunnel/environments/android-release --input -
   gh api -X POST repos/dimmmmmmmer/freetunnel/environments/android-release/deployment-branch-policies \
     -f name='android-*' -f type=tag
   gh api repos/dimmmmmmmer/freetunnel/environments/android-release/deployment-branch-policies \
     --jq '.branch_policies[] | .name + " " + .type'
   ```

   The last command must print `android-* tag` and nothing else.

   The policy keeps the secrets from workflows run from a branch, but not from
   a run from an `android-*` tag, which runs the workflows of the commit the
   tag names. So only someone who may release can push such a tag: the
   repository's "Release tags" ruleset lets only admins create, move or delete
   `v*` and `android-*` tags. For a second look at every release, add a
   required reviewer under Settings → Environments → android-release. Each
   release then waits for that approval twice: once for the secrets check at
   the start, and once for the signing after CI.
4. Put the keystore and its password in the environment. `gh secret set` asks
   for each password, so neither lands in the shell history; give the same
   password twice.

   ```sh
   base64 < "$KS" | tr -d '\n' | gh secret set ANDROID_KEYSTORE_BASE64 --env android-release -R dimmmmmmmer/freetunnel
   gh secret set ANDROID_KEYSTORE_PASSWORD --env android-release -R dimmmmmmmer/freetunnel
   gh secret set ANDROID_KEY_PASSWORD --env android-release -R dimmmmmmmer/freetunnel
   ```

   The key's alias, `freetunnel`, is set in the workflow, not as a secret. The
   manifest is signed with `ED25519_SIGNING_KEY`, the desktop's repository
   secret, which is set already.
5. Pin the certificate in `android/release-certificate.sha256` and commit that
   file in a pull request. A release whose APK is signed by any other
   certificate stops before publishing. The app compiles the same pin in, and
   updates itself only when it is signed with that certificate.

   ```sh
   cer="$(mktemp)"
   keytool -exportcert -keystore "$KS" -alias freetunnel -file "$cer"
   sha256sum "$cer" | cut -d' ' -f1 > android/release-certificate.sha256
   keytool -printcert -file "$cer" | sed -n 's/^[[:space:]]*SHA256: //p' | tr -d : | tr A-F a-f
   rm "$cer"
   ```

   On macOS without `sha256sum`, use `shasum -a 256`. The fourth command
   prints the same value another way; check that it equals the file's. It is what `apksigner verify --print-certs` reports as the APK's
   certificate SHA-256 digest. Export to a file rather than piping keytool's
   output: with its output piped, keytool shows the password as you type it.

### Each release

1. In a pull request, bump `appVersion` in `android/app/build.gradle` (X at
   least 1, Y and Z at most 99), and give `android/CHANGELOG.md` a
   `## X.Y.Z` section that says what changed, for users: it becomes the
   release notes, down to the next heading.
2. Merge it once CI is green.
3. Tag the merged commit on an up-to-date `main` and push the tag:

   ```sh
   git switch main && git pull --ff-only
   git tag -a android-X.Y.Z -m "FreeTunnel for Android X.Y.Z"
   git push origin android-X.Y.Z
   ```

If a release stops before publishing, nothing is out yet. A missing or wrong
secret: fix it and use "Re-run failed jobs". Anything that needs a code change:
merge the fix, delete the tag (only an admin can, because of the ruleset) with
`git push origin :refs/tags/android-X.Y.Z` and `git tag -d android-X.Y.Z`, and
tag `main` again. Once a version is published, its files never change: release
the next version instead.

Before `android-1.0.0`: the `## Unreleased` section of `android/CHANGELOG.md`
still holds the preview's notes, partly written for reviewers ("No fork signing
identity or stable-release claim is part of this PR"), and a stray line that
reads only "preview", left from a split heading. Everything from `## 1.0.0` down to the next
heading is published, so rewrite all of it in the 1.0.0 release pull request.

The workflow checks the tag, that its commit is on `main`, that the version is
not published yet, the pin, the notes and that the secrets are set. It runs the
whole Android CI on the tagged commit, builds the APK unsigned, then signs it
in a job that runs only Build Tools, and checks its alignment, package, version
and signer against the pin. It writes `SHA256SUMS-android.txt`, signs it,
checks that signature against `include/core/ReleaseSigning.h`, and publishes
**FreeTunnel for Android X.Y.Z**. If any step fails or a secret is missing,
nothing is published. A version that is published already is never published
again: release a new one.

## Device checks

The automated device suite is currently validated on **API 35** (arm64 locally,
x86_64 in CI), and CI also runs it on **Android TV API 34** (x86); older
supported Android versions have not been validated by this suite. Run on a
dedicated emulator, with a fresh install:

```sh
./android/gradlew -p android :app:installDebug :app:installDebugAndroidTest
adb shell appops set org.freetunnel.android ACTIVATE_VPN allow
adb shell pm grant org.freetunnel.android android.permission.POST_NOTIFICATIONS
adb shell am instrument -w -e withoutInstallPermission true \
  -e class org.freetunnel.android.TvDeviceTest#installWithoutThePermissionKeepsTheUpdateReady \
  org.freetunnel.android.test/androidx.test.runner.AndroidJUnitRunner
adb shell appops set org.freetunnel.android REQUEST_INSTALL_PACKAGES allow
./android/gradlew -p android :app:connectedDebugAndroidTest \
  -Pandroid.testInstrumentationRunnerArguments.debugCertificate="$("$ANDROID_HOME"/build-tools/35.0.0/apksigner \
    verify --print-certs android/app/build/outputs/apk/debug/app-debug.apk |
    sed -n 's/^Signer #1 certificate SHA-256 digest: //p')"
```

The `debugCertificate` argument is apksigner's reading of the debug APK's
signer. `UpdateDeviceTest` checks the app's own reading against it, and
`TvDeviceTest` pins it so that Settings shows its update action as in an
official build. Both fail without it. The `am instrument` line runs the one
test that needs the app without the install permission. It has to come before
the permission is granted: Android stops an app that loses it, and the tests
with it. In the full run, that test is skipped.

[Android CI](../.github/workflows/android.yml) runs JVM tests, lint, builds and
emulator tests without lint baselines, the emulator tests on a phone and on an
Android TV image. `tests/test_android_tv_manifest.sh` checks what TV launchers
read from the built APK, including what the TrustTunnel AAR's manifest adds.
The device tests reach into the app by name (a control's key, a private field or
method, a string), and run half an hour into CI, so
`tests/test_android_device_tests.sh` (`scripts/check-android-device-tests.py`) reads the
tests and the app first, in the job that builds the app, and fails for a name one of them has
and the other does not, for an `onMain` block that runs another, for an `eventually()` without its message, and for a
picture dropped from the tour (below); that job also compiles the Kotlin tests
(`:app:compileDebugAndroidTestKotlin`).

**The UI tour.** `UiTourDeviceTest` goes through every screen of the app and saves a
picture of each, so that the parity with the desktop is a thing to look at. One pass is a
theme (dark, light, and dark in Russian) driven with a remote's keys, the focus put on a control and OK
pressed on it; the phone leg also goes through it by taps in each theme (`touch-dark`,
`touch-light`), with no focus to show. Each pass seeds three configs, sets `theme_mode` and recreates the
activity, and visits Home (disconnected, the selector and the logo focused, the dropdown, a toast, then
connecting, connected, disconnecting and the kill switch's blocked state (Connecting…), through a
local server that accepts and never answers, as `VpnDeviceTest` does), the nav with a focused
tile, Configs (the list with pings, a row, its icons, the add, export and delete menus, empty), the
editor (new, edit, the discard question), the import question and the Replace question,
Split (profiles and rules, after Recommended for Russia, the mode dropdown, the new profile
field, the Through VPN notice), the app picker, Settings (the top, the theme and language
dropdowns, the routes, the foot with the update row in each state the updater's test hooks reach),
Logs (on and off) and, on the TV leg only, Receive from phone, with the
text a phone sent asked about over the card. A state it cannot reach is written to the pass's
`index.txt` as `UNREACHABLE` and not drawn. The pages the TV serves to a phone are saved as `.html`
in English and Russian, as they are served and with each theme forced.

Pictures are taken with `UiAutomation.takeScreenshot()`, except while the window is `FLAG_SECURE`,
which the editor makes it and which a screenshot shows black: that is drawn from the activity's own
view into a bitmap, and the app's protection is left alone. The files go to the instrumentation
argument `additionalTestOutputDir`, which the Android Gradle Plugin copies to
`app/build/outputs/connected_android_test_additional_output` and CI uploads with the
device results (`android-device-results` for the phone, `android-tv-device-results` for the
TV); a copy is also written to `/data/local/tmp/uitour`, which the CI script pulls if Gradle
copied no picture (it makes the folders of its output before the run, so an empty one proves nothing). Each pass folder, `uitour/<phone|tv>-<pass>/`, has `NN-name.png`,
`index.txt` and a contact sheet, `index.html`.

On every screen the tour also asserts what the parity is about, and collects the findings so that
one does not cost the pictures after it: only the activity's own window is shown (no dialog, no
popup, nothing of another app), no view has a foreground (a ring), and the control that has the
focus is no wider than what it holds plus its padding, unless it is a row of a list, a field or a
box of text, a small cell or the Home logo (132 across, with no tile behind it). From Android 15 on it also reads the pixels
under the status and navigation bars (the foot is the keyboard's while one is up): each is the page's colour as it is drawn beside the
bar, dimmed once under a card and twice under a question over it, as the desktop's backdrops dim its whole window
(`TvDeviceTest.theSystemBarsDimWithACardAndComeBack` reads the same, in the window as the app draws it). The pass fails at its end if there is any,
and so does a VPN screen (connecting, connected, blocked) it could take and lost: only a VPN consent that was not granted
leaves those as `UNREACHABLE`.

Device tests exercise the real JNI adapter, TUN replacement/failure cleanup,
encrypted storage and exported-entry boundaries.
Their lifecycle TCP sink is not a working tunnel endpoint. Historical manual
acceptance used an API-35 emulator, `adb reverse` to a local macOS TrustTunnel
endpoint and TCP/HTTP2 only: [QA.md](QA.md). It does not establish physical-device,
remote-server, DNS or HTTP/3 coverage.

## License

FreeTunnel and TrustTunnel are Apache-2.0. See [LICENSE](../LICENSE) and
[TrustTunnelClient v1.1.7](https://github.com/TrustTunnel/TrustTunnelClient/tree/v1.1.7).

The APK also contains the libraries in the Android section of
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md), under their own
licenses. `app/build.gradle` copies the notices, `LICENSE` and the texts under
`licenses/common` and `licenses/android` into the APK's assets
(`assets/THIRD_PARTY_NOTICES.md`, `assets/LICENSE` and `assets/licenses/`; the desktop's texts are
left out), as the desktop ships them as files next to the program, and the app has no screen for
them. They are also in the repository. `scripts/check-apk-notices.sh` checks a built APK for
them. A new or bumped dependency in `app/build.gradle`, or a new
AAR, needs its entry there, or `scripts/check-third-party-notices.sh` fails.

The text is set in Inter (SIL OFL 1.1), as the desktop's is: Regular for all text and
Medium where the desktop's text is Medium, no other weight. The two files are in
`assets/fonts/inter`, which the desktop compiles in as well, and `assets/fonts/inter/SOURCE` says
where they come from. `app/build.gradle` checks them against `SHA256SUMS` and copies them into the
APK's font resources (`res/font/inter_regular.ttf` and `inter_medium.ttf`); `Fonts` is the one place
the code gets them from, and the theme names Regular for any text nothing sets.
`scripts/check-apk-data.sh` checks a built APK for the same bytes.

### Always-on VPN and Block connections without VPN

The app's kill switch protects while FreeTunnel runs. For protection while the
process or the tunnel is absent (after a crash, a force-stop or a reboot, before
the app starts), Android's own system settings do the rest. The app has no row
or link for them, as the desktop's Settings has none: turn them on in Android.

1. Open Android's Settings → Network & internet → VPN (on some devices
   Connections → More connection settings → VPN, or search Settings for "VPN").
2. Tap the gear next to FreeTunnel.
3. Turn on **Always-on VPN**, and then **Block connections without VPN**.

Always-on starts the selected config's VPN when the device starts and when
Android restarts the service; it is not a watchdog, and the system may not
restart the process after a crash at once. **Block connections without VPN**
(Android's lockdown) blocks all traffic while the VPN is down, which is what
the kill switch asks the system for; it also blocks internet access for the apps
that per-app routing excludes from the tunnel. Most Android TVs have no
Always-on VPN and no screen for it; there the kill switch protects only while
FreeTunnel runs.

### Failure handling and legacy rules

With kill switch enabled, a fatal native disconnect or a failed start after TUN
establishment retains the interface as a blocking guard. The notification offers
Reconnect and Disconnect; only an explicit stop, permission revocation or service
destruction releases the guard. Per-app exclusions and configured bypass routes
remain outside its scope. Use Android lockdown for protection while this app's
process is absent; lockdown also blocks apps excluded from the VPN.

While the guard holds, FreeTunnel keeps reconnecting by itself, with the desktop's
timing: 1 s, doubling to 30 s, ±20 %, without end. Each attempt starts a new core
on the retained TUN, with the settings that TUN was built from. A network that
appears starts the delays again at 1 s: the next attempt comes 1 s later, or 1 s
after the attempt in progress fails. The delays don't run while the phone sleeps,
and nothing wakes it. The app, the tile and the notification keep showing that
traffic is blocked until the connection is back. One alert, "Connection lost —
traffic is blocked", appears per episode, with Reconnect and Disconnect (Home has
neither, as the desktop's has not: it says Connecting…, and its logo disconnects). The log records each reason, and the reconnect,
once per episode. A connection that drops within 10 s of reconnecting by itself
continues the episode: the delay keeps growing and the alert comes back silently.
Opening the app clears an alert left by a process that was killed while blocked.
A failure that ends the connection (a refused config, a revoked permission) is a
notice in the shade that alerts while the app is away. While the app is on the
screen the notice is silent, with no pop-over to cover the navigation bar, and the
reason is said in the app's own toast, as the desktop's window does with an error.
An explicit Reconnect builds a new TUN from the current settings and hands traffic
over to it.

Each attempt also repeats two costs of the pinned TrustTunnel 1.1.7 core that
used to come once per explicit start. The core resolves a hostname given in
`addresses` as it starts, for up to 15 s, on the service's single worker, so a
Disconnect, Reconnect or settings change made during that lookup waits for it to
end. And each start keeps a copy of the configuration, a few KB, in native memory
until the process ends.

Native configuration checks run on a worker through a private isolated service.
Each validation process exits after unbinding, so checks cannot change the active
VPN's process-global native options or retain the adapter's temporary native copy
of credentials. Legacy unsupported saved destination rules are removed during
preparation and a notice appears on the routing screen. New rules remain strict.

### Authenticated Android updates

Only the official build updates itself. The build compiles in the pin from
`android/release-certificate.sha256`: without the file it has no pin, and a
malformed pin fails the build. The app checks once that it is signed by exactly
one certificate, the pinned one. Any other build (debug, CI, a fork, the 0.9.0
preview) says in Settings that it doesn't come from the official channel and
never checks or downloads, since Android would refuse its update anyway.

Settings' Maintenance row offers **Check for updates**, and then what each state of
the update calls for (see Settings and Logs). As the desktop does 1.2 s after it
starts, the app also checks by itself when it starts: silently (no "Checking…",
nothing said if it fails or finds nothing, only a dot on Settings' tile and the
line when an update is found), at most once every 12 hours (the desktop checks at
every launch; see the accepted differences), and never while a check, a download or
an install is under way or an update is already found, so a verified update
waiting to be installed is not disturbed. A click on **Check for updates** is
never held to the 12 hours. The client reads the original repository's paginated Releases API,
accepts only stable `android-X.Y.Z` tags newer than its installed version, and
pins all three asset URLs to that repository and tag. Desktop tags, drafts,
prereleases, foreign URLs and incomplete assets are ignored. The desktop
updater's `/releases/latest` endpoint is never used.

An Android release must provide `freetunnel-android-X.Y.Z.apk`,
`SHA256SUMS-android.txt`, and the raw 64-byte Ed25519 signature
`SHA256SUMS-android.txt.sig`. The manifest's first line must be `#version=X.Y.Z`,
followed by an unambiguous SHA-256 entry for the APK. The public verification key
is taken at build time from `include/core/ReleaseSigning.h`; no private signing
key is shipped. Verification uses Bouncy Castle's lightweight Ed25519 implementation
on all supported Android versions, including API 26.

Downloads stay in the app's private cache. A download cannot be stopped from the
app, as the desktop's cannot: the line does nothing while one comes down. A stalled
download stops within its 30-second read timeout, with the network error and the
↻ that tries again. As a backstop,
a transfer stops after 60 minutes for the APK and 2 for the other requests; the
500 MB size limit stays, since a release's limits stay with its users.
Signature, version binding, APK hash, package name, APK version, increasing
version code and the APK's signer, which must be the pinned certificate, are
checked before enabling Install. The APK is hashed again as it is written into
the PackageInstaller session, and nothing is committed if the bytes differ.
Android asks for the unknown-app-source permission where needed (where a TV or
OEM build has no such screen, a message says where the setting is) and requires
installation confirmation, then enforces APK signer compatibility.

Installing replaces the running app, and Android stops the VPN with it. If the
VPN is on, the app warns first, then reconnects by itself once the new version
starts, unless the VPN was disconnected or its permission revoked in between.
Android's own Always-on start, and this reconnect, only start a VPN that isn't
running, so a connection is never started twice; turning Always-on on while
connected therefore doesn't reconnect. The downloaded APK is deleted after the
update.

Cancelling Android's install dialog keeps the verified APK, and the update
line works again without a new download. If the dialog was lost, for
example after switching apps, the line starts the installation over.
A failure says whether the network, GitHub's rate limit, the security check or
Android's installer stopped it. GitHub limits unauthenticated checks per public
address. FreeTunnel's own traffic bypasses its VPN, so the limit is shared with
everyone on the same Wi-Fi or mobile network address.

Releases are published by the repository's release workflow, which never marks
an Android release GitHub's `latest`; see [Releasing](#releasing). This client
does not publish releases or configure secrets.
