# Changelog

What changed for people using FreeTunnel. The release notes for each version are
built from the section below it, so this file is the description of the release —
write it before tagging. For the full commit history of a release, follow the
compare link at the bottom of its release notes.

## Unreleased

### Security

- **A freetunnel:// link asks before it turns the VPN off.** Any web page or app
  could open freetunnel://disconnect or freetunnel://toggle and turn the VPN off
  on the spot, kill switch and all, so that your traffic went out without it. The
  browser's question about opening FreeTunnel was the only thing in the way, and
  it can be told to stop asking. A link that would turn the VPN off now brings up
  the window and asks first, and the question is answered with a click, once it
  has been on screen for a moment: Return does not answer it, and nor does a
  click that lands as it appears. Connecting works as before.

  A Stream Deck button or a script that runs FreeTunnel with the command is not
  asked, as long as it runs the program rather than opening the link: on Windows
  `"C:\Program Files\FreeTunnel\FreeTunnel.exe" freetunnel://toggle`, on macOS
  `/Applications/FreeTunnel.app/Contents/MacOS/FreeTunnel freetunnel://toggle`
  (`open freetunnel://toggle` opens a link, and asks). "External control" in the
  README has the details.

  Linux: if AppImageLauncher or Gear Lever added the AppImage to your menu, add
  it again once you have updated. The menu entry made from an older AppImage
  hands links over as commands, and they are not asked about.

  The question a tt:// link asks before it adds a server is answered the same
  way now. A Return already held down when it came up could answer Import, and
  add a server the page had chosen.
- **A program running under your account could have FreeTunnel delete any file
  with administrator rights.** FreeTunnel's privileged part is handed a small
  file when it starts, and once it had read the file it deleted it — whatever
  file it had been handed. Other programs running as you can ask for that same
  administrator prompt themselves and hand it a file of their choosing; if you
  approved, that file was deleted, even one only an administrator may change.
  The privileged part now reads only a file of the kind FreeTunnel writes, and
  deletes nothing; FreeTunnel removes its own file itself. This applied on
  Windows, macOS and Linux. A FreeTunnel administrator prompt you did not
  expect, when you had not just connected, is one to refuse.
- **A config file can no longer have FreeTunnel's privileged part act on files,
  interfaces or kill-switch ports the file names.** FreeTunnel keeps the
  settings of an imported config that it has no editor for, so that saving the
  file loses nothing, and it passed them all to the VPN core, which runs with
  administrator rights. A few of them do more than shape the tunnel: a folder in
  which the core deleted and wrote files, ports that Windows let through the
  kill switch, the name of the tunnel interface, and on Linux an existing
  interface or network namespace to use. A config from someone you do not
  trust, or one that another program on your computer changed, could use them.
  These settings are now ignored; routes, DNS, the server and its certificate
  are read as before. If an imported config listed ports for the kill switch to
  let through, they are blocked now like everything else. On Windows, where
  such ports work, you can have them back, for example to reach this computer
  over Remote Desktop while the kill switch is on: turn on "Let the VPN config
  open ports" in Settings → Security. That setting is for every config at once:
  while it is on, each config you connect with, one you import later included,
  decides again which ports bypass the kill switch, so turn it on only if you
  trust every config you use. If a config named its tunnel interface, the
  interface gets the default name instead, so a firewall rule or script that
  looks for the old name needs the new one.
- **Linux: with the AppImage, another person with an account on your computer
  could have a program of theirs run with administrator rights.** To start its
  privileged part, the AppImage unpacks itself as administrator into a temporary
  folder. That folder was in the place every account shares, where someone else
  could prepare it in advance, and what they put in it ran the next time you
  connected. The AppImage now unpacks into a new folder that only the
  administrator can write to, every time, and removes it afterwards. The .deb
  was not affected. If other people have accounts on your computer and you use
  the AppImage, update.

  That covers an AppImage started the usual way. One started with
  --appimage-extract-and-run, as where FUSE is missing, is first unpacked by
  the AppImage itself, under your own account, into that shared place, and
  other accounts can still interfere with that copy; FreeTunnel cannot prevent
  it. Without FUSE, on a computer other people have accounts on, use the .deb.
- **An update has to say which version it is.** Since 1.1.8 FreeTunnel has
  checked that an update was signed for the version on offer, but an update from
  before 1.1.8, which does not say, was still accepted. Someone able to pass
  themselves off as GitHub's servers to your computer, which takes a security
  certificate your computer trusts, could have offered one of those old versions
  as a new one, and FreeTunnel would have installed it over the newer one you
  had. Such updates are now refused. Every release since 1.1.8 says its version,
  so no real update is turned away.
