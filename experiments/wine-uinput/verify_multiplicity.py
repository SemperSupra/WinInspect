#!/usr/bin/env python3
import json
import pathlib
import re
import sys

NAMES = [
    "WinInspect-Multi-Kbd-A",
    "WinInspect-Multi-Kbd-B",
    "WinInspect-Multi-Mouse-A",
    "WinInspect-Multi-Mouse-B",
]

def load_jsonl(path):
    out=[]
    for line in pathlib.Path(path).read_text(errors="replace").splitlines():
        try:
            out.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return out

def one_handle(events, pred, label, failures):
    handles={e.get("hDevice") for e in events if pred(e) and e.get("hDevice")}
    if len(handles) != 1:
        failures.append(f"{label}: expected one handle, saw {sorted(handles)}")
        return None
    return next(iter(handles))

def main(probe_path, xinput_ids_path, xi2_path, host_path, out_path):
    failures=[]
    events=[e for e in load_jsonl(probe_path) if e.get("event")=="raw_input"]

    kbd_a=one_handle(events, lambda e:e.get("type")=="keyboard" and e.get("vkey")==65, "kbdA", failures)
    kbd_b=one_handle(events, lambda e:e.get("type")=="keyboard" and e.get("vkey")==66, "kbdB", failures)
    mouse_a=one_handle(events, lambda e:e.get("type")=="mouse" and e.get("dx")==17, "mouseA", failures)
    mouse_b=one_handle(events, lambda e:e.get("type")=="mouse" and e.get("dy")==-13, "mouseB", failures)

    handles=[h for h in (kbd_a,kbd_b,mouse_a,mouse_b) if h]
    keyboard_handles={h for h in (kbd_a,kbd_b) if h}
    mouse_handles={h for h in (mouse_a,mouse_b) if h}
    if len(keyboard_handles) != 1:
        failures.append(f"Wine keyboard identity topology changed: {sorted(keyboard_handles)}")
    if len(mouse_handles) != 1:
        failures.append(f"Wine mouse identity topology changed: {sorted(mouse_handles)}")
    if keyboard_handles and mouse_handles and keyboard_handles == mouse_handles:
        failures.append("Wine keyboard and mouse classes unexpectedly share one Raw Input handle")

    def seq_for(pred):
        vals=[e.get("seq") for e in events if pred(e) and isinstance(e.get("seq"), int)]
        return min(vals) if vals else None

    a_down=seq_for(lambda e:e.get("type")=="keyboard" and e.get("vkey")==65 and e.get("message")==256)
    b_down=seq_for(lambda e:e.get("type")=="keyboard" and e.get("vkey")==66 and e.get("message")==256)
    b_up=seq_for(lambda e:e.get("type")=="keyboard" and e.get("vkey")==66 and e.get("message")==257)
    a_up=seq_for(lambda e:e.get("type")=="keyboard" and e.get("vkey")==65 and e.get("message")==257)
    if None in (a_down,b_down,b_up,a_up) or not (a_down < b_down < b_up < a_up):
        failures.append(f"keyboard interleave not preserved: {a_down},{b_down},{b_up},{a_up}")

    def mouse_flag_seen(handle, mask):
        return any(e.get("type")=="mouse" and e.get("hDevice")==handle and (int(e.get("button_flags",0)) & mask) for e in events)

    if mouse_a:
        if not mouse_flag_seen(mouse_a, 0x0001): failures.append("mouseA left-down not observed on mouseA handle")
        if not mouse_flag_seen(mouse_a, 0x0002): failures.append("mouseA left-up not observed on mouseA handle")
    if mouse_b:
        if not mouse_flag_seen(mouse_b, 0x0004): failures.append("mouseB right-down not observed on mouseB handle")
        if not mouse_flag_seen(mouse_b, 0x0008): failures.append("mouseB right-up not observed on mouseB handle")

    ids={}
    for line in pathlib.Path(xinput_ids_path).read_text(errors="replace").splitlines():
        if "=" in line:
            name,val=line.split("=",1)
            val=val.strip()
            if val.isdigit(): ids[name.strip()]=int(val)
    if set(ids) != set(NAMES):
        failures.append(f"xinput ids incomplete: {ids}")

    xi2=pathlib.Path(xi2_path).read_text(errors="replace")
    for name,xid in ids.items():
        if not re.search(rf"device:\s+\d+\s+\({xid}\)", xi2):
            failures.append(f"XI2 source id did not emit: {name} id={xid}")

    host=pathlib.Path(host_path).read_text(errors="replace")
    for name in NAMES:
        if name not in host:
            failures.append(f"host input identity missing: {name}")

    result={
        "schema":"wininspect-wine-multiplicity/1",
        "host_device_count":sum(name in host for name in NAMES),
        "xinput_ids":ids,
        "wine_handles":{"kbdA":kbd_a,"kbdB":kbd_b,"mouseA":mouse_a,"mouseB":mouse_b},
        "wine_distinct_handle_count":len(set(handles)),
        "wine_keyboard_handle_count":len(keyboard_handles),
        "wine_mouse_handle_count":len(mouse_handles),
        "wine_same_class_identity_collapsed":len(keyboard_handles)==1 and len(mouse_handles)==1,
        "failures":failures,
        "pass":not failures,
    }
    pathlib.Path(out_path).write_text(json.dumps(result,indent=2,sort_keys=True)+"\n")
    print(json.dumps(result,sort_keys=True))
    return 0 if not failures else 1

if __name__=="__main__":
    if len(sys.argv)!=6:
        raise SystemExit("usage: verify_multiplicity.py PROBE_JSONL XINPUT_IDS XI2 HOST_INPUTS OUT")
    raise SystemExit(main(*sys.argv[1:]))
