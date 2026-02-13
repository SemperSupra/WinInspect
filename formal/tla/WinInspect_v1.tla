---- MODULE WinInspect_v1 ----
EXTENDS Naturals, Sequences, FiniteSets

(*
  Minimal model:
  - Multiple clients concurrently issue requests.
  - Daemon routes responses per connection; subscriptions are per connection.
  - Inspection has no side effects; EnsureVisible is desired-state idempotent.
*)

CONSTANTS Clients, Windows

VARIABLES
  worldVisible,         \* [Windows -> BOOLEAN]
  conns,                \* SUBSET Clients
  inbox, outbox,        \* [Clients -> Seq(Msg)]
  subs

EvType == {"Create","Destroy","Move","NameChange","Foreground"}

Msg ==
  [ id     : Nat,
    method : {"ListTop","GetInfo","EnsureVisible","Subscribe","Poll"},
    hwnd   : Windows \cup {NULL},
    vis    : BOOLEAN \cup {NULL},
    evset  : SUBSET EvType \cup {NULL}
  ]

Init ==
  /\ worldVisible \in [Windows -> BOOLEAN]
  /\ conns = {}
  /\ inbox = [c \in Clients |-> << >>]
  /\ outbox = [c \in Clients |-> << >>]
  /\ subs = [c \in Clients |-> {}]

Connect(c) ==
  /\ c \in Clients
  /\ c \notin conns
  /\ conns' = conns \cup {c}
  /\ UNCHANGED <<worldVisible,inbox,outbox,subs>>

Send(c, m) ==
  /\ c \in conns
  /\ inbox' = [inbox EXCEPT ![c] = Append(@, m)]
  /\ UNCHANGED <<worldVisible,conns,outbox,subs>>

HandleOne(c) ==
  /\ c \in conns
  /\ Len(inbox[c]) > 0
  /\ LET m == Head(inbox[c]) IN
     /\ inbox' = [inbox EXCEPT ![c] = Tail(@)]
     /\ CASE m.method = "EnsureVisible" ->
           /\ worldVisible' = [worldVisible EXCEPT ![m.hwnd] = m.vis]
           /\ outbox' = [outbox EXCEPT ![c] = Append(@, m)]
           /\ UNCHANGED <<conns,subs>>
        [] OTHER ->
           /\ outbox' = [outbox EXCEPT ![c] = Append(@, m)]
           /\ UNCHANGED <<worldVisible,conns,subs>>

NonInterference ==
  \A c1, c2 \in Clients :
    c1 # c2 =>
      \A i \in 1..Len(outbox[c1]) :
        \A j \in 1..Len(outbox[c2]) :
          outbox[c1][i].id # outbox[c2][j].id

Next ==
  \E c \in Clients :
    Connect(c) \/ HandleOne(c)

Spec == Init /\ [][Next]_<<worldVisible,conns,inbox,outbox,subs>>

THEOREM Spec => []NonInterference
====
