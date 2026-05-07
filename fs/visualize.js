const drawer = (canvasId) => {
  const cnv = document.getElementById(canvasId);
  const ctx = cnv.getContext('2d');
  const W = cnv.width, H = cnv.height;
  const ATTR = 'tank_percentage';
  let pct = +cnv.getAttribute(ATTR) || 0;
  new MutationObserver(ml => {
    for (const m of ml)
      if (m.attributeName === ATTR) pct = +m.target.getAttribute(ATTR);
  }).observe(cnv, {attributes: true});

  const N = 24, R = 0.7, HL = 1.1;

  const verts = [];
  for (let i = 0; i < N; i++) {
    const a = 2 * Math.PI * i / N;
    verts.push([R*Math.cos(a), R*Math.sin(a),  HL],
               [R*Math.cos(a), R*Math.sin(a), -HL]);
  }

  const faces = [];
  for (let i = 0; i < N; i++) {
    const j = (i+1)%N, a = Math.PI*(2*i+1)/N;
    faces.push({v:[i*2,j*2,j*2+1,i*2+1], n:[Math.cos(a),Math.sin(a),0]});
  }

  const xf    = ([x,y,z],m) => [m[0]*x+m[1]*y+m[2]*z+m[3], m[4]*x+m[5]*y+m[6]*z+m[7], m[8]*x+m[9]*y+m[10]*z+m[11]];
  const proj  = ([x,y,z])   => [W/2+W*x/z, H/2+H*y/z];
  const dot   = (a,b)       => a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
  const mean  = (arr,fn)    => arr.reduce((s,v)=>s+fn(v),0)/arr.length;
  const shade = (base,t)    => `rgb(${base.map(c=>(c*t)|0)})`;

  // poly: fill + stroke same colour (closes gaps between adjacent wall quads)
  const poly = (pts, col) => {
    ctx.beginPath(); ctx.moveTo(...pts[0]);
    pts.slice(1).forEach(p => ctx.lineTo(...p));
    ctx.closePath();
    ctx.fillStyle = ctx.strokeStyle = col;
    ctx.fill(); ctx.stroke();
  };

  // fill: fill only — used for cap faces to avoid chord-line stroke artefact
  const fill = (pts, col) => {
    ctx.beginPath(); ctx.moveTo(...pts[0]);
    pts.slice(1).forEach(p => ctx.lineTo(...p));
    ctx.closePath();
    ctx.fillStyle = col;
    ctx.fill();
  };

  const LIGHT = [0.57, -0.57, -0.57];
  const WET   = [40, 120, 220];
  const DRY   = [170, 170, 175];
  const WALL  = [210, 215, 220];

  function drawCapFace(capZ, thr, mat) {
    const ring = [];
    for (let i = 0; i < N; i++) {
      const a = 2*Math.PI*i/N;
      ring.push({p: proj(xf([R*Math.cos(a), R*Math.sin(a), capZ], mat)), y: R*Math.sin(a)});
    }
    const all = ring.map(r => r.p);

    fill(all, shade(WET, 0.85));
    if (thr >= R)  { fill(all, shade(DRY, 0.85)); return; }
    if (thr < -R)  { return; }

    let ds = -1;
    for (let i = 0; i < N; i++)
      if (ring[(i-1+N)%N].y > thr && ring[i].y <= thr) { ds = i; break; }

    if (ds < 0) { if (ring[0].y <= thr) fill(all, shade(DRY, 0.85)); return; }

    const dryPts = [];
    for (let k = 0; k < N; k++) {
      const i = (ds+k)%N;
      if (ring[i].y > thr) break;
      dryPts.push(ring[i].p);
    }

    const cx = Math.sqrt(Math.max(0, R*R - thr*thr));
    const cp1 = proj(xf([ cx, thr, capZ], mat));
    const cp2 = proj(xf([-cx, thr, capZ], mat));
    fill([cp1, ...dryPts, cp2], shade(DRY, 0.85));
  }

  function update() {
    const ang = Date.now()/950, s = Math.sin(ang), c = Math.cos(ang);
    const mat = [c,0,-s,0, 0,1,0,0, s,0,c,5];
    ctx.fillStyle = '#f3f3f3';
    ctx.fillRect(0, 0, W, H);

    const tv  = verts.map(p => xf(p, mat));
    const thr = (100-pct)*2*R/100-R;

    const items = [];

    // Side walls — fill+stroke to close inter-panel gaps
    // Face centre local-Y (R·sin(a)) compared to thr picks wet vs dry colour
    for (const f of faces) {
      const tn  = [c*f.n[0]-s*f.n[2], f.n[1], s*f.n[0]+c*f.n[2]];
      if (tn[2] > 0) continue;
      const pts = f.v.map(i => proj(tv[i]));
      const lit = Math.max(0, dot(tn, LIGHT));
      const col = R * f.n[1] > thr
        ? shade(WET,  0.75 + 0.25*lit)
        : shade(WALL, 0.65 + 0.35*lit);
      items.push({ z: mean(f.v, i => tv[i][2]), draw: () => poly(pts, col) });
    }

    // Caps — inserted into painter's sort at their centre z so walls can
    // correctly overlap them when the cylinder is nearly side-on
    for (const [capZ, nz] of [[HL, 1], [-HL, -1]]) {
      if (c * nz >= 0) continue;  // backface cull; >= avoids double-draw at edge-on
      const z = xf([0, 0, capZ], mat)[2];
      items.push({ z, draw: () => drawCapFace(capZ, thr, mat) });
    }

    items.sort((a, b) => b.z - a.z);
    for (const item of items) item.draw();

    requestAnimationFrame(update);
  }
  update();
};

drawer('tank-visualization');
