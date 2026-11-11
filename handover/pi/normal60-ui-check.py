import ctypes as C
import ctypes.util
import os,pathlib,subprocess,time,json,gzip,struct,io,sys
from PIL import Image
root=pathlib.Path('/home/pi/classicube-bench')
cwd=root/'runs'/'normal-release-ui'
cwd.mkdir(parents=True,exist_ok=True)
if not (cwd/'texpacks').exists(): (cwd/'texpacks').symlink_to('/home/pi/ClassiCube2/texpacks',target_is_directory=True)
settings=json.loads((root/'normal-release-settings.json').read_text())
settings={k:v for k,v in settings.items() if not k.startswith('benchmark-')}
settings.update({'sp-worldsize':256,'auto-pause':'false','mode-classic':'true','soundsvolume':0,'musicvolume':0})
(cwd/'options.txt').write_text(''.join(f'{k}={v}\n' for k,v in settings.items()))
env=dict(os.environ,DISPLAY=':0',XAUTHORITY='/home/pi/.Xauthority',XDG_RUNTIME_DIR='/run/user/1000',WAYLAND_DISPLAY='wayland-0')
log=(root/'results'/'normal-release-ui.log').open('w')
p=subprocess.Popen([str(root/'normal60-rc2-ClassiCube'),'--singleplayer'],cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
x = C.CDLL(ctypes.util.find_library('X11'))
xt = C.CDLL(ctypes.util.find_library('Xtst'))
Display = C.c_void_p
Window = C.c_ulong
x.XOpenDisplay.argtypes=[C.c_char_p]; x.XOpenDisplay.restype=Display
x.XDefaultRootWindow.argtypes=[Display]; x.XDefaultRootWindow.restype=Window
x.XQueryTree.argtypes=[Display,Window,C.POINTER(Window),C.POINTER(Window),C.POINTER(C.POINTER(Window)),C.POINTER(C.c_uint)]
x.XFetchName.argtypes=[Display,Window,C.POINTER(C.c_void_p)]
x.XGetGeometry.argtypes=[Display,Window,C.POINTER(Window),C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_uint),C.POINTER(C.c_uint),C.POINTER(C.c_uint),C.POINTER(C.c_uint)]
x.XTranslateCoordinates.argtypes=[Display,Window,Window,C.c_int,C.c_int,C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(Window)]
x.XFree.argtypes=[C.c_void_p]
x.XFlush.argtypes=[Display]
x.XCloseDisplay.argtypes=[Display]
x.XSetInputFocus.argtypes=[Display,Window,C.c_int,Window]
xt.XTestFakeKeyEvent.argtypes=[Display,C.c_uint,C.c_int,C.c_ulong]
xt.XTestFakeMotionEvent.argtypes=[Display,C.c_int,C.c_int,C.c_int,C.c_ulong]
xt.XTestFakeButtonEvent.argtypes=[Display,C.c_uint,C.c_int,C.c_ulong]
d=x.XOpenDisplay(b':0')
assert d, 'X display unavailable'
rw=x.XDefaultRootWindow(d)

def find(w, depth=0):
    name=C.c_void_p()
    if x.XFetchName(d,w,C.byref(name)) and name:
        text=C.string_at(name).decode(errors='replace'); x.XFree(name)
        if text.startswith('ClassiCube'): return w
    if depth>5: return None
    r=Window(); parent=Window(); children=C.POINTER(Window)(); n=C.c_uint()
    if not x.XQueryTree(d,w,C.byref(r),C.byref(parent),C.byref(children),C.byref(n)): return None
    windows=[children[i] for i in range(n.value)]
    if children: x.XFree(children)
    for child in windows:
        found=find(child,depth+1)
        if found: return found

x.XStringToKeysym.argtypes=[C.c_char_p]; x.XStringToKeysym.restype=C.c_ulong
x.XKeysymToKeycode.argtypes=[Display,C.c_ulong]; x.XKeysymToKeycode.restype=C.c_uint
def key(name):
    code=x.XKeysymToKeycode(d,x.XStringToKeysym(name.encode()))
    assert code,name
    xt.XTestFakeKeyEvent(d,code,1,0); xt.XTestFakeKeyEvent(d,code,0,0); x.XFlush(d); time.sleep(.025)
