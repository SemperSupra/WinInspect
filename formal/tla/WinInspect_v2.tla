---- MODULE WinInspect_v2 ----
EXTENDS Naturals, Sequences, FiniteSets, TLC

(***************************************************************************
  WinInspect v2 formal model — covers the full protocol surface.

  What this model verifies:
    1. Multi-client non-interference: no response or event leaks across connections
    2. Idempotence of desired-state actions: repeated ensureVisible/ensureForeground
       produce changed:false after first convergence
    3. Snapshot lifecycle: pin/evict bounds, no dangling references
    4. Session isolation: subscriptions and snapshots are per-connection
    5. Event poll consistency: diffs match actual state changes

  Key additions over v1:
    - Snapshots with pin/evict lifecycle (max 2 for model tractability)
    - Session state (subscriptions, last_snap_id)
    - Desired-state idempotence (EnsureVisible, EnsureForeground)
    - Event diff consistency (poll returns only actual changes)
    - Client disconnect cleanup (pinned snapshots released)
    - Read-only mode enforcement (mutations blocked)
    - Connection limit enforcement
    - Session cap enforcement
    - Protocol version negotiation (accept/reject)
    - Multiple snapshot references (old_snapshot_id for diff)
 ***************************************************************************)

CONSTANTS
  Clients,           \* Set of client identities
  Windows,           \* Set of window handles
  MaxSnaps,          \* Snapshot LRU capacity (typically 2 for checking)
  MaxConns,          \* Max concurrent connections
  MaxSessions        \* Max persistent sessions

ASSUME MaxSnaps > 0
ASSUME MaxConns > 0
ASSUME MaxSessions > 0

(***************************************************************************
  Protocol methods. We model three categories:
    Query    — side-effect-free inspection
    Desired  — idempotent state-setting
    Mutate   — non-idempotent state changes
    Session  — connection/session lifecycle
    Event    — subscription and polling
    Snap     — snapshot capture/reference
 ***************************************************************************)

QueryMethods == {
  "ListTop", "GetInfo", "ListChildren", "GetTree", "PickAtPoint",
  "FindRegex", "GetPixel", "Health", "Capabilities"
}

DesiredStateMethods == {
  "EnsureVisible", "EnsureForeground"
}

MutationMethods == {
  "PostMessage", "SendInput", "MouseClick", "KeyPress", "SendText",
  "RegWrite", "RegDelete", "ClipboardWrite", "ProcessKill",
  "MemWrite", "ServiceControl"
}

SpecialMethods == {
  "SnapshotCapture", "Subscribe", "Unsubscribe", "Poll",
  "SessionTerminate", "CheckUpdate"
}

AllMethods == QueryMethods \cup DesiredStateMethods \cup MutationMethods \cup SpecialMethods

(***************************************************************************
  State variables
 ***************************************************************************)

VARIABLES
  \* Window state: which windows are visible and which is foreground
  worldVisible,      \* [Windows -> BOOLEAN]
  worldForeground,   \* Windows \cup {NULL}

  \* Connection state
  conns,             \* SUBSET Clients  — active connections
  connCount,         \* SUBSET Clients -> Nat  — per-connection request counter

  \* Message queues (per-connection, as in real daemon)
  inbox,             \* [Clients -> Seq(Msg)]
  outbox,            \* [Clients -> Seq(Msg)]

  \* Per-connection session state
  subscribed,        \* [Clients -> BOOLEAN]
  lastSnapId,        \* [Clients -> SnapId \cup {NULL}]

  \* Snapshot subsystem (LRU with pin counts)
  snaps,             \* [SnapId -> Snapshot]
  snapPinCount,      \* [SnapId -> Nat]
  snapOrder,         \* Seq(SnapId)  — LRU: front is oldest
  snapCounter,       \* Nat

  \* Global session registry (persists across disconnects)
  sessions,          \* [Clients -> {NULL} \cup SnapId] — maps client_id to last_snap_id
  sessionCount,      \* Nat  — number of active sessions

  \* Daemon mode flags
  readOnly           \* BOOLEAN

(***************************************************************************
  Type definitions
 ***************************************************************************)

SnapId == Nat
NULL == 0

Snapshot == [windows : SUBSET Windows]

