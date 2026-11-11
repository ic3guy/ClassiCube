from importlib.machinery import SourceFileLoader
from pathlib import Path
import json
ROOT=Path('/home/pi/classicube-bench')
b=SourceFileLoader('bench',str(ROOT/'run-bench.py')).load_module()
settings=json.loads((ROOT/'normal-release-settings.json').read_text())
keys=['viewdist','gfx-minviewdist','gfx-targetfps','gfx-preserveview','gfx-adaptiveview','sp-worldsize','gfx-chunkbuildms',
'fpslimit','gfx-flushverts','gfx-hizcull','gfx-portalcull','gfx-meshbands','gfx-meshgrid','gfx-greedy2d','gfx-farlod','gfx-lodnear','gfx-lodstep',
'gfx-atlasstream','gfx-framepacing','window-width','window-height','window-fullscreen','viewbobbing']
base={k:settings[k] for k in keys}
base.update({'benchmark-freeze':'false','benchmark-preload':'true','benchmark-warmup':10,'benchmark-seconds':120,
'benchmark-uncapped':'false','benchmark-executable':str(ROOT/'normal60-rc2-ClassiCube')})
cases=[
('walking-soak',{'benchmark-mode':3}),
('aerial-seed2',{'benchmark-mode':2,'benchmark-seed':22222,'benchmark-seconds':60}),
('new-saved-world',{'benchmark-mode':3,'benchmark-seconds':60,'benchmark-map':str(ROOT/'runs/normal-release-ui/maps/normal-60.cw')}),
('original-saved-world',{'benchmark-mode':3,'benchmark-seconds':30,'benchmark-map':'/home/pi/ClassiCube2/maps/byron-default.cw'}),
('reload-edit',{'benchmark-seconds':5,'benchmark-atlascompare':'true','benchmark-map':str(ROOT/'runs/normal-release-ui/maps/normal-qa-placed.cw')})]
for name,options in cases:
    d=b.run('normal-final-'+name,dict(base,**options))
    assert d['ready']=={'max_pending':0,'last_pending':0},d['ready']
    assert d['distance']==512,d['distance']