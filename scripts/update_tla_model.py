"""
Update TLA+ model with clipboard, authorization, and allow/deny features.
"""
import sys

with open('formal/tla/WinInspect_v2.tla', 'rb') as f:
    data = f.read()

NL = b'\n'
BS = b'\\'  # single backslash byte

replacements = [
    # 1. Constants
    (b'MaxSessions        ' + BS + b'* Max persistent sessions',
     b'MaxSessions,       ' + BS + b'* Max persistent sessions' + NL +
     b'  AllowMethods,      ' + BS + b'* Set of allowed methods (empty = all allowed)' + NL +
     b'  DenyMethods        ' + BS + b'* Set of denied methods (empty = none denied)'),

    # 2. Variables
    (b'readOnly           ' + BS + b'* BOOLEAN',
     b'readOnly,           ' + BS + b'* BOOLEAN' + NL +
     b'  noClipboard        ' + BS + b'* BOOLEAN'),

    # 3. ClipboardMethods
    (b'MutationMethods == {',
     b'ClipboardMethods == {' + NL +
     b'  "ClipboardRead", "ClipboardWrite"' + NL +
     b'}' + NL + NL +
     b'MutationMethods == {'),

    # 4. AllMethods union
    (b'AllMethods == QueryMethods ' + BS + b'cup DesiredStateMethods ' + BS + b'cup MutationMethods ' + BS + b'cup SpecialMethods',
     b'AllMethods == QueryMethods ' + BS + b'cup DesiredStateMethods ' + BS + b'cup MutationMethods ' + BS + b'cup ClipboardMethods ' + BS + b'cup SpecialMethods'),

    # 5. Init
    (b'/' + BS + b' readOnly ' + BS + b'in {TRUE, FALSE}',
     b'/' + BS + b' readOnly ' + BS + b'in {TRUE, FALSE}' + NL +
     b'  /' + BS + b' noClipboard ' + BS + b'in {TRUE, FALSE}'),
]

# 6. Auth block: uses \notin (backslash-n-o-t-i-n, no newline issues because we use byte concatenation)
AUTH  = b'            ' + BS + b'* ---- METHOD AUTHORIZATION (--allow/--deny) ----' + NL
AUTH += b'            ELSE IF (AllowMethods # {} /' + BS + b' m.method ' + BS + b'notin AllowMethods)' + NL
AUTH += b'                 ' + BS + b'/ m.method ' + BS + b'in DenyMethods' + NL
AUTH += b"            THEN /" + BS + b" outbox' = [outbox EXCEPT ![c] = Append(@," + NL
AUTH += b'                    [id |-> m.id, method |-> m.method,' + NL
AUTH += b'                     hwnd |-> NULL, snapId |-> NULL, oldSnapId |-> NULL,' + NL
AUTH += b'                     changed |-> FALSE, ok |-> FALSE,' + NL
AUTH += b'                     session |-> c])]' + NL
AUTH += b'                 /' + BS + b' UNCHANGED <<conns, connCount, worldVisible, worldForeground,' + NL
AUTH += b'                                lastSnapId, snaps, snapPinCount,' + NL
AUTH += b'                                snapOrder, snapCounter, sessions,' + NL
AUTH += b'                                sessionCount, subscribed, readOnly, noClipboard>>' + NL
AUTH += b'            ' + BS + b'* ---- CLIPBOARD BLOCKING (--no-clipboard) ----' + NL
AUTH += b'            ELSE IF m.method ' + BS + b'in ClipboardMethods /' + BS + b' noClipboard' + NL
AUTH += b"            THEN /" + BS + b" outbox' = [outbox EXCEPT ![c] = Append(@," + NL
AUTH += b'                    [id |-> m.id, method |-> m.method,' + NL
AUTH += b'                     hwnd |-> NULL, snapId |-> NULL, oldSnapId |-> NULL,' + NL
AUTH += b'                     changed |-> FALSE, ok |-> FALSE,' + NL
AUTH += b'                     session |-> c])]' + NL
AUTH += b'                 /' + BS + b' UNCHANGED <<conns, connCount, worldVisible, worldForeground,' + NL
AUTH += b'                                lastSnapId, snaps, snapPinCount,' + NL
AUTH += b'                                snapOrder, snapCounter, sessions,' + NL
AUTH += b'                                sessionCount, subscribed, readOnly, noClipboard>>' + NL

MARKER = b'            ' + BS + b'* ---- QUERY METHODS (side-effect-free) ----'

# Apply all replacements
for i, (old, new) in enumerate(replacements):
    if old in data:
        data = data.replace(old, new)
        print(f"  [{i+1}] OK: {old[:60].decode('ascii', errors='replace').rstrip()}...")
    else:
        print(f"  [{i+1}] NOT FOUND: {old[:60].decode('ascii', errors='replace').rstrip()}...")

if MARKER in data:
    data = data.replace(MARKER, AUTH + MARKER)
    print(f"  [6] OK: authorization + clipboard blocks")
else:
    print(f"  [6] NOT FOUND: QUERY METHODS marker")

with open('formal/tla/WinInspect_v2.tla', 'wb') as f:
    f.write(data)

# Verify
counts = {
    'noClipboard': data.count(b'noClipboard'),
    'AllowMethods': data.count(b'AllowMethods'),
    'DenyMethods': data.count(b'DenyMethods'),
    'ClipboardMethods': data.count(b'ClipboardMethods'),
    'notin': data.count(b'\\notin'),
}
print(f"\nVerification:")
for k, v in counts.items():
    print(f"  {k}: {v}", end='')
    print(" OK" if v > 0 else " MISSING!")

if all(v > 0 for v in counts.values()):
    print("\nSUCCESS: All changes applied. Ready for TLC.")
else:
    print("\nFAILURE: Some replacements missing.")
    sys.exit(1)
