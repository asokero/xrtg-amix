# XDM session selection

Optional desktop programs must be installed separately; they are not bundled
with Xrtg. See [installation](XDM.md) and [tested behavior](XDM-VALIDATION.md).

## Keys and preferences

Enter the username normally. After typing the password, submit with:

| Key | Action |
| --- | --- |
| Enter | User preference, then machine preference, then default desktop |
| F1 | Default desktop, ignoring preferences |
| F2 | amiwm for this login |
| F3 | Open Look for this login |
| F4 | Failsafe login-shell xterm |

Use a desktop function key **instead of Enter after the password**. These
actions finish the current field; they are not persistent menu selections.
Pressing F2 at the username field and later submitting with Enter does not
retain the F2 choice.

F5 opens an unauthenticated **text-console confirmation**. Return confirms
and Escape cancels. It does not log in or change boot mode. See
[text-console behavior](XDM-TEXT-CONSOLE-PLAN.md).

For a persistent user preference, put exactly one of `default`, `amiwm` or
`openlook` on the first line of `$HOME/.xrtg-session`. For example:

```sh
echo amiwm > "$HOME/.xrtg-session"
```

This replaces that preference file. Write `default` to force the default
desktop, or remove the file to inherit the machine preference. An empty
first line selects `default`. The file is read as text, never sourced.

For a machine preference, add or update `XRTG_SESSION=amiwm` (or `default`
or `openlook`) in `/usr/X/lib/xrtg/session.conf.local`. Preserve other local
settings. Explicit F1/F2/F3 overrides the user and machine preferences.
Preference changes apply on the next login without a reboot.

## Desktop lifecycle

`default` starts the executable named by `TWM` in the installed
`/usr/X/lib/xrtg/xrtg-session`, with the shared twmrc. The fresh-install
template uses `/usr/X/bin/twm`. A customized installation may use
`/usr/X/bin/tvtwm`; preserve that assignment during upgrades.

`amiwm` starts `/usr/local/bin/start-amiwm` without arguments. That separately
installed launcher must exec amiwm and use the intended configuration. Do not
pass twm's `-f`. The lasting session shell waits for amiwm; do not launch it
through a transient remote shell that can disrupt its modules.

`openlook` requires `/usr/X/bin/olwm` and `/usr/X/bin/olwsm`. The server's
font path includes Xol. The session starts olwm, initializes desktop clients,
then starts and waits for olwsm. Workspace Exit can leave olwm alive, so
after olwsm exits the script sends SIGTERM to its own olwm and returns to
XDM without an indefinite wait. A later olwm-only crash while olwsm remains
alive is not independently monitored.

Review user Open Look startup files for duplicate managers. The session does
not invoke olinit or source `.olinitrc` itself; the workspace manager may
have its own startup behavior. Do not copy a vendor template that launches
another olwm into an already managed session without reviewing it.

Only the default desktop loads the configured backdrop; amiwm and Open Look
skip that image load. The greeter retains its separately configured backdrop.
The session preserves the XDM environment and loads resources and a keyboard
map. The template starts xterm and xclock; retain customized clients and
geometries during upgrades. XDM resets the display when the session ends.

## Failure recovery

Missing programs, an unknown preference or early startup failure open a
failsafe xterm with an explanatory title. Early olwsm failure also sends
SIGTERM to the session's olwm. Startup checks establish process liveness,
not full desktop initialization. There is no automatic second-WM fallback.

Exit the failsafe shell to return to XDM, then use F1 or F4. Default-WM output
uses `/tmp/xdm-twm.log`; other managers inherit the session output. Do not
assume `.xsession-errors` exists on every AMIX setup. Shared XDM diagnostics
are in `/usr/X/lib/xdm/xdm-errors`.

## Greeter appearance

`patches/xdm-session-hints.patch` adds `xlogin*sessionHint` and
`xlogin*sessionHint2`. These optional single-line strings use the prompt font
and color and are centered below the reserved authentication-error row.
They do not affect input-field alignment. Keep `Login:` and `Password:`
short: the original widget aligns both inputs after the longest prompt.

The supplied layout is for 1280x720 with a 480x230 greeter at x=400, y=245.
Review dimensions when changing resolution or font. Explicit sizes do not
grow for long hint text. Hint changes need a refreshed greeter, not a binary
rebuild; the supporting patches must already be installed.

## Safe upgrades and tests

The full installer replaces the session template, including `TWM` and
application geometry. Back up affected files and apply only intended changes
to a customized deployment. Retain local configuration, desktop settings and
images. Keep actual backup locations in private operator notes.

Test each installed desktop's login/logout, menus and input, preference
precedence, failsafe, and F5 cancel/confirm. Check amiwm module lifetime and
Open Look Workspace Exit. Verify no orphan clients remain and a subsequent
default login works. Recheck X authorization after binary changes.

Run offline tests with `python3 -B -m unittest discover -s tests -v`. Stub
tests do not replace AMIX shell, widget, colormap, hardware or desktop-lifetime
testing. Read [the recovery guide](XDM-RUNBOOK.md) before deployment.
