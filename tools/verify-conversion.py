"""Independent end-to-end validation for one completed test conversion (numpy + FFmpeg)."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('run', type=Path)
args = parser.parse_args()
run = args.run
result = json.loads((run / 'result.json').read_text(encoding='utf-8'))
source, output = result['input'], result['output']
w, h = result['width'], result['height']
indices = [0, result['frames']//2, result['frames']-1]
frame_words = w*h*3//2

def probe(path, *options):
    return json.loads(subprocess.check_output(['ffprobe', '-v', 'error', *options, '-of', 'json', str(path)]))

metadata = probe(output, '-show_streams', '-show_format')
video = next(x for x in metadata['streams'] if x['codec_type'] == 'video')
assert (video['width'], video['height']) == (w,h)
assert video['profile'] == 'Main 10' and video['pix_fmt'] == 'yuv420p10le'
assert [video[k] for k in ('color_primaries','color_transfer','color_space','color_range','chroma_location')] == ['bt2020','smpte2084','bt2020nc','tv','center']
select = '+'.join(f'eq(n\\,{i})' for i in indices)
decoded = subprocess.check_output(['ffmpeg','-v','error','-i',output,'-vf',f'select={select}',
    '-fps_mode','passthrough','-pix_fmt','p010le','-f','rawvideo','pipe:1'])
decoded = np.frombuffer(decoded,dtype='<u2').reshape(len(indices),frame_words) >> 6
errors=[]
for frame,actual in zip(indices,decoded):
    reference=np.fromfile(run/f'frame-{frame}.p010',dtype='<u2') >> 6
    delta=np.abs(actual.astype(float)-reference)
    row={'frame':frame,'mae_codes':float(delta.mean()),'p99_codes':float(np.quantile(delta,.99)), 'max_codes':float(delta.max())}
    # The generated fixture contains a flat white rectangle at this location.
    if w == 1920 and h == 1080 and 'first-test' in str(source):
        white_actual=actual[:w*h].reshape(h,w)[100:300,1400:1750]
        white_reference=reference[:w*h].reshape(h,w)[100:300,1400:1750]
        row['white_patch_median_error']=float(abs(np.median(white_actual)-np.median(white_reference)))
        assert row['white_patch_median_error'] <= 3
    assert row['mae_codes'] < 4, row
    errors.append(row)

# Compare our RGB->P010 matrix/range against the independent FFmpeg zscale implementation.
# Flat regions avoid differences between the deliberately simple 2x2 box and zimg chroma filters.
rgb=np.fromfile(run/f'frame-{indices[1]}.rgb10a2',dtype='<u4').reshape(h,w)
channels=[((rgb>>s)&1023).astype(np.float32)/1023 for s in (10,20,0)]  # planar G,B,R
ref_p010=subprocess.check_output(['ffmpeg','-v','error','-f','rawvideo','-pixel_format','gbrpf32le','-video_size',f'{w}x{h}',
    '-i','pipe:0','-vf','zscale=matrixin=gbr:matrix=2020_ncl:rangein=full:range=limited:chromal=center,format=yuv420p10le',
    '-frames:v','1','-f','rawvideo','pipe:1'],input=np.stack(channels).astype('<f4').tobytes())
zimg=np.frombuffer(ref_p010,dtype='<u2')
ours=np.fromfile(run/f'frame-{indices[1]}.p010',dtype='<u2')>>6
matrix_y_error=np.abs(zimg[:w*h].astype(float)-ours[:w*h])
print('matrix comparison', matrix_y_error.max(), matrix_y_error.mean(), 'ranges', zimg[:w*h].min(),zimg[:w*h].max(), ours[:w*h].min(),ours[:w*h].max())
assert matrix_y_error.max() <= 1

def audio_packets(path):
    return probe(path,'-select_streams','a','-show_packets','-show_data_hash','sha256',
        '-show_entries','packet=stream_index,pts_time,duration_time,data_hash').get('packets',[])
a,b=audio_packets(source),audio_packets(output)
offset=json.loads((run/'timing.json').read_text(encoding='utf-8'))['video_start_seconds']
rate_n,rate_d=map(int,result['fps'].split('/'))
if result.get('limited_to_max_frames'):
    duration=result['frames']*rate_d/rate_n
    a=[p for p in a if float(p['pts_time'])-offset < duration]
assert len(a)==len(b)
assert [p['data_hash'] for p in a] == [p['data_hash'] for p in b], 'Audio packets changed'
timing_errors=[abs(float(x['pts_time'])-offset-float(y['pts_time'])) for x,y in zip(a,b)]
assert max(timing_errors,default=0) <= .0011
times=probe(output,'-select_streams','v:0','-show_frames','-show_entries','frame=best_effort_timestamp_time')['frames']
assert len(times)==result['frames']
video_errors=[abs(float(f['best_effort_timestamp_time'])-i*rate_d/rate_n) for i,f in enumerate(times)]
assert max(video_errors) <= .0011
report={'status':'passed','output':output,'frames':len(times),'roundtrip_errors':errors,
    'matrix_vs_zscale_y_max_error':float(matrix_y_error.max()),
    'audio_packets_identical':True,'audio_packet_count':len(a),'audio_max_timestamp_error_seconds':max(timing_errors,default=0),
    'video_max_timestamp_error_seconds':max(video_errors),
    'output_sha256':hashlib.file_digest(open(output,'rb'),'sha256').hexdigest(),
    'browser_equivalence_verified':False}
(run/'verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