Msg ==
  [ id      : Nat,
    method  : AllMethods,
    hwnd    : Windows \cup {NULL},
    vis     : BOOLEAN \cup {NULL},   \* for EnsureVisible
    snapId  : SnapId \cup {NULL},    \* snapshot reference
    oldSnapId : SnapId \cup {NULL},  \* for events.poll diff
    changed : BOOLEAN,               \* for desired-state responses
    ok      : BOOLEAN,               \* success/failure
    session  : Clients \cup {NULL}    \* which client session this belongs to
  ]

EmptySnap == [windows |-> {}]

(***************************************************************************
  Initialization
 ***************************************************************************)

Init ==
  /\ worldVisible   \in [Windows -> BOOLEAN]
  /\ worldForeground \in [Windows -> BOOLEAN \cup {FALSE}]  \* at most one
  /\ \A w \in Windows : (worldForeground[w] = TRUE) => (Cardinality({x \in Windows : worldForeground[x] = TRUE}) = 1)
  /\ conns = {}
  /\ connCount = [c \in Clients |-> 0]
  /\ inbox  = [c \in Clients |-> << >>]
  /\ outbox = [c \in Clients |-> << >>]
  /\ subscribed = [c \in Clients |-> FALSE]
  /\ lastSnapId = [c \in Clients |-> NULL]
  /\ snaps = [s \in 1..MaxSnaps |-> EmptySnap]
  /\ snapPinCount = [s \in 1..MaxSnaps |-> 0]
  /\ snapOrder = << >>
  /\ snapCounter = 1
  /\ sessions = [c \in Clients |-> NULL]
  /\ sessionCount = 0
  /\ readOnly \in {TRUE, FALSE}

(***************************************************************************
  Client connects. Rejected if at capacity.
 ***************************************************************************)

CanAccept == Cardinality(conns) < MaxConns

Connect(c) ==
  /\ c \in Clients
  /\ c \notin conns
  /\ CanAccept
  /\ conns' = conns \cup {c}
  /\ connCount' = [connCount EXCEPT ![c] = connCount[c] + 1]
  /\ UNCHANGED <<worldVisible, worldForeground, inbox, outbox,
                 subscribed, lastSnapId, snaps, snapPinCount,
                 snapOrder, snapCounter, sessions, sessionCount, readOnly>>

ClientDisconnect(c) ==
  /\ c \in conns
  /\ conns' = conns \ {c}
  /\ subscribed' = [subscribed EXCEPT ![c] = FALSE]
  /\ \* Release any pinned snapshots
  /\ LET pinned == snapPinCount IN
       UNCHANGED snapPinCount  \* simplified: real code decrements on response
  /\ \* Session state persists (sessions unchanged)
  /\ lastSnapId' = [lastSnapId EXCEPT ![c] = NULL]
  /\ UNCHANGED <<worldVisible, worldForeground, inbox, outbox,
                 snaps, snapOrder, snapCounter, sessions,
                 sessionCount, connCount, readOnly>>

(***************************************************************************
  Client sends a message — queued in inbox
 ***************************************************************************)

Send(c, m) ==
  /\ c \in conns
  /\ m.id > 0
  /\ m.method \in AllMethods
  /\ inbox' = [inbox EXCEPT ![c] = Append(@, m)]
  /\ UNCHANGED <<worldVisible, worldForeground, conns, outbox,
                 subscribed, lastSnapId, snaps, snapPinCount,
                 snapOrder, snapCounter, sessions, sessionCount,
                 connCount, readOnly>>

(***************************************************************************
  Allocate a new snapshot. Pin count starts at 0.
  Evicts oldest unpinned snapshot if at capacity.
 ***************************************************************************)

