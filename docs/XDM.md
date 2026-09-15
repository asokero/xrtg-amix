# XDM integration for Xrtg on AMIX

This directory provides a reproducible XDM login and Xrtg/twm session without
making the display manager inseparable from the manual `startxrtg` path.

The configuration is specific to the local `:0` Xrtg display at 1280x720.  It
uses the same dark palette as `session/Xdefaults` and `session/twmrc`.

Recorded tests and remaining acceptance checks: [XDM-VALIDATION.md](XDM-VALIDATION.md).

Desktop selection (twm/tvtwm, amiwm and Open Look), user preferences, and safe
upgrade instructions: [XDM-SESSION-CHOOSER.md](XDM-SESSION-CHOOSER.md).
Temporary native console access: [XDM-TEXT-CONSOLE-PLAN.md](XDM-TEXT-CONSOLE-PLAN.md).

## Files and runtime locations

| Repository file | Installed as | Purpose |
| --- | --- | --- |
| `session/xdm/xdm-config` | `/usr/X/lib/xdm/xdm-config` | XDM/SAF configuration |
| `session/xdm/Xservers` | `/usr/X/lib/xdm/Xservers` | Xrtg server command |
| `session/xdm/Xresources` | `/usr/X/lib/xdm/Xresources` | Login widget theme |
| `session/xdm/Xsetup_0` | `/usr/X/lib/xdm/Xsetup_0` | Login background and cursor |
| `session/xdm/Xaccess-local` | `/usr/X/lib/xdm/Xaccess-local` | Denies remote XDMCP hosts |
| `session/xrtg-session.sh` | `/usr/X/lib/xrtg/xrtg-session` | Desktop selection and session lifecycle |
| `session/{Xdefaults,twmrc}` | `/usr/X/lib/xrtg/` | Shared dark theme |
| `session/session.conf` | `/usr/X/lib/xrtg/session.conf` | Versioned defaults |
| `xdm-mode.sh` | `/usr/X/bin/xdm-mode` | Boot mode and live service control |
| `xdm-console.sh` | `/usr/X/bin/xdm-console` | Root-only temporary console helper |

`/usr/lib/X11` and `/usr/X/lib` refer to the same X11 tree on a standard AMIX
installation.

## Local backdrop

No backdrop image is distributed in this repository.  Pass an existing,
absolute image path as the second installer argument:

```sh
sh install-xdm.sh /path/to/xrtg-amix /path/to/local-backdrop.gif
```

The installer writes only this machine-local setting:

```sh
XRTG_BACKDROP='/path/to/local-backdrop.gif'
```

to `/usr/X/lib/xrtg/session.conf.local`. Supplying the backdrop argument
replaces that local file: preserve and reapply any other local overrides.
Omitting the argument preserves an existing local file.

With no local backdrop, the XDM screen and the default twm/tvtwm session use
`XRTG_ROOT_COLOR` from `session/session.conf`. The amiwm and Open Look session
paths do not load the shared backdrop; those desktops manage their own workspace
appearance. This does not affect the greeter backdrop.

Prepare large images at the exact screen size and in a cheap-to-decode format.
On a 68060, a 1280x720 256-colour GIF is much faster to display than a JPEG.

## Installation and non-boot test

Exit any running X session and stop a test XDM cleanly before installation:

```sh
sacadm -k -p xdm
sh install-xdm.sh /path/to/xrtg-amix /path/to/local-backdrop.gif
```

The installer creates a timestamped backup under `/root`, installs the files,
and deliberately leaves the XDM boot flag unchanged. The current configuration
requires the patched XDM binary described below; install that binary before
starting the service. A full configuration install replaces shared templates,
including the session script: retain any local tvtwm or application-layout changes.
Start a temporary test:

```sh
sacadm -s -p xdm
sacadm -l -p xdm
```

Verify a bad login, a successful login, twm/xterm/xclock appearance, F4
failsafe login, and at least three logout-to-greeter cycles.  Check
`/usr/X/lib/xdm/xdm-errors` for Xrtg generation resets and errors.

## Terminal initialization and optional bash

`XTerm*loginShell: true` loads the login shell's initialization. On this AMIX
installation `/etc/profile` sets `erase ^H` and `echoe`. Without that setup,
the XDM-launched sh had `-echoe`: Backspace moved the cursor but left the
deleted character visible. An existing terminal can be repaired with
`stty echoe`; the resource applies to new terminals. Manual startxrtg had
inherited the login terminal's settings.

If optional bash is installed at `/usr/public/bin/bash`, verify it runs before
selecting it for XDM session terminals. Append the following to the existing
machine-local `/usr/X/lib/xrtg/session.conf.local` (preserve the backdrop line):

```sh
SHELL=/usr/public/bin/bash
export SHELL
```

This takes effect at the next XDM login and leaves the account's system login
shell unchanged. To try bash immediately, run `/usr/public/bin/bash -login`.
For a directory prompt, set `PS1='\u@\h:\w\$ '` in `$HOME/.bashrc` and
source that file from `$HOME/.bash_profile`:

```sh
if [ -f "$HOME/.bashrc" ]; then
    . "$HOME/.bashrc"
fi
```

These bash settings are optional instructions, not installed by the XDM
installer. The tested X11R5 xterm sources implement bold, underline and
reverse video but not ANSI SGR text colors (30–37/40–47). Changing widget
foreground/background colors is supported; a multicolored prompt requires
additional terminal color support.

