#!/usr/bin/env python3
"""Compare fixed-camera culling on/off images on the Pi. Never install a build."""
import argparse, importlib.util, json
from pathlib import Path
from PIL import Image, ImageChops

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--root', type=Path, default=Path('/home/pi/classicube-bench'))
parser.add_argument('--name', required=True, help='Unique result prefix')
parser.add_argument('--grid', type=int, default=2, choices=[0,2,4,8])
parser.add_argument('--plane-depth', action='store_true')
parser.add_argument('--ray-cache', type=float, default=0)
parser.add_argument('--multi-draw', action='store_true')
args=parser.parse_args()
spec=importlib.util.spec_from_file_location('bench',Path(__file__).with_name('run-bench.py'))
bench=importlib.util.module_from_spec(spec); spec.loader.exec_module(bench)
bench.ROOT=args.root
base={'viewdist':512,'gfx-minviewdist':512,'gfx-targetfps':60,'gfx-preserveview':'true',
      'benchmark-freeze':'true','benchmark-preload':'true','benchmark-warmup':5,'benchmark-seconds':5,
      'gfx-chunkbuildms':4,'gfx-flushverts':65536,'gfx-hizcull':'true','gfx-hizmesh':'true',
      'gfx-hizboxes':4096,'gfx-hizbudgetus':3000,'gfx-hizvc4':'true','gfx-hizflatdepth':'true',
      'gfx-hizplanedepth':str(args.plane_depth).lower(),'gfx-hizraybounds':'true','gfx-hizrayexit':'false',
      'gfx-hizraycache':args.ray_cache,'gfx-hizreuse':'false','gfx-portalcull':'false',
      'gfx-greedy2d':'true','gfx-meshbands':0,'gfx-meshgrid':args.grid,
      'gfx-multidraw':str(args.multi_draw).lower(),'benchmark-compare-images':'true',
      'benchmark-executable':str(args.executable.resolve()),'benchmark-uncapped':'false','fpslimit':'Limit60FPS'}
cases=[]
for size in [256,512]:
    cases += [(f'{size}-spawn',{'sp-worldsize':size}),
              (f'{size}-aerial',{'sp-worldsize':size,'benchmark-x':size/2,'benchmark-y':70,
                               'benchmark-z':size/2-min(size*.3,96),'benchmark-yaw':180,'benchmark-pitch':35}),
              (f'{size}-walk',{'sp-worldsize':size,'benchmark-mode':3})]
fixtures=args.root/'fixtures'
if (fixtures/'open-shaft.cw').exists() and (fixtures/'sealed.cw').exists():
    shaft={'benchmark-x':25.5,'benchmark-y':34,'benchmark-z':28,'benchmark-yaw':0,'benchmark-pitch':75}
    cases += [('shaft',dict(shaft,**{'benchmark-map':str(fixtures/'open-shaft.cw')})),
              ('mining-opening',dict(shaft,**{'benchmark-map':str(fixtures/'sealed.cw'),'benchmark-edit-fixture':1})),
              ('mining-closing',dict(shaft,**{'benchmark-map':str(fixtures/'open-shaft.cw'),'benchmark-edit-fixture':2})),
              ('glass-cave',{'benchmark-map':str(fixtures/'sealed.cw'),'benchmark-x':25,'benchmark-y':12,
                             'benchmark-z':25,'benchmark-yaw':90,'benchmark-pitch':0}),
              ('outside',{'benchmark-map':str(fixtures/'open-shaft.cw'),'benchmark-x':-8,'benchmark-y':44,
                          'benchmark-z':25,'benchmark-yaw':90,'benchmark-pitch':25})]
results=[]
for name,settings in cases:
    run=bench.run(args.name+'-'+name,dict(base,**settings))
    assert run['ready']=={'max_pending':0,'last_pending':0},run['ready']
    assert len(run['screenshots'])==2,run['screenshots']
    a,b=[Image.open(p).convert('RGB') for p in run['screenshots']]
    assert any(hi for lo,hi in a.getextrema()) and any(hi for lo,hi in b.getextrema()),'Black capture'
    diff=ImageChops.difference(a,b)
    result={'case':name,'different_pixels':sum(any(pixel) for pixel in diff.getdata()),
            'bounds':diff.getbbox(),'images':run['screenshots'],'executable_sha256':run['executable_sha256']}
    results.append(result); print('VISUAL_AB',json.dumps(result),flush=True)
output=args.root/'results'/(args.name+'-visual-checks.json')
output.write_text(json.dumps(results,indent=2)+'\n')
assert all(r['different_pixels']==0 for r in results),f'Image mismatch: see {output}'