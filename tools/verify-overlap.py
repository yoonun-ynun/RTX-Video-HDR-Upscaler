"""Compare serial/overlapped outputs: decoded frame hashes, timestamps, HDR tags and audio packets."""
import argparse, json, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('serial');p.add_argument('overlap');p.add_argument('--frames',type=int,required=True);a=p.parse_args()
def probe(path,*args):
    return json.loads(subprocess.check_output(['ffprobe','-v','error',*args,'-of','json',path]))
def frames(path):
    data=subprocess.check_output(['ffmpeg','-v','error','-i',path,'-map','0:v:0','-fps_mode','passthrough','-f','framemd5','-']).decode()
    return [s.strip() for s in data.splitlines() if s and not s.startswith('#')]
serial=frames(a.serial);overlap=frames(a.overlap)
assert len(serial)==len(overlap)==a.frames, (len(serial),len(overlap))
assert serial==overlap,'Decoded frames or timestamps differ'
for path in [a.serial,a.overlap]:
    v=probe(path,'-select_streams','v:0','-show_streams')['streams'][0]
    assert [v[k] for k in ['profile','pix_fmt','color_primaries','color_transfer','color_space','color_range','chroma_location']]==['Main 10','yuv420p10le','bt2020','smpte2084','bt2020nc','tv','center']
def audio(path):
    return probe(path,'-select_streams','a','-show_packets','-show_data_hash','sha256','-show_entries','packet=pts,dts,duration,data_hash')['packets']
assert audio(a.serial)==audio(a.overlap),'Audio packets or timestamps differ'
print(f'PASS: {a.frames} decoded frames byte-identical with matching timestamps; HDR tags and audio packets match')