The versioned `XRTG_XMODMAP` default expands to `$HOME/.Xmodmap` in the
authenticated user's session and is loaded only if that file exists. A local
override can select another file, or set `XRTG_XMODMAP=` to disable loading.

## Why UDP 177 remains configured

The AMIX XDM port monitor did not remain healthy with `requestPort: 0`: after a
session it left an orphaned Xrtg and a black display.  Port 177 exercises the
normal XDMCP polling path and survived the tested session cycle.  The named
`Xaccess-local` database has no allowed hosts, so direct remote XDMCP service is
denied.

`DisplayManager._0.grabServer` is explicitly false. On the tested AMIX binary
that setting alone did not make the setup hook run. The accompanying
`patches/xdm-xrtg-setup.patch` moves setup before `InitGreet`. The patched
binary successfully ran setup and retained the backdrop, including after
logout. Its trace is `/usr/X/lib/xdm/Xsetup.log`.

The local display uses the explicit server authority file
`/usr/X/lib/xdm/Xrtg-auth`.  XDM creates it with its restrictive umask and
passes the path to `Xsetup_0` as `XAUTHORITY`, allowing `xv` to connect while
server authorization is enabled.  This runtime file contains the live cookie:
never commit it, include it in a backup, or copy it to another system.

## Authorization release gate

The production XDM binary in this setup contains the `XRTG_SECURE_COOKIE_V1`
generator.  It uses the root-owned 32-byte `/usr/X/lib/xdm/Xrtg-seed`, advances
that seed atomically for each server generation, and never stores the live
cookie in this repository.  The seed is machine-local and must be provisioned
separately; `xdm-mode enable` refuses to enable boot mode unless it is present.

Before exposing the machine beyond a trusted or isolated network, verify:

1. Cookies are not constant and differ across server generations.
2. A client with the session's authority file can connect.
3. A client without it is rejected.
4. No authority files, cookies or password data are committed to this repo.

Keep the machine on a trusted or isolated network during testing.

## Building and installing the patched binary

On AMIX with the original X11R5 client sources and static X libraries installed:

```sh
sh build-xdm-amix.sh /path/to/xrtg-amix /root/xdm-build-new
sh install-xdm-binary.sh /root/xdm-build-new/xdm /root/private-seed
```

The build directory must not already exist. The build copies the source tree,
applies the backdrop, secure-cookie, session-hints and text-console patches,
compiles with the installed GCC and links the static
X libraries explicitly. This avoids the obsolete `-fcombine-regs` option and
missing library archives in the original Makefile's relative paths. The build
requires the original, unpatched `session.c`, `genauth.c`, `Login.c` and
`LoginP.h`. The optional greeter hints are documented in
[XDM-SESSION-CHOOSER.md](XDM-SESSION-CHOOSER.md).

Provision `private-seed` as exactly 32 bytes from a modern operating system's
cryptographic random generator, transferred privately to this machine. Do not
reuse another machine's seed, publish it, or include it in a release package.
Subsequent binary installations can omit the seed argument to preserve the
current seed. The installer saves the previous binary under `/root` and leaves
boot mode unchanged. Configuration installation is a separate step.

The generator patch is specific to 32-bit AMIX `unsigned long`. Its current
failure behavior logs an emergency fallback when the seed is missing or
unsafe, rather than refusing XDM startup. `xdm-mode enable` checks the seed;
direct SAF startup bypasses that check. Keep the seed intact and inspect
`xdm-errors` for `XRTG_SECURE_COOKIE_V1` warnings after startup.

The seed and authority directory is made root-owned by the installers.
Never restore an old seed or runtime authority file from backups.

## Enabling and disabling the boot mode

Only after the visual, reset and authorization checks pass:

```sh
/usr/X/bin/xdm-mode enable
```

This removes the `x` flag from the XDM entry in `/etc/saf/_sactab`, disables
the competing `screens/con10` service, and asks SAF to start XDM.  It backs up
`_sactab` under `/root` before changing it.

Return to manual mode with:

```sh
/usr/X/bin/xdm-mode disable
/usr/X/bin/startxrtg
```

The disable action writes the safe next-boot state before stopping the live
service, uses `sacadm -k` rather than signals sent directly to Xrtg, and
re-enables `screens/con10`.  Never use `kill -9` on Xrtg; it can leave the AMIX
screen manager without working keyboard and mouse input until a cold boot.

The original AMIX interactive path remains valid as well: run `amixadm`, select
`Configure xdm`, then `Enable` or `Disable`.

## Full configuration restore

The installer prints its backup directory, for example:

```text
/root/xdm-production-backup-<timestamp>
```

Disable XDM first.  Restore `_sactab` and the files under the backup's `xdm/`,
`xrtg/`, and `bin/` directories.  Consult `absent-before-install` and remove
only files explicitly listed there.  Reboot before relying on the restored boot
mode.  Do not copy `.Xauthority` files between installations or restore them
from backups.

To restore the original binary, stop XDM and copy the selected backup's `xdm`
to `/usr/X/bin/xdm.new`, set owner root and mode 755, then rename it to
`/usr/X/bin/xdm`. Leave boot mode disabled when returning to the stock binary.
Use the backup path recorded by your installation, not one copied from another
machine's deployment notes. Never restore an old seed or live authority file.
