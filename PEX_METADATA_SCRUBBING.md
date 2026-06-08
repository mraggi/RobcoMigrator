# Scrubbing identity metadata from compiled `.pex` files

**Problem:** Every compiled Fallout 4 Papyrus `.pex` has a header storing three
strings that leak the build machine's identity. The game prints these in
Papyrus logs / stack traces, so they end up in *users'* logs:

| Field | Typical leaked value | Source |
|---|---|---|
| `sourceFileName` | `Z:\home\<user>\projects\...\Foo.psc` | the absolute path compiled |
| `userName` | `<user>` | OS user (Wine maps the Unix user) |
| `machineName` | `<PC>` | computer name |

These are part of the standard Papyrus format; Caprica and Bethesda's compiler
both write them.

**What does NOT fix it:** `--enable-debug-info=0` leaves the header untouched
(and needlessly drops line numbers). `USERNAME` / `COMPUTERNAME` env vars don't
reach Wine's view of the user.

**Fix:** after compiling, rewrite the header — `sourceFileName` → bare basename,
`userName`/`machineName` → empty. Line-number debug info is preserved, so stack
traces still read `Foo.psc Line 42`. Use `strip_pex_metadata.py` (next to this
file); it's a generic FO4 `.pex` tool, not specific to any one mod:

```bash
python3 strip_pex_metadata.py Foo.pex [Bar.pex ...]
```

Run it on every `.pex` right after compilation and before packaging/distribution.

**For another AI applying this to a different mod:** copy `strip_pex_metadata.py`
into the repo and add the `python3 .../strip_pex_metadata.py <pex>` call to the
build/packaging script immediately after the Caprica (or Creation Kit) compile
step, before any `cp`/zip of the `.pex`. Verify with:
`strings Foo.pex | grep -iE 'home|users|<username>|<machinename>'` → should be empty.
