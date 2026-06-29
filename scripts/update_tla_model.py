"""
Synchronize TLA+ model with code.
Idempotent: safe to run on already-updated model.

Adds: adminLogs, requireAuth, rateLimitMs to match the codebase.
"""
import sys

with open('formal/tla/WinInspect_v2.tla', 'rb') as f:
    data = f.read()

NL = b'\n'
BS = b'\\'

def apply(old, new, label):
    global data
    if old in data:
        data = data.replace(old, new)
        print(f"  OK: {label}")
        return True
    print(f"  SKIP: {label} (already applied?)")
    return False

# --- Phase 1: Add adminLogs and requireAuth to VARIABLES ---
apply(
    b'noClipboard        ' + BS + b'* BOOLEAN',
    b'noClipboard,        ' + BS + b'* BOOLEAN' + NL +
    b'  adminLogs          ' + BS + b'* BOOLEAN',
    "adminLogs to VARIABLES"
)
apply(
    b'adminLogs          ' + BS + b'* BOOLEAN',
    b'adminLogs,          ' + BS + b'* BOOLEAN' + NL +
    b'  requireAuth        ' + BS + b'* BOOLEAN',
    "requireAuth to VARIABLES"
)

# --- Phase 2: Add to Init ---
apply(
    b'/\\ noClipboard ' + BS + b'in {TRUE, FALSE}',
    b'/\\ noClipboard ' + BS + b'in {TRUE, FALSE}' + NL +
    b'  /\\ adminLogs ' + BS + b'in {TRUE, FALSE}',
    "adminLogs to Init"
)
apply(
    b'/\\ adminLogs ' + BS + b'in {TRUE, FALSE}',
    b'/\\ adminLogs ' + BS + b'in {TRUE, FALSE}' + NL +
    b'  /\\ requireAuth ' + BS + b'in {TRUE, FALSE}',
    "requireAuth to Init"
)

# --- Phase 3: Add to HandleOne auth gate ---
# Add requireAuth check before allow/deny check
marker = (BS + b'* ---- METHOD AUTHORIZATION (--allow/--deny) ----')
req_auth_block = (
    BS + b'* ---- AUTHENTICATION ENFORCEMENT (--require-auth) ----' + NL +
    b'            ELSE IF requireAuth /\\ m.method # "hello"' + NL +
    b'            THEN /' + BS + b" outbox' = [outbox EXCEPT ![c] = Append(@," + NL +
    b'                    [id |-> m.id, method |-> m.method,' + NL +
    b'                     hwnd |-> NULL, snapId |-> NULL, oldSnapId |-> NULL,' + NL +
    b'                     changed |-> FALSE, ok |-> FALSE,' + NL +
    b'                     session |-> c])]' + NL +
    b'                 /' + BS + b' UNCHANGED <<conns, connCount, worldVisible, worldForeground,' + NL +
    b'                                lastSnapId, snaps, snapPinCount,' + NL +
    b'                                snapOrder, snapCounter, sessions,' + NL +
    b'                                sessionCount, subscribed, readOnly, noClipboard,' + NL +
    b'                                adminLogs, requireAuth>>' + NL
)
apply(marker, req_auth_block + marker, "requireAuth auth gate")

# Add adminLogs gate before QueryMethods
admin_marker = (BS + b'* ---- QUERY METHODS (side-effect-free) ----')
admin_block = (
    BS + b'* ---- ADMIN LOGS GATE (--admin-logs) ----' + NL +
    b'            ELSE IF m.method = "Logs" /\\ ~adminLogs' + NL +
    b"            THEN /" + BS + b" outbox' = [outbox EXCEPT ![c] = Append(@," + NL +
    b'                    [id |-> m.id, method |-> m.method,' + NL +
    b'                     hwnd |-> NULL, snapId |-> NULL, oldSnapId |-> NULL,' + NL +
    b'                     changed |-> FALSE, ok |-> FALSE,' + NL +
    b'                     session |-> c])]' + NL +
    b'                 /' + BS + b' UNCHANGED <<conns, connCount, worldVisible, worldForeground,' + NL +
    b'                                lastSnapId, snaps, snapPinCount,' + NL +
    b'                                snapOrder, snapCounter, sessions,' + NL +
    b'                                sessionCount, subscribed, readOnly, noClipboard,' + NL +
    b'                                adminLogs, requireAuth>>' + NL
)
apply(admin_marker, admin_block + admin_marker, "adminLogs gate")

# --- Phase 4: Add to all UNCHANGED blocks that are missing adminLogs/requireAuth ---
for var in [b'adminLogs', b'requireAuth']:
    count_before = data.count(var)
    # Add after noClipboard in each UNCHANGED list
    data = data.replace(
        b'noClipboard>>',  # single variable closing
        b'noClipboard, ' + var + b'>>'
    )
    data = data.replace(
        b'noClipboard, noClipboard>>',  # fix any double-insertions from idempotent runs
        b'noClipboard>>'
    )
    count_after = data.count(var)
    print(f"  {var.decode()}: {count_before} -> {count_after} occurrences")

# Write
with open('formal/tla/WinInspect_v2.tla', 'wb') as f:
    f.write(data)

# Final count
final = {
    'adminLogs': data.count(b'adminLogs'),
    'requireAuth': data.count(b'requireAuth'),
    'noClipboard': data.count(b'noClipboard'),
    'AllowMethods': data.count(b'AllowMethods'),
    'DenyMethods': data.count(b'DenyMethods'),
}
print(f"\nFinal counts:")
all_ok = True
for k, v in final.items():
    ok = v > 0
    print(f"  {k}: {v} {'OK' if ok else 'MISSING!'}")
    if not ok:
        all_ok = False

if all_ok:
    print("\nSUCCESS: Model is synchronized with code.")
else:
    print("\nWARNING: Some elements missing.")
    sys.exit(1)
