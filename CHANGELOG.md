# Changelog

What changed for people using FreeTunnel. The release notes for each version are
built from the section below it, so this file is the description of the release —
write it before tagging. For the full commit history of a release, follow the
compare link at the bottom of its release notes.

## 1.2.1

### Security

- **Split tunnelling by application could send a connection the wrong way.** To
  tell which program a connection belongs to, the app looked its port number up
  in the system's list of open sockets. But one number can be in use by two
  programs at once: on different network addresses, or once over IPv4 and once
  over IPv6. This is not rare, because local services, containers and the system
  itself hold ports of their own. When the two collided, the app could answer
  about the wrong one.

  It went wrong both ways. A connection from a program you listed could be taken
  for "not one of yours": with the mode set to "Through VPN", it went outside the
  tunnel instead of through it, unless an address or domain on your list also
  covered where it was going. And a connection from some other program could be
  taken for yours, and follow your rule instead of the rest of your settings.

  Sockets are now told apart by address as well as by number. Two cases are still
  beyond what the app can see: two connections from the same address and port to
  different places, and, on Windows, an IPv6 socket that takes no IPv4 traffic.

### Changed

- **The window fits in with your system.**
  - **macOS 26 and 27:** the window buttons and corners now match other current
    apps, where they looked like an older macOS. The app icon has a dark
    rounded-square background of its own, so macOS no longer puts it inside a
    grey one. Dragging the window by its top edge works on a trackpad, and
    double-clicking there zooms or minimises it as set in System Settings.
  - **Windows:** the window no longer gives up what Windows gives an ordinary
    window. It has a shadow, and on Windows 11 rounded corners. It snaps when you
    drag it to an edge of the screen, and on Windows 11 hovering over the maximise
    button shows the Snap Layouts choices. Right-clicking the empty part of the
    top of the window, or pressing Alt+Space, opens the window menu. The buttons
    in the corner are drawn to match Windows 11's. This is new on Windows; if the
    window misbehaves, please open an issue.
  - **Linux:** on GNOME and Pop!_OS 22.04 the window buttons are the ones your
    desktop uses, in the places it puts them. On Pop!_OS 22.04 as it comes that
    is minimise and close, with no maximise, and on GNOME as it comes only close.
    Where there is no tray icon, closing still quits the app; Super+H hides the
    window instead. With Pop's theme or GNOME's standard look the buttons also
    look like the desktop's own. Double-, middle- and right-clicking the top of
    the window do what the desktop is set to do, except rolling the window up or
    maximising it in one direction only. A right-click opens the window manager's
    own menu where the window manager supports that, as GNOME's does. Change the
    layout in Tweaks and the window follows at once. On other desktops, such as
    KDE or XFCE, the app cannot read these settings, and keeps three buttons on
    the right.
- **Windows: the program says what it is.** The app and its installer now carry
  their name and version, and File Properties shows them. Before, the name and
  version fields there were empty.
- **FreeTunnel now needs macOS 12 or later.** 1.2.0 said it needed macOS 11, but
  it could not start there either: the Qt libraries it is built on need 12.
- **Global hotkeys are off until you turn them on.** The keys they came with,
  Ctrl+Shift+T, E and D (⌘⇧T, E and D on macOS), are what browsers and terminals
  use to reopen a closed tab and more, and a global hotkey takes its keys away
  from every other program. If you used them as they came, turn them back on in
  Settings → Hotkeys. If you had changed any of the keys, your hotkeys stay as
  they were.
- **A hotkey needs Ctrl, Alt or Meta** (⌘, ⌥ or ⌃ on macOS), unless it is one of
  F1–F12. Enter, Tab or a letter pressed in the field used to become a hotkey for
  the whole system. Backspace or Delete in the field now removes a hotkey, and
  one that FreeTunnel could not register, because another program or the desktop
  has it, shows in red.
- **Config names keep their spaces and punctuation.** "Germany · Frankfurt" used
  to be listed as "Germany___Frankfurt". Only characters a file name cannot hold
  are replaced. Configs you already have keep their names.

### Fixed

- **Linux: «Show FreeTunnel» in the tray menu did not bring the window back** on
  GNOME and Pop!_OS, and neither did starting FreeTunnel again while it was
  running. The window stayed minimised and only asked for attention: the request
  reached the app from the panel rather than as a click on the window, and the
  window manager took it for another program trying to steal focus. It now comes
  forward, and a window closed while maximised comes back maximised.
- **Linux: the "System" theme showed a light window on a dark desktop** on GNOME
  and Pop!_OS. It follows the desktop's light or dark setting now, including when
  you switch it while the app is open. Other desktops are followed the same way
  if they tell apps whether they are light or dark; not all of them do.
- **macOS 27: clicking the menu-bar icon could make the app quit.** This is a
  fault in the Qt version FreeTunnel is built on, and the app now works around
  it.
