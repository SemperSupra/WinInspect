# TLA+ Model Synchronization

## Principle

The formal model must stay synchronized with the code. Every time you add
a feature flag, a new protocol method, or a stateful behavior, the model
needs updating.

## When to Update the Model

Update `formal/tla/WinInspect_v2.tla` when you:

1. **Add a daemon flag** (`--something`) that changes behavior
   - Add a `flagName` variable to `VARIABLES`
   - Add `/\ flagName \in {TRUE, FALSE}` to `Init`
   - Add blocking logic in `HandleOne`
   - Add `flagName` to all `UNCHANGED` lists and the `vars` tuple

2. **Add a new protocol method** that changes state
   - Add to the appropriate method set (`QueryMethods`, `MutationMethods`, etc.)
   - Add handling logic in `HandleOne`

3. **Change the snapshot lifecycle** (pin/evict/capture)
   - Update `AllocateSnapshot`, `PinSnapshot`, `UnpinSnapshot`

4. **Add a new invariant** you want TLC to verify
   - Add the invariant formula in the `PROPERTY` section

## How to Update (Safe Method)

Use the Python script at `scripts/update_tla_model.py` which uses
byte-string concatenation (`b'...'`) to avoid escape-sequence corruption.
Key patterns:

```python
BS = b'\\'   # single backslash byte
NL = b'\n'   # newline
# Building TLA+ operators:
#   \cup  -> BS + b'cup'
#   \notin -> BS + b'notin'
#   \in   -> BS + b'in'
#   \*    -> BS + b'*'
#   /\    -> b'/' + BS
#   outbox' -> b"outbox'"  (use double-quoted Python string)
```

## How to Verify

```bash
cd formal/tla
java -Xmx2g -cp /path/to/tla2tools.jar tlc2.TLC WinInspect_v2 -workers 4 -depth 4
```

Expected output: `512 distinct states generated at ...` then exploration
progress. Zero invariant violations.

## Version Consistency

The model version (`PROTOCOL_VERSION`) must match the C++ and Go code.
Run `scripts/verify-versions.sh` to validate.

## Checklist for Code Changes

```
[ ] Feature flag added to VARIABLES?
[ ] Flag initialized in Init?
[ ] Flag used in HandleOne (if it gates behavior)?
[ ] Flag in all UNCHANGED lists (propagation)?
[ ] Flag in vars tuple (line ~530)?
[ ] New flag constants in cfg file?
[ ] TLC run with no violations?
[ ] Model committed alongside code change?
```
