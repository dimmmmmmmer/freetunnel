# Changelog

What changed for people using FreeTunnel. The release notes for each version are
built from the section below it, so this file is the description of the release —
write it before tagging. For the full commit history of a release, follow the
compare link at the bottom of its release notes.

## 1.2.0

### Added

- **Split tunnelling by application.** Next to the addresses on the Split
  tunnelling page you can now name programs, and they follow whichever way round
  the split is already set: with "all traffic except the listed" they leave the
  tunnel, with "only the listed" they are the only ones inside it. Add a program
  from the list of what is installed on the machine, by dragging its icon onto
  the page, or by picking the file yourself.

  On macOS a rule covers the application rather than one file inside it. A
  browser does its networking from a separate helper process, so a rule naming
  the program you actually picked would otherwise never match a single one of
  its connections.

  Nothing extra has to be installed for this: no driver, no system extension, no
  permission dialog. Which program a connection belongs to is worked out by
  asking the operating system who owns the socket it came from, on the
  connection itself, so a rule added while the VPN is up applies to the next
  connection rather than the next session.

  Programs belong to the profile, the same as the addresses above them, so a set
  for work and a set for everything else switch together. Any list you had before
  this release becomes the starting list of every profile you already had, which
  is what it used to mean.

### Security

- If you chose **HTTP/3** as the protocol for a config, the server's certificate
  was not being checked at all. The connection was made before the check could
  be armed, so none of the three things that normally establish trust ran: not
  the system's list of certificate authorities, not a certificate you imported
  with the config, not even the "skip verification" switch. In practice that
  means someone positioned between you and the server — on a shared Wi-Fi
  network, or at your provider — could have presented any certificate, and the
  app would have accepted it and sent your traffic and password through them.
  This is fixed in the VPN core this release moves to.

  Configs on HTTP/2 were never affected, and HTTP/2 is what new configs use
  unless you change it. If you use HTTP/3, please update.

  One consequence to expect: a self-signed server on HTTP/3 will now be refused
  with a certificate error if the config does not carry the server's certificate
  or have verification switched off. Re-import the config from its link — the
  certificate travels inside it — or turn verification off deliberately.

- **Windows: a program running as another user of this computer could give
  FreeTunnel orders.** A second launch forwards `freetunnel://` and `tt://` links
  to the window already open, over a local channel that is supposed to accept
  only connections from you. On the receiving side that check was asking about
  the wrong end of the connection, so it described this very process and could
  never refuse anything. Anything that reached the channel could switch the VPN
  on or off, or hand it a server link to import.

### Fixed

- **The Linux `.deb` could not be installed on a current system.** It asked for a
  package Debian 13 and Ubuntu 25.04 no longer ship, with a fallback name that
  has never existed in either. It asks for `pkexec` now — the program that
  actually starts the privileged part.
- **Updating on Linux could leave nothing running.** The replacement was started
  while the copy being replaced still held the channel above, so it handed itself
  over to a program that was in the middle of quitting and then exited. Both
  disappeared, and the app had to be started again by hand.
- **Editing a server could save over a different one.** The editor remembered
  which row of the list it opened on, and a config imported meanwhile is added to
  the top and moves every row down. Saving after that wrote the form over
  whichever server had taken that place — silently, and leaving the original
  behind as a duplicate. It remembers the file now, and says so plainly if that
  file has been deleted in the meantime.
- **A config file could be rewritten into something nothing can read.** Anything
  in it this app does not itself write — a setting from your provider spread over
  several lines — was cut off after its first line whenever the file was saved
  back, which happens on import and on every connect. A certificate written in
  any form other than the one this app uses was read as empty and then written
  back as empty, so a pinned server certificate was lost from the file.
  Configurations that use single quotes, which are perfectly ordinary, were read
  as blank entirely.
- **Sharing a config as a link dropped all but the first certificate** when it
  pinned a chain rather than a single one.
- **The app could sit on "Connecting…" for good.** If something else on the
  computer was already using the port the privileged helper picks, the app would
  connect to it, wait for an answer that was never coming, and show nothing —
  after you had already entered your administrator password. It now gives up and
  says what happened.
- **macOS: "Start at login" could say it was on while doing nothing.** The entry
  records where the app was when you switched it on, and on macOS that is usually
  not where it ends up — people run it from the disk image or from Downloads and
  move it to Applications afterwards. Nothing rewrote it, and the switch went on
  claiming to be on for good.
- **Windows: the Start menu and desktop shortcuts went into one account.** The
  program is installed for the whole machine, but its shortcuts landed in the
  profile of whichever account Windows ran the installer as — often not the one
  that asked for it. They are made for all users now, and an upgrade clears the
  old ones rather than leaving a second copy of each.
- **Settings no longer freezes while it opens.** It was checking for a password
  keyring three times over, each check stopping the interface while it ran. It
  asks once; and the notice about a missing keyring now goes away once one is
  there, instead of staying until the app is restarted.
- HTTP/3: the app could keep saying it was connected after the connection had
  actually died, leaving traffic going nowhere until you reconnected by hand.
- HTTP/3: a config listing several server addresses could crash the privileged
  helper while connecting, which ends the connection attempt.
- HTTP/3: each connection attempt to a server with several addresses leaked a
  network handle. Enough of them and the app would start reconnecting on its own.

## 1.1.9

### Fixed

- macOS: FreeTunnel no longer dies with a crash if you close it while it is
  getting ready to connect. That is the moment macOS asks whether the app may
  read the password saved for this config, and quitting while the system's
  prompt was on screen ended the app abruptly instead of letting it close.