- **A config's password stays out of its file when a section name has a comment
  after it.** A config file may put a comment after the name of a section, as in
  `[endpoint] # main server`. FreeTunnel did not take such a line for a section
  name, so every time it moved the password into the system's credential store it
  wrote it straight back into the config file, in plain text, and it did so again
  before every connection. Only configs written or edited by hand that way were
  affected. The line is read as it should be now, and a config that was affected
  loses the copy of its password the next time you connect with it. A comment
  after `[listener.tun]` cost a config its own routes the same way, and the
  connection used the default ones. FreeTunnel has already written the defaults
  into such a config in place of its routes, so it does not get them back by
  itself: import it again from the original file.
- **Windows: the firewall no longer lets other computers connect to
  FreeTunnel.** The installer added a Windows Firewall rule that allowed incoming
  connections to FreeTunnel from any network, public Wi-Fi included, and to the
  part of it that runs with administrator rights as much as to the app.
  FreeTunnel takes no connections from other computers, so the rule only widened
  what could reach it. Installing this version removes it. The rule that lets
  FreeTunnel reach your server stays.

### Changed

- **The kill switch says what it covers.** Its line in Settings said it blocked
  traffic outside the VPN, which it does while FreeTunnel connects or brings
  back a connection that dropped. Nothing is blocked while the VPN is off or
  stopped by an error, and the block lifts for a moment whenever a session is
  built anew: when you switch configs, or change the excluded routes or the
  kill switch itself while connected. The line now reads "block traffic if the
  VPN drops", and the README says when it applies.
- **Settings says that its excluded routes come on top of a config's own.** Each
  config excludes some routes itself: one made in FreeTunnel or from a link keeps
  local networks and multicast outside the tunnel. The list in Settings is added
  to those, never used instead, so emptying it does not send the local network
  through the tunnel. The page showed the list as if it were the whole set; a
  line under it now says what it adds to.
- **A config file that turns off certificate checks says so when you import
  it.** A tt:// link that turns off server certificate verification says so
  before it adds the server, but a file that did the same was added without a
  word. The message that the config was added now says it too.
- **Windows: FreeTunnel goes around another VPN connected underneath it.** With
  a VPN whose adapter Windows counts as a network card, such as OpenVPN with its
  TAP adapter, which sends everything through itself without being the default
  route, FreeTunnel's connection to its server, and the server pings on the
  Configs page, went through that VPN. They now use the network adapter that
  carries Windows' default route, as TrustTunnel's own clients do, so a server
  that can only be reached through the other VPN can't be reached from FreeTunnel.
- **Linux: FreeTunnel runs on older distributions.** It was built on Ubuntu
  22.04 and needed its C library (glibc 2.35), so neither the .deb nor the
  AppImage started on Ubuntu 20.04, Debian 11 or RHEL 9 and its rebuilds. It is
  built on Ubuntu 20.04 now and needs glibc 2.31.
- **The AppImage is called freetunnel-x86_64.AppImage**, without "linux", as
  AppImages are for Linux anyway. A saved link to freetunnel-linux-x86_64.AppImage
  no longer finds the new version; FreeTunnel's own updater still does.

### Fixed

- **Disconnecting, switching servers or turning logging off no longer puts the
  connection at risk.** The VPN core closed its log file when a session ended
  but went on writing its messages to it, inside the privileged helper that holds
  the tunnel and the kill switch. Whatever it logged after a disconnect or a
  server switch went there, and so did everything in the next session if you
  had turned logging off in between. That could crash the helper and drop the
  connection. FreeTunnel now writes the core's messages itself, to a file it
  keeps open for as long as logging is on. With logging off they are now
  written nowhere; on macOS they used to go to a temporary file even then.
- **Windows: the VPN moves with you to another network adapter.** It kept its own
  traffic on the adapter it had connected over. Unplugging the network cable with
  Wi-Fi on, undocking a laptop or unplugging a phone used for USB tethering left it
  trying the old adapter for about a minute before it gave up and reconnected, and
  on a computer with Hyper-V or WSL that reconnect could pick one of their internal
  adapters and fail again. It now follows the adapter Windows uses for the
  internet, within a few seconds of a change, the way it already did on macOS and
  Linux, and takes that network's DNS servers along for the sites that bypass the
  VPN.
