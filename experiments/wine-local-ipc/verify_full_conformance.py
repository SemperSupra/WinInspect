#!/usr/bin/env python3
import json
import pathlib
import re
import sys

def require(pattern, text, label, failures):
    if not re.search(pattern,text,re.M):
        failures.append(label)

def main(corpus_path, server_path, keyboard_log, mouse_log, out_path):
    corpus=json.loads(pathlib.Path(corpus_path).read_text())
    server=pathlib.Path(server_path).read_text()
    klog=pathlib.Path(keyboard_log).read_text(errors="replace")
    mlog=pathlib.Path(mouse_log).read_text(errors="replace")
    failures=[]

    pairs={int(u,16):name for u,name in re.findall(r"case\s+0x([0-9a-fA-F]+)\s*:\s*return\s+(KEY_[A-Z0-9_]+)",server)}
    usages=[v["usage"] for v in corpus["keyboard"] if v["kind"]=="keyboard_usage"]
    for usage in usages:
        name=pairs.get(usage)
        if not name:
            failures.append(f"mapping missing usage 0x{usage:02x}")
            continue
        require(rf"\({re.escape(name)}\), value 1\b",klog,f"{name} make",failures)
        require(rf"\({re.escape(name)}\), value 0\b",klog,f"{name} break",failures)

    mod_block=re.search(r"static const int modifier_codes\[8\]\s*=\s*\{([^}]+)\}",server,re.S)
    if not mod_block:
        failures.append("modifier table missing")
    else:
        mods=re.findall(r"KEY_[A-Z0-9_]+",mod_block.group(1))
        if len(mods)!=8: failures.append(f"modifier count {len(mods)}")
        for name in mods:
            require(rf"\({name}\), value 1\b",klog,f"{name} make",failures)
            require(rf"\({name}\), value 0\b",klog,f"{name} break",failures)

    for value in (-127,-1,1,127):
        require(rf"\(REL_X\), value {value}\b",mlog,f"REL_X {value}",failures)
        require(rf"\(REL_Y\), value {value}\b",mlog,f"REL_Y {value}",failures)
    for name in ("BTN_LEFT","BTN_RIGHT","BTN_MIDDLE"):
        require(rf"\({name}\), value 1\b",mlog,f"{name} make",failures)
        require(rf"\({name}\), value 0\b",mlog,f"{name} break",failures)
    for value in (-127,1,127):
        require(rf"\(REL_WHEEL\), value {value}\b",mlog,f"REL_WHEEL {value}",failures)
    require(r"\(REL_X\), value 17\b",mlog,"combined REL_X 17",failures)
    require(r"\(REL_Y\), value -11\b",mlog,"combined REL_Y -11",failures)

    result={
      "schema":"wininspect-full-conformance-observation/1",
      "declared_keyboard_usages":len(usages),
      "failures":failures,
      "pass":not failures
    }
    pathlib.Path(out_path).write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(result))
    return 0 if not failures else 1

if __name__=="__main__":
    if len(sys.argv)!=6: raise SystemExit("usage: verify_full_conformance.py CORPUS SERVER KLOG MLOG OUT")
    raise SystemExit(main(*sys.argv[1:]))