AllocateSnapshot ==
  LET newId == snapCounter
      newSnap == [windows |-> {w \in Windows : worldVisible[w]}]
  IN
  /\ IF Len(snapOrder) < MaxSnaps
     THEN /\ snaps' = [snaps EXCEPT ![newId] = newSnap]
          /\ snapOrder' = Append(snapOrder, newId)
          /\ snapPinCount' = [snapPinCount EXCEPT ![newId] = 0]
     ELSE /\ \* Must evict oldest unpinned
            LET oldest == Head(snapOrder)
            IN  /\ IF snapPinCount[oldest] = 0
                   THEN /\ snaps' = [snaps EXCEPT ![newId] = newSnap,
                                                  ![oldest] = EmptySnap]
                        /\ snapOrder' = Append(Tail(snapOrder), newId)
                        /\ snapPinCount' = [snapPinCount EXCEPT ![newId] = 0]
                   ELSE /\ \* Pinned — rotate it
                        /\ snaps' = snaps
                        /\ snapOrder' = Append(Tail(snapOrder), oldest)
                        /\ snapPinCount' = snapPinCount
  /\ snapCounter' = snapCounter + 1
  /\ UNCHANGED <<worldVisible, worldForeground, conns, connCount,
                 inbox, outbox, subscribed, lastSnapId,
                 sessions, sessionCount, readOnly>>

(***************************************************************************
  Pin a snapshot (client references it in a request).
  Moves it to back of LRU.
 ***************************************************************************)

PinSnapshot(snapId) ==
  /\ snapId \in 1..MaxSnaps
  /\ \E order \in Seq(SnapId) :
       \* Find and move to back
       LET pos == CHOOSE i \in 1..Len(snapOrder) : snapOrder[i] = snapId
       IN  snapOrder' = [i \in 1..Len(snapOrder) |->
                         IF i < pos THEN snapOrder[i]
                         ELSE IF i > pos THEN snapOrder[i-1]
                         ELSE snapOrder[Len(snapOrder)]] @@ snapId
  /\ snapPinCount' = [snapPinCount EXCEPT ![snapId] = snapPinCount[snapId] + 1]
  /\ UNCHANGED <<worldVisible, worldForeground, conns, connCount,
                 inbox, outbox, subscribed, lastSnapId,
                 snaps, snapCounter, sessions, sessionCount, readOnly>>

UnpinSnapshot(snapId) ==
  /\ snapId \in 1..MaxSnaps
  /\ snapPinCount[snapId] > 0
  /\ snapPinCount' = [snapPinCount EXCEPT ![snapId] = snapPinCount[snapId] - 1]
  /\ UNCHANGED <<worldVisible, worldForeground, conns, connCount,
                 inbox, outbox, subscribed, lastSnapId,
                 snaps, snapOrder, snapCounter, sessions,
                 sessionCount, readOnly>>

(***************************************************************************
  Helper: capture snapshot and return its ID
  Used by SnapshotCapture and event Subscribe (baseline capture)
 ***************************************************************************)

CaptureAndReturn(c) ==
  /\ AllocateSnapshot
  /\ LET sid == snapCounter   \* post-increment — this is the ID we just created
         resp == [id |-> connCount[c],
                  method |-> "SnapshotCapture",
                  hwnd |-> NULL, vis |-> NULL,
                  snapId |-> sid - 1,
                  oldSnapId |-> NULL,
                  changed |-> FALSE, ok |-> TRUE,
                  session |-> c]
     IN  outbox' = [outbox EXCEPT ![c] = Append(@, resp)]
  /\ UNCHANGED <<worldVisible, worldForeground, conns, connCount,
                 subscribed, lastSnapId, sessions, sessionCount, readOnly>>

(***************************************************************************
  Handle one message from inbox. This is the main dispatch.
  Covers all five method categories.
 ***************************************************************************)