- **Windows: losing the network no longer ends the VPN.** With no network at all,
  the VPN spent a minute trying to reach its server, then stopped with an error
  about not detecting an active network interface, taking the kill switch's block
  down with it, and stayed stopped once the network came back. It now waits for a
  network, keeping the block if the kill switch is on, says that there is no
  network connection, and reconnects when one is back. A computer that dials its
  own internet connection, over PPPoE or a modem, still can't use the VPN on
  Windows, and now gets an error that says so instead of the one about the
  network interface; connecting through a router, Ethernet or Wi-Fi works.
- **Windows: the server pings on the Configs page work while connected on a
  computer with Hyper-V or WSL.** While the VPN was up they went out of the
  first network adapter Windows listed, which on such a computer can be one of
  their internal adapters, with no way out, so the servers showed as
  unreachable. They now go out of the adapter that carries Windows' default
  route, the one the VPN itself uses.
- **The core's messages reach the log as they happen.** They were held back
  until a few kilobytes had gathered or the connection ended, so a warning could
  arrive long after it mattered, and the last ones before a disconnect never
  arrived at all.
- **An excluded route or a split tunnelling rule of every address, such as
  `0.0.0.0/0` or `::/0`, is refused, and the message says why.** An excluded
  route goes around the tunnel, and one ending in /0 took all IPv4 or all IPv6
  traffic out of it while FreeTunnel still showed you as connected. The Split
  page took the same subnet as an address rule, and under "Bypass VPN" it did
  the same. One added with an earlier version is removed from Excluded routes
  or from its profile, and that traffic goes through the tunnel again.
- **"Through VPN" keeps the full tunnel when none of its programs can be used.**
  Settings can hold a program rule that cannot name a program on this computer,
  such as a Windows path in settings moved to a Mac or Linux, or one edited by
  hand. Such rules were ignored when routing but still counted as rules, so a
  profile with only those and no addresses sent everything outside the tunnel,
  without the notice that the full tunnel is kept. Now the full tunnel is kept,
  and the notice says the profile has no rules that can be used.
- **An address or subnet with `*.` or a dot in front of it is refused, and the
  message says why.** Rules such as `*.10.0.0.0/8` or `.1.2.3.4` were accepted and
  listed, but the VPN core takes `*.` only before a domain name, so they never
  matched anything. Under "Through VPN" the traffic they named went outside the
  tunnel, and with no other rule nothing went through it at all. Write the address
  or subnet on its own, as `10.0.0.0/8`. Rules like these that an earlier version
  saved are dropped from the list.
- **Windows: rules for Discord, Slack and similar apps apply to the app.** Apps
  like these, along with GitHub Desktop and others that install into
  AppData\Local next to an Update.exe, start through that updater, which runs
  the app from a folder named after its version. Picking one from the list or
  dropping its shortcut made a rule for the updater, and the app's own
  connections never matched it. A rule made by choosing the app's file itself
  stopped matching at the app's next update, when it moved to a new folder. The
  rule now names the app and keeps matching after updates. A rule made the old
  way, which shows as Update.exe, is changed to the app the first time this
  version starts, as long as the app is still installed beside it.
- **Split tunnelling changes take effect without reconnecting.** Adding or
  removing a domain, an address or a program on the Split tunnelling page,
  switching its mode, or turning split tunnelling on or off while connected
  rebuilt the whole tunnel: every open connection dropped, and with the kill
  switch on nothing was blocked while it came back. The change now reaches the
  running tunnel, which stays up with its kill switch. A program rule applies
  from the program's next connection. A change to the domains, the addresses or
  the mode restarts the connections that are open, so that they follow the new
  rules. The excluded routes in Settings still reconnect, as does the kill
  switch itself.
- **With the kill switch on, a server that cannot be reached no longer lets
  traffic out between attempts.** A first connect that failed ended the session
  after five tries, and FreeTunnel built a new one, round after round; each
  time, traffic went out unblocked between taking the old session down and
  bringing the new one up. The session now stays up and keeps trying, with
  traffic blocked, until it connects or you press Disconnect. Meanwhile the
  status reads "Connecting…", or "Waiting for network…" while there is no
  network, and why it is failing is shown once for each reason. A server that
  refuses the login or its certificate still ends the session, and FreeTunnel
  builds a new one as before, with the block lifting in between. A config that
  names its server by a domain name gets no session at all, and so no block,
  while that name cannot be looked up.
