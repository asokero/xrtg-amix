# perf/

Performance baselines recorded with `x11perf-baseline.sh`.

Every optimisation phase on the `optimization` branch is committed together
with the measurement that justifies it. Without that, the speedup estimates
in the review stay estimates, and a regression on the other target machine
goes unnoticed until someone reports it.

## Recording a run

On AMIX, with Xrtg running:

```sh
sh /path/to/xrtg-amix/x11perf-baseline.sh /tmp/p.txt "phase0 z3 060"
cp /tmp/p.txt /path/to/xrtg-amix/perf/phase0-z3-060.txt
```

## Naming

```
<phase>-<bus>-<cpu>.txt        e.g. phase0-z3-060.txt
<phase>-<bus>-<cpu>-compat.txt run with -compat, for A/B comparison
```

Both target configurations matter and they are not interchangeable:

| Bus | CPU | Machine |
|-----|-----|---------|
| `z2` | `030` | stock A3000UX, VA2000 in Zorro II firmware |
| `z3` | `040` / `060` | ported kernel, VA2000 in Zorro III |

## Before committing a result

These files go into a public repository. The script deliberately records
`uname -srvm` rather than `uname -a` so the machine's node name stays out,
but check the header anyway -- it should contain no host names, user names
or network addresses. `DISPLAY` is written verbatim, so run with `:0` rather
than a `host:0` form.

## Repetitions

`x11perf` is run three times per test, not once. The saku26 demo measured
the same configuration and the same box size at 14, 15, 40, 46 and 47
ms/frame across separate runs of an unchanged binary, cause not yet
understood. A single number from this machine is not evidence; read the
spread, and treat a difference smaller than the spread as no difference.

## Comparing

```sh
x11perfcomp perf/phase0-z3-060.txt perf/phase2-z3-060.txt
```

The test list in `x11perf-baseline.sh` must stay identical between runs --
`x11perfcomp` compares line by line. If you change the list, start a new
baseline series rather than mixing old and new files.
