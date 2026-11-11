from importlib.machinery import SourceFileLoader
from pathlib import Path
from PIL import Image,ImageChops
import json
ROOT=Path('/home/pi/classicube-bench')
b=SourceFileLoader('bench',str(ROOT/'run-bench.py')).load_module()
base={'viewdist':512,'gfx-minviewdist':512,'gfx-targetfps':60,'gfx-chunkbuildms':4,
      'benchmark-freeze':'true','benchmark-preload':'true','benchmark-warmup':2,'benchmark-seconds':5,
      'gfx-flushverts':65536,'gfx-hizcull':'true','gfx-hizboxes':4096,'gfx-hizmesh':'true','gfx-hizvc4':'true','gfx-meshbands':8,'gfx-portalcull':'false',
      'benchmark-compare-images':'true'}
cases=[('shaft',{'benchmark-map':str(ROOT/'fixtures/open-shaft.cw'),'benchmark-x':25.5,'benchmark-y':34,'benchmark-z':28,'benchmark-yaw':0,'benchmark-pitch':75})]
for size in [256,512]:
    cases += [(f'{size}-spawn',{'sp-worldsize':size}),
              (f'{size}-aerial',{'sp-worldsize':size,'benchmark-x':size/2,'benchmark-y':70,'benchmark-z':size/2-min(size*.3,96),'benchmark-yaw':180,'benchmark-pitch':35})]
results=[]
for name,pose in cases:
    d=b.run('visual12-'+name,dict(base,**pose))
    assert len(d['screenshots'])==2,d['screenshots']
    a,c=[Image.open(p).convert('RGB') for p in d['screenshots']]
    diff=ImageChops.difference(a,c)
    result={'name':name,'different_pixels':sum(any(v) for v in diff.getdata()),'bounds':diff.getbbox(),'images':d['screenshots']}
    results.append(result); print('VISUAL_AB',json.dumps(result),flush=True)
(ROOT/'results'/'vc4-visual-checks.json').write_text(json.dumps(results,indent=2)+'\n')