- **Saving a config in the editor keeps the config's own routing.** A config
  file can say which addresses go through the tunnel and which stay outside it,
  and can hold settings the editor has no field for. Saving it from the editor,
  even with nothing changed, or renaming it, replaced that routing with
  FreeTunnel's defaults and dropped the rest, so a config from a provider still
  connected but sent different traffic through the tunnel. The editor now
  changes only what it shows, and Save with nothing changed leaves the file as
  it is. A config that was saved this way has already lost those lines: import
  it again from the provider's file or link.
- **Config files written by hand are read the way the VPN core reads them.** A
  setting may be indented and its name may be in quotes, as in
  `"password" = "…"`; a section's name may be in quotes too, as in
  `["endpoint"]`; and a value may be in triple quotes, as in
  `password = """…"""`. FreeTunnel only read a setting written plainly at the
  left edge of its line, in a section named plainly, and read a value in triple
  quotes as empty. An indented address or a quoted `["endpoint"]` made importing
  the file fail, an indented certificate was left out of the connection and then
  out of the file, and an indented, quoted or triple-quoted password was never
  moved into the credential store, so the config did not connect. A setting was
  also taken from whichever section of the file had it first: a SOCKS
  listener's password could be used as the server's, and with a DNS list both
  at the top of the file and under `[endpoint]`, the editor showed the one the
  connection did not use. Each setting is read from its own section now.

  What a config imported before has already lost is not brought back. If it had
  indented, quoted or triple-quoted settings, a certificate in particular, they
  may be gone from it: import it again from the original file. If a SOCKS
  listener's password was taken for the server's, that password is now stored as
  the config's own: if the config does not connect, open it in the config editor
  and enter the server's username and password again.
- **A config made for a local SOCKS proxy connects again.** TrustTunnel's own
  client can run a config as a SOCKS proxy on your computer instead of a VPN
  tunnel. FreeTunnel always runs the tunnel, and since 1.1.8 it kept the proxy
  setting next to it; the VPN core refuses a config that asks for both, so such
  a config failed to connect. The proxy setting is now left out, and the config
  connects as a VPN without being imported again. If the proxy had a password of
  its own, an earlier version may have stored it as the server's when you tried
  to connect, as the entry above describes: if the config then fails to sign in,
  open it in the config editor and enter the server's password again.
- **A config that turns off the post-quantum key exchange keeps it off when it
  connects.** A config file may say `post_quantum_group_enabled = false`, but
  FreeTunnel turned it back on when it moved the password out of the file, and
  again for every connection, so the setting never took effect. The file's
  setting is used now, and saving the config in the config editor keeps it. A
  config imported before already has it turned back on in its file: import it
  again from the original file.
- **A link's client random reaches the VPN core in a form it can use.** A link
  can give the client random with a mask, as prefix/mask. FreeTunnel wrote the
  mask under a key of its own that the VPN core never reads, so the connection
  went out without it. It now goes to the core whole, and configs imported that
  way before are read back whole, without needing to be opened.

  The core can only use whole bytes of hex, at most 32 of them, before and after
  the slash, and FreeTunnel took whatever a link held: a value the core could
  not use went into the config, the connection quietly went without it, and the
  config editor could refuse to save the config until the value was changed.
  Such a link is now refused, with a message saying its client random is
  malformed. A mask with nothing before the slash is dropped on import, as an
  empty one after it already was. The config editor accepts a mask after a slash
  and checks by the same rule, so it no longer saves an odd number of digits, or
  more than 64. A config's share link passes its client random on as the
  connection uses it: where the mask is one the core cannot use, the link gives
  the part before the slash alone, as the connection does.
- **A config that could not be saved still connects as it did.** When saving an
  edited config failed at the last step, with "Could not write config", its new
  password had already been stored, while the file on disk was still the old
  config, perhaps with another username. It then failed to connect until it was
  saved again. The stored password is now put back as it was.
- **Renaming a config no longer deletes your own copy of it.** A config listed
  from a file outside FreeTunnel's folder, as a very early build could leave it,
  was moved into FreeTunnel's folder when renamed in the editor, and the original
  file was deleted. The original now stays where it is, as it already did when
  such a config was deleted from the list.
- **Choosing a certificate file the editor cannot use no longer empties the
  certificate field.** The editor loads a certificate only from your home,
  Downloads, Documents or Desktop folder, or the folder for temporary files, so
  not from another drive or a USB stick, and not from a link to another file or
  a file over 1 MB. Such a file emptied the field without a word, taking a
  certificate you had pasted there with it, and Save then wrote the config
  without one. The field now keeps what it holds, and a message says why the
  file was not loaded.
