"""Full-frame animation benchmark summary; standard library only."""
import csv
import statistics
import sys
from pathlib import Path

def percentile(values, p):
    a=sorted(values)
    n=(len(a)-1)*p/100
    lo=int(n)
    return a[lo]+(a[min(lo+1,len(a)-1)]-a[lo])*(n-lo)

directory=Path(sys.argv[1])
lines=['| Mode/run | Frames | Frame p50 | Frame p95 | Frame p99 | Animation p50 | Animation p95 | Render submit p50 |',
       '|---|---:|---:|---:|---:|---:|---:|---:|']
groups={}
for path in sorted(directory.glob('*.csv')):
    text=path.read_text().splitlines()
    meta=dict(line[2:].split('=',1) for line in text if line.startswith('# '))
    if meta.get('complete')!='1': raise ValueError(f'Incomplete run: {path}')
    rows=list(csv.DictReader(line for line in text if not line.startswith('#')))
    frame=[float(r['frame_ms']) for r in rows]
    animation=[float(r['animation_ms']) for r in rows]
    render=[float(r['render_submit_ms']) for r in rows]
    metrics=[percentile(frame,50),percentile(frame,95),percentile(frame,99),percentile(animation,50),percentile(animation,95),percentile(render,50)]
    groups.setdefault(meta['mode'],[]).append(metrics)
    lines.append('| '+path.stem+' | '+str(len(rows))+' | '+' | '.join(f'{v:.3f}' for v in metrics)+' |')
for mode,runs in groups.items():
    metrics=[statistics.median(column) for column in zip(*runs)]
    lines.append('| '+mode+' median of runs | - | '+' | '.join(f'{v:.3f}' for v in metrics)+' |')
result='All timings in milliseconds.\n\n'+'\n'.join(lines)+'\n'
(directory/'summary.md').write_text(result,encoding='utf-8')
print(result)
