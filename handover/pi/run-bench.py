#!/usr/bin/env python3
"""Run an isolated hardware benchmark; preserve the installed game's settings."""
import json, os, pathlib, statistics, subprocess, sys, time, shutil, re, hashlib

ROOT = pathlib.Path('/home/pi/classicube-bench')
SOURCE = pathlib.Path('/home/pi/classicube-autotune')

def percentile(values, p):
    return sorted(values)[min(len(values)-1, int((len(values)-1)*p))]

def run(name, settings):
    cwd = ROOT / 'runs' / name
    cwd.mkdir(parents=True, exist_ok=True)
    tex = cwd / 'texpacks'
    if not tex.exists():
        tex.symlink_to('/home/pi/ClassiCube2/texpacks', target_is_directory=True)
    options = dict(line.split('=', 1) for line in (ROOT/'original-options.txt').read_text().splitlines() if '=' in line)
    options.update({'benchmark-mode':1, 'benchmark-seconds':15, 'benchmark-warmup':10,
                    'auto-pause':'false', 'window-width':854, 'window-height':480,
                    'window-fullscreen':'false', 'viewdist':512, 'sp-worldsize':256,
                    'gfx-drawmergegap':64, 'musicvolume':0, 'soundsvolume':0,
                    'viewbobbing':'false', 'fpslimit':'LimitNone'})
    options.update(settings)
    map_path = options.pop('benchmark-map', None)
    executable = options.pop('benchmark-executable', str(SOURCE/'ClassiCube'))
    binary_hash=hashlib.sha256(pathlib.Path(executable).read_bytes()).hexdigest()
    args = [str(executable), '--singleplayer']
    if map_path:
        # Load an isolated copy, so an existing saved world is never written.
        shutil.copy2(map_path, cwd/'benchmark-map.cw')
        args = [str(executable), './benchmark-map.cw']
    (cwd/'options.txt').write_text(''.join(f'{k}={v}\n' for k,v in options.items()))
    log = ROOT/'results'/f'{name}.log'
    env = dict(os.environ, DISPLAY=':0', XAUTHORITY='/home/pi/.Xauthority')
    with log.open('w') as out:
        started_at=time.time()
        process = subprocess.Popen(args, cwd=cwd, env=env, stdout=out, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=max(180,int(options['benchmark-seconds'])+int(options['benchmark-warmup'])+120))
        except subprocess.TimeoutExpired:
            process.terminate()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
            raise RuntimeError(f'{name}: timed out, see {log}')
    text = log.read_text(errors='replace')
    rows = [[int(x) for x in line.split(' ',1)[1].split(',')] for line in text.splitlines() if line.startswith('BENCH_ROW ')]
    if code or not rows:
        raise RuntimeError(f'{name}: exit={code}, no results: {text[-2000:]}')
    ready=re.search(r'BENCH_READY max_pending=(\d+) last_pending=(\d+)',text)
    heights={m[0]:int(m[1]) for m in re.findall(r'BENCH_HEIGHT y=(\d+) submitted_vertices=(\d+)',text)}
    meshes=re.search(r'BENCH_MESH before_vertices=(\d+) after_vertices=(\d+)',text)
    hiz=re.search(r'^BENCH_HIZ (.*)$',text,re.M)
    if int(options.get('gfx-minviewdist',0))>=int(options['viewdist']):
        assert all(r[8]==int(options['viewdist']) for r in rows), 'View distance changed during fixed-distance test'
    frame = [r[0]/1000 for r in rows]
    result = {'name':name, 'settings':settings, 'executable_sha256':binary_hash, 'samples':len(rows), 'fps':round(1000/statistics.mean(frame),2),
              'p50_ms':round(percentile(frame,.5),2), 'p95_ms':round(percentile(frame,.95),2),
              'p99_ms':round(percentile(frame,.99),2), 'max_ms':round(max(frame),2),
              'over_20ms_pct':round(sum(f>20 for f in frame)*100/len(frame),2),
              'logic_ms':round(statistics.mean(r[1] for r in rows)/1000,3),
              'draw_ms':round(statistics.mean(r[2] for r in rows)/1000,3),
              'wait_ms':round(statistics.mean(r[3] for r in rows)/1000,3),
              'present_ms':round(statistics.mean(r[4] for r in rows)/1000,3),
              'build_ms':round(statistics.mean(r[5] for r in rows)/1000,3),
              'draws':round(statistics.mean(r[6] for r in rows)),
              'vertices':round(statistics.mean(r[7] for r in rows)),
              'distance':round(statistics.mean(r[8] for r in rows),1),
              'metadata':[l for l in text.splitlines() if l.startswith('BENCH_META')],
              'ready':{'max_pending':int(ready[1]),'last_pending':int(ready[2])} if ready else None,
              'submitted_vertices_by_y':heights,
              'mesh_vertices':{'before':int(meshes[1]),'after':int(meshes[2])} if meshes else None,
              'hiz':{k:int(v) if v.isdigit() else v for k,v in re.findall(r'(\w+)=([^ ]+)',hiz[1])} if hiz else None,
              'screenshots':[str(p) for p in sorted((cwd/'screenshots').glob('*.png')) if p.stat().st_mtime>=started_at]}
    (ROOT/'results'/f'{name}.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result),flush=True)
    return result

if __name__ == '__main__':
    run(sys.argv[1], json.loads(sys.argv[2]) if len(sys.argv)>2 else {})