- **A long config name is explained instead of failing.** A config's name is
  also its file name, and a name longer than a file name may be, about 120
  Cyrillic letters on Linux and macOS, failed with "Could not write config",
  which said nothing about the name. Names now have a limit of 50 characters: the
  editor says when a name is longer, and a link or file with a longer name is
  imported under its first 50 characters. A config that already has a longer
  name keeps it, and the same link sent again still offers to replace it.
- **Pasting over the config editor no longer imports.** ⌘V or Ctrl+V on the
  Configs page adds a config from a link in the clipboard. It did the same with
  the config editor open over the page whenever the cursor was not in one of its
  fields, and put "No tt:// link in the clipboard", or a question about adding a
  server, on top of the config being edited. It now works only while the Configs
  page itself is in front.
- **Linux: Settings says so when there is no keyring to keep passwords in.**
  FreeTunnel keeps VPN passwords in the desktop's keyring, such as GNOME Keyring
  or KWallet, and nowhere else. On a desktop without one, every save of a config
  with a password failed, and the warning in Settings that explains why never
  appeared: any desktop session was taken for one with a keyring. FreeTunnel now
  asks for the keyring itself, so the warning shows, and asks again when an
  import fails as well as when a save does. It also no longer tells you to
  install secret-tool, which FreeTunnel does not need. When a keyring is there
  and still refused the password, as when its unlock prompt was closed, the
  message now says to unlock it and try again, rather than to install one.
- **macOS and Windows: a password that could not be saved is explained in your
  system's terms.** When the Keychain or Windows Credential Manager refused a VPN
  password, FreeTunnel told you to install gnome-keyring or KWallet, which are
  Linux programs. It now names the store that refused. On macOS it says to allow
  access to the Keychain when macOS asks for it. On Windows, where the refusal
  known to happen is a password longer than Credential Manager holds, it says
  that the limit is 2560 bytes.
- **Windows: an update waits for FreeTunnel to finish closing.** The installer
  asks a running FreeTunnel to close and waits for it, but it took the first
  refusal as "closed". The part of FreeTunnel that runs with administrator rights
  always refuses, as it has no window to close: it quits when the app does, once
  the connection is down. So the installer went on while FreeTunnel was still
  shutting down, and could stop on files still in use. It now waits until
  FreeTunnel has quit, for up to ten seconds, before closing it by force.
- **Windows: uninstalling closes FreeTunnel properly.** The uninstaller ended
  FreeTunnel by force, together with the part that runs with administrator
  rights, so a connection that was up was cut off instead of being closed, and
  the tray icon stayed behind until the mouse passed over it. It now asks
  FreeTunnel to quit and waits for it, as the installer does, and uses force only
  if FreeTunnel has not quit within ten seconds.
- **Windows: a silent install no longer goes into a folder that holds other
  files.** The installer refuses such a folder, because uninstalling FreeTunnel
  deletes its folder with everything in it. But the check sat on the page where
  you choose the folder, and an install run with /S shows no pages, so /D= could
  name any folder at all. A silent install into a folder that is neither empty
  nor an earlier FreeTunnel install now stops before installing anything, with
  exit code 2.
- **Windows: "Launch at system startup" could say it was on while doing
  nothing.** The setting records where FreeTunnel was when you switched it on,
  and the switch went on showing it as on after that copy was gone: a copy run
  from Downloads and deleted since, or one removed by an uninstall that ran
  under another Windows account, such as an administrator's, which leaves your
  own account's setting behind. Windows then had nothing to start. The switch
  shows it as off now when the program it would start is not there; switch it
  on again to have this copy start.
- **Starting FreeTunnel with "disconnect" keeps the VPN off.** With "Connect on
  startup" on, a script or a Stream Deck button that started FreeTunnel with
  freetunnel://disconnect while it was not running left it connected: the
  disconnect came before there was anything to disconnect, and the connection on
  startup followed a moment later. A disconnect now calls that connection off.
- **Two changes made close together no longer show "Off" mid-switch.**
  Switching configs, or changing the excluded routes or the kill switch, twice
  within five seconds while connected could show "Off" for a moment during the
  second change when the old tunnel was slow to go down.