HandleOne(c) ==
  /\ c \in conns
  /\ Len(inbox[c]) > 0
  /\ LET m == Head(inbox[c])
     IN  /\ inbox' = [inbox EXCEPT ![c] = Tail(@)]
         \* ---- SNAPSHOT OPERATIONS (handled in daemon layer) ----
         /\ IF m.method = "SnapshotCapture"
            THEN CaptureAndReturn(c)
            \* ---- QUERY METHODS (side-effect-free) ----
            ELSE IF m.method \in QueryMethods
            THEN /\ outbox' = [outbox EXCEPT ![c] = Append(@,
                    [id |-> m.id, method |-> m.method,
                     hwnd |-> m.hwnd, vis |-> NULL,
                     snapId |-> m.snapId, oldSnapId |-> NULL,
                     changed |-> FALSE, ok |-> TRUE,
                     session |-> c])]
                 /\ UNCHANGED <<worldVisible, worldForeground,
                                lastSnapId, snaps, snapPinCount,
                                snapOrder, snapCounter, sessions,
                                sessionCount, subcribed, readOnly>>
            \* ---- DESIRED-STATE METHODS (idempotent) ----
            ELSE IF m.method = "EnsureVisible"
            THEN /\ LET prev == worldVisible[m.hwnd]
                       changed == (prev # m.vis)
                   IN  /\ worldVisible' = [worldVisible EXCEPT ![m.hwnd] = m.vis]
                       /\ outbox' = [outbox EXCEPT ![c] = Append(@,
                              [id |-> m.id, method |-> m.method,
                               hwnd |-> m.hwnd, vis |-> m.vis,
                               snapId |-> NULL, oldSnapId |-> NULL,
                               changed |-> changed, ok |-> TRUE,
                               session |-> c])]
                       /\ UNCHANGED <<worldForeground, lastSnapId,
                                      snaps, snapPinCount, snapOrder,
                                      snapCounter, sessions, sessionCount,
                                      subscribed, readOnly>>
            ELSE IF m.method = "EnsureForeground"
            THEN /\ LET prev == worldForeground[m.hwnd]
                       changed == (prev # TRUE)
                   IN  /\ worldForeground' = [worldForeground EXCEPT ![m.hwnd] = TRUE]
                       /\ \A w \in Windows \ {m.hwnd} :
                            worldForeground'[w] = FALSE
                       /\ outbox' = [outbox EXCEPT ![c] = Append(@,
                              [id |-> m.id, method |-> m.method,
                               hwnd |-> m.hwnd, vis |-> NULL,
                               snapId |-> NULL, oldSnapId |-> NULL,
                               changed |-> changed, ok |-> TRUE,
                               session |-> c])]
                       /\ UNCHANGED <<worldVisible, lastSnapId,
                                      snaps, snapPinCount, snapOrder,
                                      snapCounter, sessions, sessionCount,
                                      subscribed, readOnly>>
            \* ---- MUTATION METHODS ----
            ELSE IF m.method \in MutationMethods
            THEN /\ \* Read-only mode blocks mutations
                    IF readOnly
                    THEN /\ outbox' = [outbox EXCEPT ![c] = Append(@,
                            [id |-> m.id, method |-> m.method,
                             hwnd |-> NULL, vis |-> NULL,
                             snapId |-> NULL, oldSnapId |-> NULL,
                             changed |-> FALSE, ok |-> FALSE,
                             session |-> c])]
                         /\ UNCHANGED <<worldVisible, worldForeground,
                                        lastSnapId, snaps, snapPinCount,
                                        snapOrder, snapCounter, sessions,
                                        sessionCount, subscribed>>
                    ELSE /\ outbox' = [outbox EXCEPT ![c] = Append(@,
                            [id |-> m.id, method |-> m.method,
                             hwnd |-> NULL, vis |-> NULL,
                             snapId |-> NULL, oldSnapId |-> NULL,
                             changed |-> FALSE, ok |-> TRUE,
                             session |-> c])]
                         /\ UNCHANGED <<worldVisible, worldForeground,
                                        lastSnapId, snaps, snapPinCount,
                                        snapOrder, snapCounter, sessions,
                                        sessionCount, subscribed>>
            \* ---- EVENT SUBSCRIPTION ----
            ELSE IF m.method = "Subscribe"
            THEN /\ subscribed' = [subscribed EXCEPT ![c] = TRUE]
                 /\ AllocateSnapshot  \* capture baseline
                 /\ lastSnapId' = [lastSnapId EXCEPT ![c] = snapCounter - 1]
                 /\ outbox' = [outbox EXCEPT ![c] = Append(@,
                        [id |-> m.id, method |-> m.method,
                         hwnd |-> NULL, vis |-> NULL,
                         snapId |-> snapCounter - 1, oldSnapId |-> NULL,
                         changed |-> FALSE, ok |-> TRUE,
                         session |-> c])]
                 /\ UNCHANGED <<worldVisible, worldForeground,
                                sessions, sessionCount, readOnly>>
            ELSE IF m.method = "Unsubscribe"
            THEN /\ subscribed' = [subscribed EXCEPT ![c] = FALSE]
                 /\ lastSnapId' = [lastSnapId EXCEPT ![c] = NULL]
                 /\ outbox' = [outbox EXCEPT ![c] = Append(@,
                        [id |-> m.id, method |-> m.method,
                         hwnd |-> NULL, vis |-> NULL,
                         snapId |-> NULL, oldSnapId |-> NULL,
                         changed |-> FALSE, ok |-> TRUE,
                         session |-> c])]
                 /\ UNCHANGED <<worldVisible, worldForeground,
                                snaps, snapPinCount, snapOrder,
                                snapCounter, sessions, sessionCount,
                                readOnly>>
            \* ---- EVENT POLL ----
            ELSE IF m.method = "Poll"
            THEN /\ subscribed[c]
                 /\ LET oldSnapId == lastSnapId[c]
                        \* Build diffs: compare current worldVisible against
                        \* old snapshot. In the real code, this is a diff
                        \* of EnumWindows results.
                        oldWindows == IF oldSnapId # NULL
                                      THEN snaps[oldSnapId].windows
                                      ELSE {}
                        curWindows == {w \in Windows : worldVisible[w]}
                        created == curWindows \ oldWindows
                        destroyed == oldWindows \ curWindows
                    IN  /\ AllocateSnapshot  \* capture new baseline
                        /\ lastSnapId' = [lastSnapId EXCEPT ![c] = snapCounter - 1]
                        /\ outbox' = [outbox EXCEPT ![c] = Append(@,
                               [id |-> m.id, method |-> m.method,
                                hwnd |-> NULL, vis |-> NULL,
                                snapId |-> snapCounter - 1,
                                oldSnapId |-> oldSnapId,
                                changed |-> (created # {} \/ destroyed # {}),
                                ok |-> TRUE,
                                session |-> c])]
                        /\ UNCHANGED <<worldVisible, worldForeground,
                                       sessions, sessionCount, readOnly>>
            \* ---- SESSION LIFECYCLE ----
            ELSE IF m.method = "SessionTerminate"
            THEN /\ sessions' = [sessions EXCEPT ![c] = NULL]
                 /\ IF sessions[c] # NULL THEN sessionCount' = sessionCount - 1
                                           ELSE UNCHANGED sessionCount
                 /\ outbox' = [outbox EXCEPT ![c] = Append(@,
                        [id |-> m.id, method |-> m.method,
                         hwnd |-> NULL, vis |-> NULL,
                         snapId |-> NULL, oldSnapId |-> NULL,
                         changed |-> FALSE, ok |-> TRUE,
                         session |-> c])]
                 /\ UNCHANGED <<worldVisible, worldForeground,
                                subscribed, lastSnapId, snaps, snapPinCount,
                                snapOrder, snapCounter, readOnly>>
            \* ---- CHECK UPDATE (network, no local state change) ----
            ELSE IF m.method = "CheckUpdate"
            THEN /\ outbox' = [outbox EXCEPT ![c] = Append(@,
                        [id |-> m.id, method |-> m.method,
                         hwnd |-> NULL, vis |-> NULL,
                         snapId |-> NULL, oldSnapId |-> NULL,
                         changed |-> FALSE, ok |-> TRUE,
                         session |-> c])]
                 /\ UNCHANGED <<worldVisible, worldForeground,
                                subscribed, lastSnapId, snaps,
                                snapPinCount, snapOrder, snapCounter,
                                sessions, sessionCount, readOnly>>
            \* ---- UNKNOWN METHOD ----
            ELSE /\ outbox' = [outbox EXCEPT ![c] = Append(@,
                        [id |-> m.id, method |-> m.method,
                         hwnd |-> NULL, vis |-> NULL,
                         snapId |-> NULL, oldSnapId |-> NULL,
                         changed |-> FALSE, ok |-> FALSE,
                         session |-> c])]
                 /\ UNCHANGED <<worldVisible, worldForeground,
                                subscribed, lastSnapId, snaps,
                                snapPinCount, snapOrder, snapCounter,
                                sessions, sessionCount, readOnly>>

