# XDM operation and recovery

Read [installation](XDM.md) and [session selection](XDM-SESSION-CHOOSER.md).
Run administrative commands as root, from the native console or a trusted
remote connection. Preserve remote recovery access during display tests.

## Routine checks

```sh
/usr/X/bin/xdm-mode status
/usr/bin/ps -ef
tail -30 /usr/X/lib/xdm/xdm-errors
```

`Boot mode: xdm` is the persistent startup choice. SAF `ENABLED` describes
the running service, not the next boot's setting. Early STARTING can precede
a usable greeter. Verify actual display and input as well as process status.

## Login and logout

After the password, Enter uses the preference, F1 forces the default desktop,
F2 selects amiwm, F3 selects Open Look and F4 opens a failsafe terminal.
Fresh installation defaults to twm; a deployment may use tvtwm. Log out with
Exit X, amiwm Quit, Open Look Workspace Exit, or exit the failsafe shell.
Verify return to the greeter.

## Temporary text console (boot mode unchanged)

At the greeter press F5, then Return to confirm or Escape to cancel. Native
console login is still required. Alternatively, after logging out:

```sh
/usr/sbin/sacadm -k -p xdm
```

Wait for XDM/Xrtg to exit. Normal shutdown restores passthrough. Do not write
card registers or start a second server while Xrtg is running. Return with:

```sh
/usr/sbin/sacadm -s -p xdm
```

If F5 fails, inspect `/usr/X/lib/xdm/console.log` through the surviving
administrative connection. See [details](XDM-TEXT-CONSOLE-PLAN.md).

## Persistent manual or graphical startup

Disable graphical boot and stop XDM before starting manually:

```sh
/usr/X/bin/xdm-mode disable
/usr/X/bin/startxrtg
```

Proceed only if disable succeeds and no Xrtg remains. To retain a manual
session after closing a remote shell:

```sh
nohup /usr/X/bin/startxrtg > /tmp/xsession.log 2>&1 &
```

After ending the manual desktop, restore graphical boot and startup with:

```sh
/usr/X/bin/xdm-mode enable
```

The helper checks for a competing server and the required cookie seed.
The vendor `amixadm` Configure xdm menu is another administrative route.

## Unresponsive display or rollback

Do not send SIGKILL to Xrtg. Inspect processes from a surviving console or
remote connection, stop through SAF, and wait. If shutdown does not finish,
do not start another server or force passthrough writes. Escalate to system
recovery through the console; save other work before rebooting or power
cycling. A black display alone is not evidence of a system crash.

Use `reboot`, not an init-level change, for a controlled AMIX reboot. Verify
graphical boot and a login/logout cycle afterward.

Record each installation's actual backup directory in private operator notes.
Restore only intended configuration/binary files with proper permissions.
Never restore old authentication seeds or runtime cookies. See [XDM.md](XDM.md).
