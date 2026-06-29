"""Update TLA+ model with clipboard, authorization, and allow/deny features."""
# Use raw strings throughout to avoid Python escape sequence corruption.

S1 = r'MaxSessions        \* Max persistent sessions'
S2 = r'MaxSessions,       \* Max persistent sessions' + '\n' + \
     r'  AllowMethods,      \* Set of allowed methods (empty = all allowed)' + '\n' + \
     r'  DenyMethods        \* Set of denied methods (empty = none denied)'

S3 = r'readOnly           \* BOOLEAN'
S4 = r'readOnly,           \* BOOLEAN' + '\n' + r'  noClipboard        \* BOOLEAN'

S5 = r'MutationMethods == {'
S6 = r'ClipboardMethods == {\n  "ClipboardRead", "ClipboardWrite"\n}\n\nMutationMethods == {'

S7 = r'AllMethods == QueryMethods \cup DesiredStateMethods \cup MutationMethods \cup SpecialMethods'
S8 = r'AllMethods == QueryMethods \cup DesiredStateMethods \cup MutationMethods \cup ClipboardMethods \cup SpecialMethods'

S9 = r'/\ readOnly \in {TRUE, FALSE}'
S10 = r'/\ readOnly \in {TRUE, FALSE}' + '\n' + r'  /\ noClipboard \in {TRUE, FALSE}'

with open('formal/tla/WinInspect_v2.tla', 'r') as f:
    content = f.read()

content = content.replace(S1, S2)
content = content.replace(S3, S4)
content = content.replace(S5, S6)
content = content.replace(S7, S8)
content = content.replace(S9, S10)

with open('formal/tla/WinInspect_v2.tla', 'w') as f:
    f.write(content)
print("Phase 1 done")

# Phase 2: This is the tricky part with \n and \notin etc.
with open('formal/tla/WinInspect_v2.tla', 'r') as f:
    content = f.read()

# Build the auth block using raw strings for TLA+ operators
marker = r'            \* ---- QUERY METHODS (side-effect-free) ----'

auth = ''
auth += r'            \* ---- METHOD AUTHORIZATION (--allow/--deny) ----' + '\n'
auth += r'            ELSE IF (AllowMethods # {} /\ m.method \notin AllowMethods)' + '\n'
auth += r'                 \/ m.method \in DenyMethods' + '\n'
auth += r"            THEN /\ outbox' = [outbox EXCEPT ![c] = Append(@," + '\n'
auth += r'                    [id |-> m.id, method |-> m.method,' + '\n'
auth += r'                     hwnd |-> NULL, snapId |-> NULL, oldSnapId |-> NULL,' + '\n'
auth += r'                     changed |-> FALSE, ok |-> FALSE,' + '\n'
auth += r'                     session |-> c])]' + '\n'
auth += r'                 /\ UNCHANGED <<conns, connCount, worldVisible, worldForeground,' + '\n'
auth += r'                                lastSnapId, snaps, snapPinCount,' + '\n'
auth += r'                                snapOrder, snapCounter, sessions,' + '\n'
auth += r'                                sessionCount, subscribed, readOnly, noClipboard>>' + '\n'
auth += r'            \* ---- CLIPBOARD BLOCKING (--no-clipboard) ----' + '\n'
auth += r'            ELSE IF m.method \in ClipboardMethods /\ noClipboard' + '\n'
auth += r"            THEN /\ outbox' = [outbox EXCEPT ![c] = Append(@," + '\n'
auth += r'                    [id |-> m.id, method |-> m.method,' + '\n'
auth += r'                     hwnd |-> NULL, snapId |-> NULL, oldSnapId |-> NULL,' + '\n'
auth += r'                     changed |-> FALSE, ok |-> FALSE,' + '\n'
auth += r'                     session |-> c])]' + '\n'
auth += r'                 /\ UNCHANGED <<conns, connCount, worldVisible, worldForeground,' + '\n'
auth += r'                                lastSnapId, snaps, snapPinCount,' + '\n'
auth += r'                                snapOrder, snapCounter, sessions,' + '\n'
auth += r'                                sessionCount, subscribed, readOnly, noClipboard>>' + '\n'
auth += marker

content = content.replace(marker, auth)

with open('formal/tla/WinInspect_v2.tla', 'w') as f:
    f.write(content)

print("Phase 2: authorization and clipboard blocks inserted")
print("Done!")
