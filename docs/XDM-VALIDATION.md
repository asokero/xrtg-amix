# XDM validation summary

Hardware observations were collected on Amiga 3000-class hardware with AMIX
SVR4, a 68060 and VA2000. They describe tested behavior, not the present
state of a machine or a guarantee for other configurations. Per-machine
paths, process IDs, backup inventories and diaries belong in private notes.

## Confirmed on hardware

* Default desktop login, input, backdrop and logout back to the greeter.
* Automatic XDM startup after reboot and extended interactive use.
* amiwm login, input, Screens menu, live Keyboard/Launcher modules, workspace
  interactions, Quit Cancel and Quit Ok returning to XDM. Inspection after
  that logout found no remaining desktop or module processes.
* Open Look login with automatic olwsm startup, workspace menus and Exit
  returning to XDM after the lifecycle correction. The earlier olwm-only
  implementation was incomplete and is not the accepted behavior.
* F5 confirmation and transition to native text login, with XDM/Xrtg stopped
  and boot mode unchanged. The helper survived shutdown and completed without
  another passthrough utility. SAF could restart XDM afterward.
* The current two-row key layout and Escape cancellation of F5 confirmation.
* Authorized `xdpyinfo` succeeded and unauthenticated access failed after
  binary updates. Runtime cookies changed across tested restarts; cookie
  and seed contents were not recorded in repository artifacts.
* Configuration installation refused to proceed with XDM running. Mode
  helpers enabled/disabled graphical boot and refused a competing Xrtg.
* Native XDM builds and persistent-backup installations completed.

Testing was incremental, not one exhaustive run against the final key layout.
Current keys are F1 default, F2 amiwm, F3 Open Look, F4 failsafe and F5 text
console. Older key numbers must not be used as instructions.

## Offline checks

`python3 -B -m unittest discover -s tests -v` runs stub-based tests of session
selection, environment retention, backdrop gating, startup failures,
workspace-controlled logout, and console-helper failure/restore handling.
These do not prove real rendering, native shell behavior or hardware stability.

The cookie patch's SHA-256 implementation was previously checked against six
host reference inputs using 32-bit words. This is not a complete security
audit or a replacement for target authorization checks.

## Remaining acceptance checks

* Login/logout immediately after F5 cancellation on the final key layout.
* Final-layout failsafe and incorrect-password regression tests.
* Full F1--F4 regression after the last binary/key changes.
* Persistent preferences and launcher-failure UI recovery on hardware.
* Independent cleanup inspection after the final Open Look test.
* The user-relative keyboard-map default introduced during documentation
  cleanup has offline tests but has not been deployed or retested on AMIX.

AMIX shell testing found that an EXIT trap could lose the original status;
the mode helper avoids it. An init-level change was not a working reboot
substitute on the tested system; use `reboot` for a controlled boot test.
Hardware instability encountered during development was not independently
attributed to XDM. Successful restarts do not prove hardware stability.