- **Updates in Settings**
  - "Check for updates" clicked just after FreeTunnel started, while its own
    check was still under way, could offer the update a second time in the middle
    of downloading it, and taking that offer made the download fail. The click
    now waits for the check already under way.
  - Every downloaded update stayed in FreeTunnel's cache folder for good, 100 MB
    or more each time. Downloads are now cleared out the next time FreeTunnel
    starts, including those that earlier versions left there. That includes a
    .deb (Linux) or disk image (macOS) you have not installed yet, which
    FreeTunnel then offers again; a disk image that is still open stays open.
  - Linux: when FreeTunnel runs as an AppImage and the updated file could not be
    made runnable or started, FreeTunnel quit all the same, and none was left
    running. It now puts back the file it was started from, stays open and says
    so, and ↻ tries again. The old file is kept until the new one has started,
    and the download is removed once it is in place.
  - Linux: once FreeTunnel had updated itself as an AppImage, the next launch
    from the menu, or a tt:// link, could miss the new FreeTunnel and start a
    second one beside it. The new one is now the one they reach, from the update
    to this version on.
- **The Split page's notice about "Through VPN" with no rules follows a deleted
  config.** Deleting the config in use hands over to another one, which may use
  another profile, but the notice went on describing the deleted config until
  something else on the page changed: it could stay up over rules that were
  there, or stay away when the profile now in use had none.
- **A domain in another alphabet can be added in its xn-- spelling.** Such a
  domain has two spellings, пример.рф and xn--e1afmkfd.xn--p1ai, and the second
  is the one address bars, logs and certificates often show. The Split page
  refused it whenever the ending was spelled that way, as .рф is (xn--p1ai),
  while the same domain typed in its own alphabet was accepted. Both work now.
- **macOS: a failure is reported while FreeTunnel is hidden with ⌘H.** When
  something started from the menu-bar menu fails with the window away, a
  notification says so, and the error waits in the window until you open it.
  With the app hidden by ⌘H or Hide Others, the window still counted as open: no
  notification came, and the error was gone before you saw it. Hidden that way
  counts as away now.
- **Windows and macOS: in Russian, the reason an update check or download failed
  is in Russian too.** When checking for or downloading an update failed, the
  reason after «Ошибка сети:» or «Не удалось скачать:» stayed in English, as did
  the one in brackets when the VPN helper could not be reached. Those words come
  from Qt, and Qt's own Russian was shipped only with the Linux packages.
- **FreeTunnel no longer starts a second copy beside the one running.** A launch
  or a link that found FreeTunnel running but could not hand it over, because the
  password keyring was locked, started a whole second copy, and the two then
  drove the same VPN. Such a launch now gives way to the copy that is running and
  closes without showing anything, so a link it carried is not acted on: open the
  link again.
- **Windows and Linux: everyone on a shared computer gets one FreeTunnel of their
  own.** A second launch or a link is meant to go to the FreeTunnel already open,
  and it did only for the first person to start FreeTunnel on that computer. For
  anyone else, every launch and every link started another full copy. On Linux
  the place a launch looks for it is now in your session's own runtime folder,
  where another account can't put anything in the way.
- **Linux: an AppImage started without FUSE can update itself and start with the
  system.** Run with --appimage-extract-and-run, as on a system without FUSE,
  FreeTunnel did not recognise itself as an AppImage. "Launch at system startup"
  recorded a temporary copy that was gone once FreeTunnel quit, and updates
  offered the .deb and left installing it to you. It now finds its .AppImage
  file, and both start it with --appimage-extract-and-run again. The switch
  could show on while it did not work; after updating it shows off — turn it on
  again. To connect, its privileged part is now unpacked into /tmp, as for an
  AppImage started normally, so where /tmp does not allow running programs,
  pointing TMPDIR elsewhere no longer gets around that: use the .deb there. The
  .deb is also the one for a computer other people have accounts on (see
  Security).
- **Linux: FreeTunnel's memory no longer grows for as long as the VPN's
  privileged part runs.** Everything that part printed was kept in FreeTunnel's
  memory until it quit, and nothing ever read it. With logging off in Settings,
  that included the VPN core's own log. It is discarded now; the log FreeTunnel
  shows and saves is as before.
- **Linux without a password keyring: a link that arrives while FreeTunnel
  starts is handled, and so is every one after it.** A link or a second launch
  that came in while FreeTunnel was still starting was set aside and never
  handled. From then on each later one went unhandled until FreeTunnel was
  restarted. The same could happen on any computer whose password store would
  not keep the key that lets a second launch reach FreeTunnel.
- **Linux without a password keyring: FreeTunnel deletes its launch key when it
  quits.** Without a keyring, the key that lets a second launch reach the
  FreeTunnel already open is kept in a file only you can read. The file stayed
  behind after every quit, of no use to anyone; it is deleted on quit now.

## 1.2.2

### Changed

