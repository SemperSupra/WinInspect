#!/usr/bin/env python3
import json
import pathlib
import sys

def c_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')

def add_case(cases, cmd, expected, accepted):
    cases.append((cmd, expected, accepted))

def key_command(mask, keys):
    padded=(list(keys)+[0]*6)[:6]
    return "K " + " ".join(str(x) for x in [mask]+padded)

def build_cases(corpus):
    cases=[]
    deferred_disconnect=False
    for v in corpus["keyboard"]:
        kind=v["kind"]
        if kind=="keyboard_usage":
            add_case(cases,key_command(0,[v["usage"]]),"OK",True)
            add_case(cases,key_command(0,[]),"OK",True)
        elif kind=="modifier":
            add_case(cases,key_command(v["mask"],[]),"OK",True)
            add_case(cases,key_command(0,[]),"OK",True)
        elif kind=="modifier_combo":
            add_case(cases,key_command(v["mask"],[]),"OK",True)
            add_case(cases,key_command(0,[]),"OK",True)
        elif kind=="rollover":
            n=v["ordinary_key_count"]
            keys=list(range(4,4+n))
            cmd="K 0 " + " ".join(str(x) for x in keys+[0]*max(0,6-len(keys)))
            if n>6:
                add_case(cases,cmd,"ERR keyboard-format",False)
            else:
                add_case(cases,cmd,"OK",True)
                add_case(cases,key_command(0,[]),"OK",True)
        elif kind=="unsupported_usage":
            add_case(cases,key_command(0,[v["usage"]]),"ERR keyboard-usage",False)
        elif kind=="duplicate_usage":
            add_case(cases,key_command(0,[4,4]),"OK",True)
            add_case(cases,key_command(0,[]),"OK",True)
        elif kind=="release_all":
            add_case(cases,"R","OK",True)
        elif kind=="disconnect_cleanup":
            deferred_disconnect=True
        else:
            raise SystemExit(f"unhandled keyboard vector kind: {kind}")

    for v in corpus["mouse"]:
        kind=v["kind"]
        if kind=="mouse_report":
            add_case(cases,f'M {v["buttons"]} {v["dx"]} {v["dy"]} {v["wheel"]}',"OK",True)
            if v["buttons"]:
                add_case(cases,"M 0 0 0 0","OK",True)
        elif kind=="mouse_buttons":
            add_case(cases,f'M {v["buttons"]} 0 0 0',"OK",True)
            add_case(cases,"M 0 0 0 0","OK",True)
        elif kind=="mouse_invalid":
            add_case(cases,f'M {v["buttons"]} {v["dx"]} {v["dy"]} {v["wheel"]}',"ERR mouse-format",False)
        elif kind=="disconnect_cleanup":
            deferred_disconnect=True
        else:
            raise SystemExit(f"unhandled mouse vector kind: {kind}")

    if not deferred_disconnect:
        raise SystemExit("canonical corpus lost disconnect cleanup vector")
    # Hold one keyboard modifier+ordinary key and one mouse button, then close.
    add_case(cases,key_command(2,[4]),"OK",True)
    add_case(cases,"M 1 0 0 0","OK",True)
    return cases

def emit(corpus_path, output_path, summary_path):
    corpus=json.loads(pathlib.Path(corpus_path).read_text())
    cases=build_cases(corpus)
    accepted=sum(1 for _,_,a in cases if a)
    rejected=sum(1 for _,_,a in cases if not a)
    lines=[]
    lines += [
      "#define WIN32_LEAN_AND_MEAN",
      "#include <winsock2.h>",
      "#include <ws2tcpip.h>",
      "#include <stdio.h>",
      "#include <stdlib.h>",
      "#include <string.h>",
      "",
      "typedef struct { const char *cmd; const char *expect; } CASE;",
      "static int send_all(SOCKET s,const char *p,size_t n){while(n){int rc=send(s,p,(int)n,0);if(rc<=0)return -1;p+=rc;n-=(size_t)rc;}return 0;}",
      "static int read_line(SOCKET s,char *buf,size_t cap){size_t n=0;while(n+1<cap){char ch;int rc=recv(s,&ch,1,0);if(rc<=0)return -1;if(ch=='\\n'){buf[n]='\\0';return 0;}if(ch!='\\r')buf[n++]=ch;}return -1;}",
      "static int command(SOCKET s,const char *cmd,const char *expect){char line[160];if(send_all(s,cmd,strlen(cmd))<0||send_all(s,"\\n",1)<0)return -1;if(read_line(s,line,sizeof(line))<0)return -1;if(strcmp(line,expect)!=0){fprintf(stderr,"cmd=%s expected=%s got=%s\\n",cmd,expect,line);return -1;}return 0;}",
      "",
      "static const CASE kCases[] = {"
    ]
    for cmd,expect,_ in cases:
        lines.append(f'  {{"{c_escape(cmd)}","{c_escape(expect)}"}},')
    lines += [
      "};",
      "",
      "int main(int argc,char **argv){",
      "  if(argc!=3){fprintf(stderr,"usage: %s PORT TOKEN\\n",argv[0]);return 2;}",
      "  int port=atoi(argv[1]); const char *token=argv[2];",
      "  if(port<1||port>65535||!*token)return 3;",
      "  WSADATA data; if(WSAStartup(MAKEWORD(2,2),&data)!=0)return 4;",
      "  SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); if(s==INVALID_SOCKET)return 5;",
      "  struct sockaddr_in a; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons((u_short)port);",
      "  if(InetPtonA(AF_INET,"127.0.0.1",&a.sin_addr)!=1||connect(s,(struct sockaddr*)&a,sizeof(a))!=0)return 6;",
      "  char auth[192]; snprintf(auth,sizeof(auth),"AUTH %s",token);",
      "  if(command(s,auth,"OK")<0)return 7;",
      "  for(size_t i=0;i<sizeof(kCases)/sizeof(kCases[0]);++i){if(command(s,kCases[i].cmd,kCases[i].expect)<0)return 10+(int)(i%200);}",
      f'  printf("PASS accepted={accepted} rejected={rejected} cases=%u final_disconnect_hold=1\\n",(unsigned)(sizeof(kCases)/sizeof(kCases[0])));',
      "  closesocket(s); WSACleanup(); return 0;",
      "}"
    ]
    pathlib.Path(output_path).write_text("\n".join(lines)+"\n")
    summary={"schema":"wininspect-full-conformance-generated/1","accepted":accepted,"rejected":rejected,"cases":len(cases)}
    pathlib.Path(summary_path).write_text(json.dumps(summary,indent=2)+"\n")

if __name__=="__main__":
    if len(sys.argv)!=4:
        raise SystemExit("usage: generate_full_conformance_client.py CORPUS OUTPUT_C SUMMARY_JSON")
    emit(*sys.argv[1:])