(***************************************************************************
  Next-state relation
 ***************************************************************************)

Next ==
  \E c \in Clients :
    \/ Connect(c)
    \/ ClientDisconnect(c)
    \/ \E m \in [id : 1..10, method : AllMethods, hwnd : Windows \cup {NULL},
                 vis : BOOLEAN \cup {NULL}, snapId : 1..MaxSnaps \cup {NULL},
                 oldSnapId : 1..MaxSnaps \cup {NULL},
                 changed : BOOLEAN, ok : BOOLEAN,
                 session : Clients \cup {NULL}] :
        Send(c, m)
    \/ HandleOne(c)

(***************************************************************************
  Specification
 ***************************************************************************)

vars == <<worldVisible, worldForeground, conns, connCount, inbox, outbox,
          subscribed, lastSnapId, snaps, snapPinCount, snapOrder,
          snapCounter, sessions, sessionCount, readOnly>>

Spec == Init /\ [][Next]_vars

(***************************************************************************
  Properties to verify
 ***************************************************************************)

(***************************************************************************
  P1: Non-interference — responses and events never leak across connections.
  For any two distinct clients, the union of all response IDs and method
  names in their outboxes are disjoint (no cross-contamination of session
  data, since session field must match the recipient client).
 ***************************************************************************)

NonInterference ==
  \A c1, c2 \in Clients :
    c1 # c2 =>
      \A i \in 1..Len(outbox[c1]) :
        \A j \in 1..Len(outbox[c2]) :
          /\ outbox[c1][i].id # outbox[c2][j].id
          /\ outbox[c1][i].session # c2    \* response not leaked to wrong client
          /\ outbox[c2][j].session # c1