- **The VPN core is TrustTunnel 1.1.7**, up from 1.1.5, with newer DNS and TLS
  libraries.
  - It fixes a crash that could happen when an HTTP/3 connection attempt was
    cut short by the network going away, as on waking from sleep or switching
    Wi-Fi.
  - Linux: when NetworkManager briefly swapped default routes, as it does when
    Wi-Fi reconnects or its connectivity check changes its mind, the core could
    decide there was no network at all. With the kill switch on, FreeTunnel then
    waited for a network that was already there. The core keeps the real default
    route through such swaps now.
  - A DNS server given by name rather than address, such as
    udp://dns.example.com, was skipped, and a config with no other DNS server
    failed to connect. The name is now looked up through public DNS and the
    server is used. A name only your own network knows, such as router.lan, is
    not found this way: give the server's address instead.
  - A server whose certificate has an empty part in its name, as only unusual
    hand-made certificates do, used to connect. It is now refused during the
    handshake, even with "Skip certificate check" on, and the log speaks of a
    decode error (DECODE_ERROR).

### Fixed

- **DNS servers separated by spaces or semicolons work.** The config editor
  accepts "1.1.1.1 8.8.8.8" as well as "1.1.1.1, 8.8.8.8", but the list was
  handed to the VPN core split on commas only, as one server. Such a config
  failed to connect; with the new core it would have connected with no working
  DNS. Each server is its own entry now.
- **Boxes fit what they hold.**
  - A question such as "Add a VPN server from this link?" came in a card spread
    across the window around two short lines. Questions, and the messages that
    appear at the bottom of the window, are now as wide as their longest line.
  - The list of configs under the name on Home had a fixed width: wide around
    short names, while a long one was cut all the same. It is now as wide as the
    longest name, within the window. The menu for adding a config was as wide
    around its three short items; it now fits them.
  - Names were cut where the window had room for them: the active config on Home,
    and addresses, domains, profiles and applications on the Split and Settings
    pages. They are cut only where the window ends now. In the config list, the
    connected config's name was left little room by its ping, "connected" and the
    three buttons; the buttons take less of the row now, so the name has more.
  - Save and Cancel in the config editor grow to fit longer words, as in Russian.
  - A choice or a menu item longer than the window can show ends in "…". It used
    to stop at the edge, part of it hidden under the tick.
- **Links respond over their words.** "Check for updates", the log file's path
  and "Choose a file instead…" took a click anywhere on their row, far to the
  right of the words.
- **Messages at the bottom of the window**
  - A name with no spaces in it, such as a long file name, ran past both edges.
    It wraps now.
  - A message longer than three lines was cut, and the part left out was often
    the one saying what to do. Up to six lines show now, and a long message stays
    up longer.
  - On Home a message sat across the speed tiles and cut them in half. A short
    one now shows between the config name and the tiles, and a longer one covers
    the tiles instead. In a window of the default size, the longest messages, of
    five or six lines, can still reach up over the config name.
- **The config list on Home and the drop-down lists close when the window is
  resized.** They stayed open, away from what they belong to.
- **Updates in Settings**
  - The download arrow tipped onto its side under the pointer, and the retry
    arrow turned against its own direction. Now the download arrow, under the
    pointer, bends round clockwise into ↻; clicked, that ↻ turns while the update
    downloads, and if the pointer leaves without a click it straightens back into
    ↓. The retry arrow leans the way it goes.
  - Once an update was downloaded, the arrow and the line beside it both opened
    the release web page. After the download they offer nothing more: the
    installer, the disk image or the folder with the file has been opened. On
    Linux, if FreeTunnel could not open that folder, the line says where the file
    is and offers the release page.
  - When FreeTunnel runs as an AppImage and could not replace its own file, the
    row said so, but the arrow and the line still opened the release page. They
    now offer ↻, which downloads the update again and tries once more, for when
    the file can be written to.
  - After a failed download the row offers what can help. A release with nothing
    for your system offers its page (↗). One that is unsigned, or whose
    signature does not match, is checked for again rather than downloaded again.
    Other failures are retried as before.
  - A check or a download whose connection stopped answering showed "Checking…"
    or the same percentage for many minutes, and nothing in the row could be
    clicked meanwhile. It now gives up after 30 seconds without progress, and
    says the server stopped responding.
  - While an update downloads, the arrow turns, and while FreeTunnel checks for
    one, three dots hop in turn. A "…" stood beside the line instead, and it did
    not move whether the download did or not.
  - On Windows, and when FreeTunnel runs as an AppImage, installing an update
    closes FreeTunnel, and the VPN goes down with it. The line offering the
    update now says so.
  - Windows: if the downloaded installer could not be started, as when the
    administrator prompt was answered No, FreeTunnel quit all the same, and the
    VPN went down with it. It now stays open and says so, and ↻ tries again.
  - Changing the language announced an available update again.
