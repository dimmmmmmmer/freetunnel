# Changelog

What changed for people using FreeTunnel. The release notes for each version are
built from the section below it, so this file is the description of the release —
write it before tagging. For the full commit history of a release, follow the
compare link at the bottom of its release notes.

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
