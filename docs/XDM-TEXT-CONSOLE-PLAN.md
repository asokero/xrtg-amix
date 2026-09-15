# Temporary native text console

F5 at the greeter opens a confirmation. **Return confirms; Escape cancels.**
Confirmation stops graphical login and returns to the native text console.
It does not authenticate a user: normal console login is still required.
Boot-time XDM configuration is unchanged.

This intentionally allows anyone with access to the greeter to stop it.
Disable the option with `xlogin*allowTextConsole: false` in Xresources if
that policy is unsuitable, then refresh the greeter through a controlled
restart. The binary defaults to false; the supplied template enables it.

## Returning to graphical login

From a root console or a surviving administrative remote connection:

```sh
/usr/sbin/sacadm -s -p xdm
```

Do not use `xdm-mode disable` for a temporary switch: it also changes the
next boot's default. A manual equivalent of F5 is `sacadm -k -p xdm`, after
logging out of any desktop. Wait for XDM and Xrtg to exit.

## Implementation and privileges

`patches/xdm-text-console.patch` adds fixed `console-request`,
`console-confirm` and `console-cancel` widget actions, independent of the
authenticated session-argument path. Opening confirmation clears entered
credentials and the session argument. Cancellation returns to the username
field. Ordinary login/editing, session-selection and access-toggle actions
are blocked while confirming/stopping. Repeated F5 cannot launch additional
helpers or confirm the action.

The launcher checks that `/usr/X/bin/xdm-console` is a regular root-owned
file with no group/other write permission. Its child creates a new session,
ignores SIGHUP, resets termination/child signal handlers, closes inherited
descriptors, changes to `/`, and execs `/bin/sh` with a fixed script path and
minimal PATH/HOME environment. No user-supplied command or input is passed
to a privileged shell. No setuid client or network control service is added.
Input is `/dev/null`; output goes to `/usr/X/lib/xdm/console.log` in the
root-controlled XDM directory.

The widget polls the exact child with `waitpid(WNOHANG)` and does not release
login while it is alive. If it finishes but the greeter remains, a failure
view offers Escape back to login. A hanging helper requires remote
administration: elapsed time is not proof of cancellation. Confirmation text
is currently compiled in; the normal shortcut footer is configurable.

## Helper behavior

The installer places `xdm-console.sh` at `/usr/X/bin/xdm-console`, root-owned
and mode 700. It requests `sacadm -k -p xdm`, then checks for remaining XDM
or Xrtg processes with up to 30 one-second retries. Query errors, SAF errors
or timeout are failures, not reasons to force-kill Xrtg.

Normal Xrtg shutdown restores VA2000 passthrough via `va2000CloseHW()`. This
was confirmed on hardware, including F5 shutdown, so the greeter does not
run another restoration utility. The helper accepts an optional trusted
absolute restore-program path for explicit root administration only. It
refuses to run it while XDM/Xrtg is present. Never accept that argument from
an unauthenticated user.

The script itself does not detach; the C launcher does. For manual diagnosis,
invoke it from a surviving remote root connection, not an X terminal that
the stop request will terminate.

## Installation, rollback and testing

Follow [the installation guide](XDM.md). Install the helper before enabling
F5. Preserve the old binary and Xresources and record whether the helper
previously existed, using a private persistent backup. A full configuration
reinstall is not necessary for a narrow upgrade.

To revert, log out, stop XDM and verify Xrtg has exited. Restore the old
binary and Xresources with their original permissions. A new helper can be
moved into the private backup rather than deleted. Restart through SAF.
Never roll back seed state or copy runtime authority cookies.

Hardware testing confirmed the F5 question, native text login after confirm,
unchanged boot mode, helper completion after XDM shutdown, restart through
SAF and Escape cancellation. Post-cancel login/logout and forced launcher
failure recovery have not been explicitly recorded as accepted. Recheck
these and F1--F4 after changes.

Offline tests cover helper privilege/path checks, stop/query errors, timeout
and optional restore behavior. They do not exercise the actual widget or
detached AMIX process lifecycle. See [validation](XDM-VALIDATION.md) and
[recovery](XDM-RUNBOOK.md).