- **Home**
  - The logo's pulse while connecting or disconnecting never moved. It pulses now.
  - Clicking the session time under the logo, or the space beside the logo,
    disconnected. Only the logo does now.
  - When a connection started or ended, the config name under the logo jumped
    while the logo glided. Both glide now.
  - With no configs, clicking the logo said "Select a config first", with none to
    select, and "Add a config" under it only switched to the Configs page. Both
    now open the menu for adding one. So does the + beside the config name, which
    also only switched to the Configs page.
- **Configs**
  - While a config was connecting, or FreeTunnel was switching to another one,
    nothing in the list said so. The active config now says "connecting…" until
    it says "connected".
  - The button that opens a config for editing showed ⋯, which suggests a menu.
    It shows a pencil now.
  - With no configs, "Add a config" looked like faint placeholder text and did
    not respond to the pointer, though it could be clicked. It is a link now, like
    the same words on Home.
- **Light theme: text on dark buttons is white again.** Since 1.2.1 the Save
  label in the config editor and the name on the chosen profile were black on
  dark grey. Hovering chips, hotkey fields and the Cancel buttons also showed
  almost no change; it does now.
- **Tray menu:** while connecting, the first item read "Connecting…" like a
  status, and choosing it cancelled the connection. It reads "Cancel connecting"
  now. While disconnecting it cannot be chosen, since it did nothing then.
- **Hover and pointer**
  - With a question, the config editor, "Add an application", the config list
    on Home, a drop-down list or a menu open, what lay behind still lit up under
    the pointer, though a click there never reached it. It no longer reacts.
  - A few buttons showed the hand pointer, where the others and the window's own
    buttons show the arrow. They show the arrow now; links keep the hand.
  - The back arrow in the config editor responds a little around it, as the one in
    "Add an application" does.
  - A hotkey field flickered as it started or stopped listening for keys.
  - The links at the bottom of Settings underline under the pointer, like the
    others.
  - The drop-down lists fade out as they fade in, and list rows fade their
    highlight everywhere.
- **Adding an application no longer freezes the window the first time.** On
  Windows, reading every Start Menu shortcut could stall the window for a while.
  The list is now read in the background, and the picker says it is looking.

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
  to be listed as "Germany___Frankfurt". Only characters that some system does
  not allow in a file name, such as / \ : * ? " < > |, are replaced, and
  characters that draw nothing are left out. Configs you already have keep their
  names.

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
    turns the connection off, or on when it was off.
  - Double-clicking the logo connected and at once cancelled. It now counts as
    one click.
  - After deleting the active config, the next start could make a different
    config active, and with "Connect on startup" connect to it.
  - Windows: without wintun.dll next to FreeTunnel.exe, Connect waited a minute
    and then blamed the administrator prompt. It now says wintun.dll is missing,
    before asking for administrator rights.
- **Tray and window**
  - Linux: clicking the tray icon did nothing. A click on KDE, or a double-click
    on GNOME, now brings the window back.
  - Linux: when the tray came up after FreeTunnel, as a panel can at login,
    there was no tray icon for the whole session, and the window's close button
    quit the app. The icon now appears once the tray does.
  - Linux: the tray menu dropped an underscore from config names. An underscore
    there now shows as a space, because desktops read underscores in menus in
    ways that no spelling of one gets right everywhere.
  - When something started from the tray failed while the window was hidden,
    the error showed only inside the hidden window, and was gone before you saw
    it. It is also sent as a notification now, and waits in the window until
    you open it.
  - Minimising a maximised window made it come back at normal size. It stays
    maximised.
  - macOS: freetunnel://toggle, connect and disconnect no longer bring back a
    window hidden in the menu bar.
  - macOS: «Show FreeTunnel» in the menu-bar menu did nothing after ⌘H or Hide
    Others.
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
  - Text that was cut short at the default window size fits, the built-in
    profile is «По умолчанию», and the discard question reads «Закрыть без
    сохранения?» with «Не сохранять» instead of «Отмена» beside «Отменить».
  - Connection errors, update errors, errors in tt:// links and the VPN helper's
    own messages were in English. A reason that a server or the VPN core gives
    is still passed on as it comes.
  - Switching the language now also changes the keyring warning, the update
    status and ping times, which kept the old language; a reason in an update
    error keeps the language it came in. The keyring warning on Linux was in
    English for a whole session started in Russian.
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
