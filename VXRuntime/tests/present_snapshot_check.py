#!/usr/bin/env python3
"""Synthetic PE capture contract; inspect PNG signature/dimensions only."""
import os,pathlib,tempfile,subprocess,socket,json,time,select,struct,sys,re
base=pathlib.Path(__file__).resolve().parents[1];exe=base/'build/present_snapshot_guest.exe'
def run(case):
    (base.parent/'runs').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vxclip-',dir='/private/tmp') as tmp, \
         tempfile.TemporaryDirectory(prefix='synthetic-snapshot-',dir=base.parent/'runs') as repo_tmp:
        root=pathlib.Path(tmp);sock=root/'control.sock';out=root/'out.png';expected=case in ('ok','repo')
        if case=='repo':out=pathlib.Path(repo_tmp)/'present_snapshot.png'
        env={k:v for k,v in os.environ.items() if not k.startswith(('VXRT_','VERANTYX_'))}
        env.update(VXRT_ROOT=str(base/'build/snapshot-root'),VXRT_NO_WINE='1',VXRT_NO_ACTIVATE='1',VXRT_CONTROL_DIAG='render',VXRT_CONTROL_TARGET=str(exe),VXRT_CONTROL_DIAG_SOCKET=str(sock),VXRT_PRESENT_SNAPSHOT_CONTROL='1',VXRT_PRESENT_SNAPSHOT=str(out),VXRT_PRESENT_SNAPSHOT_AT='1')
        if case=='ok':env.update(VXRT_CPU_DIAG='1',VXRT_WAIT_DIAG='1')
        if case=='child':env['VXRT_CHILD']='1'
        if case=='off':env.pop('VXRT_PRESENT_SNAPSHOT_CONTROL');env['VXRT_PRESENT_SNAPSHOT_AT']='999'
        if case=='target':env['VXRT_CONTROL_TARGET']='/bin/ls'
        if case=='existing':out.write_bytes(b'keep')
        if case=='symlink':out.symlink_to(root/'absent')
        if case=='parent':
            (root/'real').mkdir();(root/'alias').symlink_to(root/'real',target_is_directory=True);out=root/'alias/out.png';env['VXRT_PRESENT_SNAPSHOT']=str(out)
        p=subprocess.Popen([str(base/'build/vxrt'),str(exe),'none' if case=='none' else 'unsupported' if case=='unsupported' else 'present'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        data=b''
        def marker(word):
            nonlocal data
            end=time.monotonic()+20
            while word not in data and time.monotonic()<end:
                if select.select([p.stdout],[],[],.1)[0]:
                    chunk=os.read(p.stdout.fileno(),8192)
                    if not chunk:break
                    data+=chunk
            assert word in data,data.decode(errors='replace')
        def req(text):
            with socket.socket(socket.AF_UNIX) as s:
                s.settimeout(3);s.connect(str(sock));s.sendall(text.encode()+b'\n');r=b''
                while not r.endswith(b'\n'):
                    chunk=s.recv(4096)
                    assert chunk, 'diagnostic socket closed before reply'
                    r+=chunk
                reply=json.loads(r)
                if text.startswith('present_snapshot '):
                    assert reply['schema_version']==1 and reply['pid']==p.pid
                return reply
        try:
            marker(b'snapshot fixture ready')
            if case in ('child','target'):assert not sock.exists()
            elif case=='unarmed':assert sock.exists()
            else:
                for i in range(0 if case=='off' else 3):assert req('metadata')['status']=='ok'
                if case!='off':assert req('metadata')['status']=='ok'
                if case=='ok':
                    assert req('cpu')['status']=='ok'
                    for i in range(3):assert req('wait_snapshot')['status']=='ok'
                status=req('present_snapshot '+str(p.pid+100000))['status'];assert status==('disabled' if case=='off' else 'wrong_pid'),status
                status=req('present_snapshot '+str(p.pid))['status'];assert status==('disabled' if case=='off' else 'armed'),status
                if case!='off':assert req('present_snapshot '+str(p.pid))['status']=='already_requested'
                if case=='ok':
                    for i in range(5):assert req('present_snapshot '+str(p.pid))['status']=='already_requested'
                    deadline=time.monotonic()+2
                    while sock.exists() and time.monotonic()<deadline:time.sleep(.01)
                    assert not sock.exists()
            marker(b'snapshot fixture TEST complete')
            if case not in ('existing','symlink'):assert not out.exists()
            if case!='none':
                marker(b'snapshot fixture first present complete')
                if expected:
                    with out.open('rb') as f:header=f.read(24)
                    assert header[:8]==b'\x89PNG\r\n\x1a\n';assert struct.unpack('>II',header[16:24])==(64,48)
                    out.unlink()
                if case=='existing':assert out.read_bytes()==b'keep';out.unlink()
            marker(b'snapshot fixture presents complete');p.wait(10);assert p.returncode==0
            records=[line for line in data.splitlines() if b'present_snapshot: ' in line]
            pattern=rb'\[vxrt\] present_snapshot: pid=(\d+) stage=(claimed|readback|encode|write|done) format=(\d+) width=(\d+) height=(\d+) category=([a-z_]+)'
            parsed=[re.fullmatch(pattern,line) for line in records]
            assert len(records)<=5 and all(parsed),records
            failure={'unsupported':'unsupported_format','existing':'file_create','symlink':'file_create','parent':'path_open'}
            if expected or case in failure:
                stages=['claimed','readback']+([] if case=='unsupported' else ['encode','write'])+['done']
                assert [m[2].decode() for m in parsed]==stages,records
                assert all(int(m[1])==p.pid and int(m[3])>0 and (int(m[4]),int(m[5]))==(64,48) for m in parsed),records
                assert len({(m[3],m[4],m[5]) for m in parsed})==1,records
                assert all(m[6]==b'none' for m in parsed[:-1]),records
                assert parsed[-1][6].decode()==failure.get(case,'ok'),records
            else:assert not records,records
            if expected or case=='existing':assert not out.exists()
            elif case=='symlink':assert out.is_symlink() and not (root/'absent').exists()
            else:assert not out.exists(),case
            assert b'centre texel' not in data
            print('PASS',case,flush=True)
        finally:
            if p.poll() is None:p.terminate();p.wait(5)
if __name__=='__main__':
    for case in sys.argv[1:] or ['ok','repo','none','unsupported','existing','symlink','parent','child','target','off','unarmed']:run(case)