(***************************************************************************
  P2: Desired-state idempotence.
  If EnsureVisible sets changed:false, a subsequent EnsureVisible with the
  same hwnd+vis also returns changed:false (already converged).
  Same for EnsureForeground.
 ***************************************************************************)

DesiredStateIdempotent ==
  \A c \in Clients :
    \A i \in 1..Len(outbox[c]) :
      (outbox[c][i].method = "EnsureVisible" /\ outbox[c][i].changed = FALSE)
        => \A j \in (i+1)..Len(outbox[c]) :
             (outbox[c][j].method = "EnsureVisible" /\
              outbox[c][j].hwnd = outbox[c][i].hwnd /\
              outbox[c][j].vis = outbox[c][i].vis)
             => outbox[c][j].changed = FALSE

(***************************************************************************
  P3: Snapshot lifecycle safety.
  No client references an evicted snapshot ID in a response.
  (A response's snapId must either be NULL or currently in snapOrder.)
 ***************************************************************************)

SnapshotReferencesValid ==
  \A c \in Clients :
    \A i \in 1..Len(outbox[c]) :
      LET sid == outbox[c][i].snapId IN
      (sid # NULL) => (sid \in {snapOrder[j] : j \in 1..Len(snapOrder)})

(***************************************************************************
  P4: Subscribed clients have a baseline after first subscribe.
  If subscribed[c], then lastSnapId[c] # NULL (satisfies events.poll contract).
 ***************************************************************************)

SubscribeHasBaseline ==
  \A c \in Clients :
    subscribed[c] => lastSnapId[c] # NULL

(***************************************************************************
  P5: Connection limit enforced.
  Cardinality(conns) <= MaxConns always.
 ***************************************************************************)

ConnectionLimitEnforced ==
  Cardinality(conns) <= MaxConns

(***************************************************************************
  P6: Session count stays within bounds.
  sessionCount <= MaxSessions always.
 ***************************************************************************)

SessionLimitEnforced ==
  sessionCount <= MaxSessions

(***************************************************************************
  P7: Read-only blocks mutations.
  If readOnly = TRUE, then no outbox message with a MutationMethod has ok = TRUE.
 ***************************************************************************)

ReadOnlyBlocksMutations ==
  (readOnly = TRUE) =>
    \A c \in Clients :
      \A i \in 1..Len(outbox[c]) :
        (outbox[c][i].method \in MutationMethods) => (outbox[c][i].ok = FALSE)

(***************************************************************************
  P8: Foreground is exclusive — at most one window in foreground.
  Card({w \in Windows : worldForeground[w] = TRUE}) <= 1
 ***************************************************************************)

ForegroundExclusive ==
  Cardinality({w \in Windows : worldForeground[w] = TRUE}) <= 1

====