### Changed

- The privileged helper now shuts down in an orderly way when something asks it
  to stop — logging out, or shutting the machine down with the VPN still on. It
  takes the tunnel down on the way out. Before it was stopped where it stood,
  and the routes and DNS it had set up were left for whatever came next to
  clear away.

## 1.1.8

### Fixed

- macOS: clicking the menu-bar icon no longer opens the main window. It only
  shows the menu, as it always should have.
- macOS: clicking the Dock icon brings the window back after you close it. This
  stopped working in 1.1.7 and did nothing at all until now.
- Windows: installing an update over a running FreeTunnel no longer fails partway
  through. The installer now closes it first — cleanly, so your connection is
  taken down properly rather than cut — and this works whether you update from
  inside the app or run the installer yourself.
- Windows: a link opened while FreeTunnel is already running no longer does
  nothing. Longer links were being dropped on their way to the running copy.
- Windows: uninstalling now removes the "launch at startup" entry. It used to be
  left behind, so Windows kept trying to start a program that was gone.
- Windows: the installer is no longer garbled when shown in Russian.
- "Through VPN" with no rules no longer sends all of your traffic outside the
  tunnel while showing you as connected. The full tunnel stays on, and the app
  says why.
- A split-tunnel rule written as ".example.com" now works. It was accepted,
  listed back to you, and quietly never applied.
- The kill switch is no longer taken down while the app reconnects after a
  network change or sleep.
- Linux: the update button now installs the update, or tells you it cannot.
  It used to report success and do nothing.
- Linux: "launch at system startup" now works for AppImage builds, and the
  switch no longer reports "on" when the entry it wrote can no longer start
  anything.
- Linux: global hotkeys are correctly reported as unavailable on Wayland,
  including when the app runs through XWayland. The previous advice on how to
  make them work led into exactly the case where they silently do not.
- Pressing Enter now confirms a dialog, and Escape reliably cancels one — on some
  screens Escape did nothing at all.
- Update checks no longer skip a release: 1.2.0-rc10 is now correctly newer than
  1.2.0-rc9.
- The Downloads/upgrade path no longer offers a package that cannot run on your
  system: the Linux package now states the system version it actually needs.

### Security

- Linux: the elevated helper is now started from a path the kernel confirms,
  not one named by environment variables. A process able to set the app's
  environment could previously have the administrator prompt run a file of its
  choosing.
- A link that adds a server now shows which server it is, and warns when the name
  mixes alphabets — the trick behind a name that looks like one you already
  trust.
- An update is now verified to belong to the release being offered, not merely to
  be signed by us. Older releases can no longer be replayed to hold you back on
  an earlier build.

## 1.1.7

### Fixed

- The window no longer freezes while your computer asks permission to use your
  saved password. It used to lock up, cursor and all, when you pressed Connect,
  and when you opened, saved, copied, exported or deleted a server.
- Cancelling, or switching to another server, during that wait no longer starts
  a connection you did not ask for.
- The app no longer crashes when you quit, disconnect or switch servers while a
  connection is still being set up.
- Connect is no longer silently ignored after you cancel a connection attempt.
- Changing a setting such as the kill switch during a long connection attempt no
  longer drops a working connection.
- Adding a server while connected no longer shows you as connected to the new
  one while your traffic still goes through the old one.
- Editing a server no longer destroys it when its password cannot be saved.
- Your saved servers no longer disappear if the app is interrupted while saving.
  On Windows, a server list damaged by a crash or a full disk is now repaired
  instead of being rebuilt from scratch at every launch.
- Adding two servers with the same name in quick succession no longer overwrites
  one of them, and no longer puts a long number in its name.
- A link naming a server you already have now asks whether to replace it or keep
  both, instead of deciding for you.
- A failed check for a new version now shows a retry button and the real reason
  it failed. It used to show what looked like a download for an update that did
  not exist, and pressing it started a download instead of checking again.
- Escape closes the topmost window again, even with a drop-down open.
- Ctrl+Q / Cmd+Q can be recorded as a shortcut without quitting the app on the
  spot.
- Delete, export and scrolling in the server list keep working after a server
  arrives in the background.
- Connection details appear in the log again, and Clear logs empties it
  immediately instead of waiting until you disconnect.
- In Russian, the import confirmation, the update-failure notice and the kill
  switch label are translated.

### Security

- The VPN could stay fully active, with your traffic still routed through it,
  while the app showed Disconnected — if you switched it off exactly as a
  connection was finishing.
- Another program on your computer could impersonate the app's privileged part
  while you were being asked for your administrator password, take your VPN
  password, and then show you as connected while nothing was protected.
- A server link could replace a server you already had without asking, or hand
  your existing password to the server named in the link.
- Updates are verified before they are installed, so the app cannot be made to
  run a tampered version — or an older, deliberately vulnerable one.
- On Windows, uninstalling could delete everything else in the folder you had
  installed into. The installer now refuses a folder that already holds other
  files.
- On Linux, the app tells you when your system has no password keyring, instead
  of behaving as though your VPN password had been stored securely. Its settings
  folder is no longer readable by other people using the same computer.

### Changed

- The confirmation shown for a server link now says what the link actually does.
  It no longer stacks two questions on top of each other, drops the generic
  advice to only add servers you trust, and warns about the one thing you cannot
  check yourself: when the link turns off verification of the server's identity.