def text(value):
    for ch in value: key({' ':'space','/':'slash','-':'minus','.':'period'}.get(ch,ch))
def click(px,py):
    xt.XTestFakeMotionEvent(d,-1,sx.value+int(px),sy.value+int(py),0); x.XFlush(d); time.sleep(.15)
    xt.XTestFakeButtonEvent(d,1,1,0); xt.XTestFakeButtonEvent(d,1,0,0); x.XFlush(d); time.sleep(.5)
def capture(name):
    path=root/'results'/('normal-release-ui-'+name+'.png')
    raw=root/'results'/'normal-release-ui-raw.png'
    subprocess.run(['grim',str(raw)],env=env,check=True)
    with Image.open(raw) as im: im.crop((sx.value,sy.value,sx.value+width.value,sy.value+height.value)).save(path)
    return str(path)
def payload(f,kind):
    if kind in (1,2,3,4,5,6):
        spec={1:'>b',2:'>h',3:'>i',4:'>q',5:'>f',6:'>d'}[kind]
        return struct.unpack(spec,f.read(struct.calcsize(spec)))[0]
    if kind==7: return f.read(struct.unpack('>i',f.read(4))[0])
    if kind==8: return f.read(struct.unpack('>H',f.read(2))[0]).decode('utf8')
    if kind==9:
        sub=f.read(1)[0]; n=struct.unpack('>i',f.read(4))[0]
        return [payload(f,sub) for _ in range(n)]
    if kind==10:
        value={}
        while True:
            sub=f.read(1)[0]
            if not sub: return value
            name=payload(f,8); value[name]=payload(f,sub)
    if kind in (11,12):
        n=struct.unpack('>i',f.read(4))[0]; return f.read(n*(4 if kind==11 else 8))
    raise ValueError(kind)
def world(path):
    f=io.BytesIO(gzip.decompress(path.read_bytes())); assert f.read(1)==b'\n'; payload(f,8)
    result=payload(f,10); assert (result['X'],result['Y'],result['Z'])==(256,64,256),result.keys()
    return result
def save(name):
    key('Escape'); time.sleep(.5); click(width.value/2,height.value/2)
    key('End')
    for _ in range(48): key('BackSpace')
    text(name); key('Return'); time.sleep(1)
    path=cwd/'maps'/(name+'.cw'); assert path.exists(),capture('save-failed')
    world(path); key('Escape'); time.sleep(.3)
    return path
def command(value):
    key('t'); text(value); key('Return'); time.sleep(.7)
try:
    time.sleep(20)
    w=find(rw); assert w,'Game window not found'
    r=Window(); gx=C.c_int(); gy=C.c_int(); width=C.c_uint(); height=C.c_uint(); border=C.c_uint(); depth=C.c_uint()
    assert x.XGetGeometry(d,w,C.byref(r),C.byref(gx),C.byref(gy),C.byref(width),C.byref(height),C.byref(border),C.byref(depth))
    child=Window(); sx=C.c_int(); sy=C.c_int()
    x.XTranslateCoordinates(d,w,rw,0,0,C.byref(sx),C.byref(sy),C.byref(child))
    x.XSetInputFocus(d,w,1,0); x.XFlush(d)
    images=[capture('game')]
    initial=save('normal-60')
    command('/client place 1 128 55 128')
    placed=save('normal-qa-placed')
    index=(55*256+128)*256+128
    assert world(placed)['BlockArray'][index]==1,'Placed block did not persist'
    command('/client place 0 128 55 128')
    removed=save('normal-qa-removed')
    assert world(removed)['BlockArray'][index]==0,'Removed block did not persist'
    command('/client tp 8 40 8'); time.sleep(2)
    images.append(capture('teleport'))
    command('/client tp 128 40 128'); time.sleep(2)
    key('Escape'); time.sleep(.5); images.append(capture('pause'))
    click(width.value-65,height.value-25)
    code=p.wait(timeout=10); assert code==0,code
    result={'passed':True,'world':str(initial),'dimensions':[256,64,256],
            'placed_block_saved':True,'removed_block_saved':True,'teleports':2,'quit_exit':code,'images':images}
    (root/'results'/'normal-release-ui.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result),flush=True)
finally:
    if p.poll() is None: p.terminate(); p.wait(timeout=5)
    x.XCloseDisplay(d); log.close()