"""Analyze diagnostic raw output. Requires numpy; never modifies original frames."""
import json
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
def rgb(name):
    p = np.fromfile(ROOT / 'artifacts' / name / 'frame-119.rgb10a2', dtype='<u4').reshape(1080, 1920)
    return np.stack([(p >> shift) & 1023 for shift in (0, 10, 20)], -1).astype(np.float64)

def pq_to_nits(code):
    v = np.power(code / 1023, 1 / (2523 / 32))
    return 10000 * np.power(np.maximum(v - 3424 / 4096, 0) / (2413 / 128 - (2392 / 128) * v), 1 / (2610 / 16384))

off, on = rgb('hdr-sdr-off'), rgb('hdr-sdr-on')
bars = rgb('hdr-bars-on')[540, 120::240]
linear = pq_to_nits(bars)
# Linear BT.2020 RGB -> XYZ D65. Chromaticity comparison is independent of absolute luminance.
matrix = np.array([[.6369580483,.1446169036,.1688809752], [.2627002120,.6779980715,.0593017165], [0,.0280726930,1.0609850577]])
xyz = linear @ matrix.T
xy = xyz[:, :2] / np.maximum(xyz.sum(axis=1, keepdims=True), 1e-12)
report = {
    'effect_mae_codes': float(np.abs(on-off).mean()),
    'on_white_codes': on[540,-1].tolist(),
    'on_white_nits_pq': pq_to_nits(on[540,-1]).tolist(),
    'pq_requested_equals_sdr_requested': bool(np.array_equal(on, rgb('hdr-pq-on'))),
    'on_first_last_equal': (ROOT/'artifacts/hdr-sdr-on/frame-0.rgb10a2').read_bytes() == (ROOT/'artifacts/hdr-sdr-on/frame-119.rgb10a2').read_bytes(),
    'bar_codes': bars.tolist(), 'bar_xy_assuming_bt2020_pq': xy.tolist(),
    'interpretation': 'BT.2020/PQ interpretation supported by HDR peak, primary chromaticities, and renderer presentation convention; browser equivalence not tested',
}
path = ROOT / 'artifacts/hdr-analysis.json'
path.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
