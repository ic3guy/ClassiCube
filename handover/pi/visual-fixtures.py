import gzip, pathlib, struct, json

ROOT=pathlib.Path('/home/pi/classicube-bench')
def tag(kind,name,payload):
    encoded=name.encode(); return bytes([kind])+struct.pack('>H',len(encoded))+encoded+payload
def short(name,n): return tag(2,name,struct.pack('>H',n))
def write_world(path,blocks):
    spawn=tag(10,'Spawn',short('X',25)+short('Y',15)+short('Z',25)+tag(1,'H',b'\0')+tag(1,'P',b'\0')+b'\0')
    raw=tag(10,'ClassicWorld',tag(1,'FormatVersion',b'\1')+short('X',64)+short('Y',64)+short('Z',64)+spawn+
            tag(7,'BlockArray',struct.pack('>i',len(blocks))+blocks)+b'\0')
    path.write_bytes(gzip.compress(raw))

def fill(blocks,x0,x1,y0,y1,z0,z1,value):
    for y in range(y0,y1):
        for z in range(z0,z1):
            start=(y*64+z)*64+x0; blocks[start:start+x1-x0]=bytes([value])*(x1-x0)

fixtures=ROOT/'fixtures'; fixtures.mkdir(exist_ok=True)
blocks=bytearray(64**3)
fill(blocks,0,64,0,32,0,64,1)
fill(blocks,0,64,32,33,0,64,2)
# Two subterranean chambers, a glass partition, a second sealed chamber.
fill(blocks,16,48,8,24,16,32,0)
fill(blocks,32,33,8,24,16,32,20)
fill(blocks,40,56,8,24,40,56,0)
fill(blocks,16,32,8,9,16,32,21)
fill(blocks,33,48,8,9,16,32,24)
fill(blocks,40,56,8,9,40,56,25)
write_world(fixtures/'sealed.cw',blocks)
opened=bytearray(blocks)
fill(opened,24,27,9,34,24,27,0)
write_world(fixtures/'open-shaft.cw',opened)

from importlib.machinery import SourceFileLoader
from PIL import Image, ImageChops
b=SourceFileLoader('bench',str(ROOT/'run-bench.py')).load_module()
base={'viewdist':512,'gfx-minviewdist':512,'gfx-targetfps':60,'gfx-chunkbuildms':4,
      'benchmark-seconds':5,'benchmark-warmup':5,'benchmark-freeze':'true','benchmark-capture':'true','benchmark-preload':'true'}
cases=[
 ('surface', 'sealed.cw', {'benchmark-x':30,'benchmark-y':34,'benchmark-z':30,'benchmark-yaw':45,'benchmark-pitch':20}),
 ('glass-cave','sealed.cw',{'benchmark-x':25,'benchmark-y':12,'benchmark-z':25,'benchmark-yaw':90,'benchmark-pitch':0}),
 ('shaft','open-shaft.cw',{'benchmark-x':25.5,'benchmark-y':34,'benchmark-z':28,'benchmark-yaw':0,'benchmark-pitch':75}),
 ('outside','open-shaft.cw',{'benchmark-x':-8,'benchmark-y':44,'benchmark-z':25,'benchmark-yaw':90,'benchmark-pitch':25}),
 ('mining-shaft','sealed.cw',{'benchmark-edit-fixture':1,'benchmark-x':25.5,'benchmark-y':34,'benchmark-z':28,'benchmark-yaw':0,'benchmark-pitch':75}),
 ('closing-shaft','open-shaft.cw',{'benchmark-edit-fixture':2,'benchmark-x':25.5,'benchmark-y':34,'benchmark-z':28,'benchmark-yaw':0,'benchmark-pitch':75}),
 ('inside-stone','sealed.cw',{'benchmark-x':25,'benchmark-y':28,'benchmark-z':25,'benchmark-yaw':90,'benchmark-pitch':0}),
]
results=[]
for name,file,pose in cases:
    screenshots=[]
    for enabled in [False,True]:
        options=dict(base,**pose); options.update({'benchmark-map':str(fixtures/file),'gfx-portalcull':str(enabled).lower(),'gfx-hizcull':str(enabled).lower(),'gfx-meshbands':8 if enabled else 0,'gfx-hizboxes':512,'gfx-flushverts':65536 if enabled else 0})
        d=b.run('visual9-'+name+'-'+str(enabled).lower(),options)
        assert d['screenshots'], f'Missing fully-built screenshot: {name} {d["ready"]}'
        screenshots.append(d['screenshots'][-1])
    a,c=[Image.open(p).convert('RGB') for p in screenshots]
    diff=ImageChops.difference(a,c)
    pixels=sum(any(v) for v in diff.getdata())
    result={'case':name,'different_pixels':pixels,'images':screenshots}
    results.append(result); print('VISUAL',json.dumps(result),flush=True)
(ROOT/'results'/'ranges9-visual-checks.json').write_text(json.dumps(results,indent=2)+'\n')
assert all(r['different_pixels']==0 for r in results), 'Image mismatch; do not deploy portal culling'