- **Connecting**
  - Switching to a config whose server was down showed no error. The app said
    "Connecting…" for as long as it kept retrying, and ignored the next config
    you picked. The error now shows, and the next pick switches.
  - Picking another config while a switch was still connecting could leave the
    tunnel on the first one while the app showed the second. The last pick wins.
  - Saving the config you are connecting with, say with its password corrected,
    starts the attempt again with what you saved. So does changing a rule or the
    kill switch while connecting. The attempt used to go on with the old
    settings until you reconnected.
  - A connection the VPN core refused at once could stay on "Connecting…" until
    you clicked.
  - Choosing the ticked config in the tray menu only removed its tick. It now
    turns the connection off, and on again.
  - Double-clicking the logo connected and at once cancelled. It now counts as
    one click.
  - After deleting the active config, the next start could make a different
    config active, and with "Connect on startup" connect to it.
  - Windows: without wintun.dll next to FreeTunnel.exe, Connect waited a minute
    and then blamed the administrator prompt. It now says wintun.dll is missing,
    before asking for administrator rights.
- **Tray and window**
  - Linux: clicking the tray icon did nothing; only «Show FreeTunnel» in its menu
    worked. A click on KDE, or a double-click on GNOME, now brings the window
    back.
  - Linux: when the tray came up after FreeTunnel, as a panel can at login,
    there was no tray icon for the whole session, and the window's close button
    quit the app. The icon now appears once the tray does.
  - Config names in the tray menu show as typed. On Linux an underscore went
    missing, and on every system an "&" could.
  - When something started from the tray failed while the window was hidden,
    the error showed only inside the hidden window, and was gone before you saw
    it. It is also sent as a notification now, and waits in the window until
    you open it.
  - Minimising a maximised window made it come back at normal size. It stays
    maximised.
  - freetunnel://toggle, connect and disconnect brought the window up each time,
    taking focus from what you were typing in. They now work without it. A
    second launch and a tt:// link still bring the window forward.
  - macOS: «Show FreeTunnel» and a second launch did nothing after ⌘H.
  - macOS: clicking the Dock icon took a zoomed or full-screen window out of zoom
    or full screen. A tt:// link left a window minimised to the Dock where it
    was, with the question about the import inside it.
  - macOS: the red button in full screen left an empty black screen. The window
    now leaves full screen first.
  - macOS: ⌘W hides the window like the red button, and ⌘M minimises it. Neither
    did anything.
  - Windows: a second launch or a tt:// link could leave the running window
    behind others.
- **Configs and questions**
  - Renaming a config only by letter case, "work" to "Work", gave "Work-2" on
    Windows and macOS.
  - When a config was added while the export menu or the delete question was
    open (from a link, a file or the clipboard), exporting or deleting could act
    on the config next to the one you chose. A copied deep link then carried
    that config's password.
  - Escape in the file dialog for a certificate or an application closed the
    editor or the application picker behind it, and on Linux could crash the
    app.
  - In the question about importing a link, a long server name was cut off at
    both ends, and clicking inside the question cancelled it. In Russian, three
    buttons ran into its edges.
  - An import question that appeared over the editor's "Discard unsaved
    changes?" did not get Return and Escape: Return discarded the edits behind
    it.
  - A hotkey field still recording under a question took the Return meant for
    the question as its hotkey. Questions now take the keyboard while they are
    open.
  - Tab moves between the fields of the config editor.
  - After a failed Save, the error covered the editor's Save button and took the
    next click. Over the editor, messages now show at the top.
  - On an empty Configs page, «Add a config» could not be clicked.
- **Settings, Split and Logs**
  - Logs: Clear left the old log on screen if some of it was selected, and a
    selection stopped the view from updating at all. With logging off, the page
    now says so instead of waiting for lines that will not come.
  - Settings: the update status is no longer cut short, and clicking it does
    what it offers: check, download or try again. During a check or a download it
    does nothing; it used to start a check in the middle of a download, which
    could then offer the same download a second time.
  - Settings: «Restore defaults» for excluded routes asks first, like «Clear all»
    next to it.
  - Split: the notice about "Through VPN" with no rules names the connected
    config and the profile it uses, which need not be the profile on screen. The
    message about it no longer pops up after each rule you add to a different
    profile.
  - Dark theme: the editor's Save button and the selected profile had white text
    on light grey, hard to read and looking disabled.
- **Russian**
  - Text cut short at the default window size fits, the built-in profile is
    «По умолчанию», and the discard question reads «Закрыть без сохранения?» with
    «Не сохранять» instead of «Отмена» beside «Отменить».
  - Connection errors, update errors, errors in tt:// links and messages from
    the VPN helper were in English.
  - Switching the language now changes everything at once. The keyring warning
    on Linux stayed in English for a whole session started in Russian.
  - Linux: file dialogs have Russian buttons.

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
