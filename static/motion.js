/* Window summaries, not reconstructed raw vibration or displacement. */
window.drawMotion = function (devices, history, selected, connected) {
  const host = document.getElementById('motion');
  host.replaceChildren();
  const number = x => typeof x === 'number' && Number.isFinite(x) && x >= 0;
  const svgNS = 'http://www.w3.org/2000/svg';
  for (const device of devices.filter(d => !selected || d.device_id === selected)) {
    const imu = device.telemetry?.imu;
    const section = document.createElement('section');
    section.className = 'motion-device';
    const title = document.createElement('h3'); title.textContent = device.device_id; section.append(title);
    const age = Date.now() - Date.parse(device.last_seen);
    const fresh = Number.isFinite(age) && age < 15000;
    if (!imu || !number(imu.rms_movement)) {
      const empty = document.createElement('p'); empty.className = 'telemetry-empty';
      empty.textContent = 'No motion measurement in the latest message.'; section.append(empty); host.append(section); continue;
    }
    const scene = document.createElement('div'); scene.className = 'motion-scene';
    const model = document.createElement('div'); model.className = 'motion-model'; model.textContent = '▣';
    scene.append(model); section.append(scene);
    // A bounded illustration of measured intensity, never physical displacement.
    if (fresh && !matchMedia('(prefers-reduced-motion: reduce)').matches && imu.rms_movement > 0) {
      const amplitude = Math.min(16, imu.rms_movement * 24);
      model.animate([{transform:`translateX(${-amplitude}px) rotate(-3deg)`},
        {transform:`translateX(${amplitude}px) rotate(3deg)`},
        {transform:'translateX(0) rotate(0)'}], {duration:220, iterations:5});
    }
    const value = document.createElement('p'); value.className = 'motion-value';
    value.textContent = `${imu.rms_movement.toFixed(3)} g RMS · ${fresh ? 'latest' : 'last received'} · ${new Date(device.last_seen).toLocaleTimeString()}`;
    section.append(value);
    const note = document.createElement('p'); note.className = 'telemetry-empty';
    note.textContent = 'Animation illustrates intensity; it does not reproduce the movement.'; section.append(note);
    const samples = history.filter(p => p.device_id === device.device_id).slice(-120);
    for (const [field, label] of [['rms_movement','Movement intensity · g RMS'], ['peak_magnitude','Peak acceleration · g, includes gravity']]) {
      const heading = document.createElement('h4'); heading.textContent = label; section.append(heading);
      const data = samples.filter(p => number(p[field]) && Number.isFinite(Date.parse(p.received_at)));
      if (!data.length) continue;
      const chart = document.createElementNS(svgNS,'svg'); chart.setAttribute('viewBox','0 0 280 100');
      chart.setAttribute('role','img'); chart.setAttribute('aria-label',label + ' over received time');
      const max = Math.max(field === 'peak_magnitude' ? 1 : .01, ...data.map(p=>p[field]));
      const start = Date.parse(data[0].received_at), end = Date.parse(data.at(-1).received_at);
      const x = p => end === start ? 140 : 12 + 256*(Date.parse(p.received_at)-start)/(end-start);
      const y = p => 78-60*p[field]/max;
      const baseline = document.createElementNS(svgNS,'path'); baseline.setAttribute('d','M12 10V78H268');
      baseline.setAttribute('fill','none'); baseline.setAttribute('stroke','var(--line)'); chart.append(baseline);
      if(field==='rms_movement') {
        const path=document.createElementNS(svgNS,'polyline'); path.setAttribute('points',data.map(p=>`${x(p)},${y(p)}`).join(' '));
        path.setAttribute('fill','none');path.setAttribute('stroke','var(--accent)');path.setAttribute('stroke-width','2');chart.append(path);
      }
      for(const p of data){
        const mark=document.createElementNS(svgNS,field==='peak_magnitude'?'line':'circle');
        if(field==='peak_magnitude'){mark.setAttribute('x1',x(p));mark.setAttribute('x2',x(p));mark.setAttribute('y1',78);mark.setAttribute('y2',y(p));mark.setAttribute('stroke','var(--accent)');mark.setAttribute('stroke-width','3');}
        else {mark.setAttribute('cx',x(p));mark.setAttribute('cy',y(p));mark.setAttribute('r','3');mark.setAttribute('fill','var(--accent)');}
        const tip=document.createElementNS(svgNS,'title');tip.textContent=`${new Date(p.received_at).toLocaleTimeString()} · ${p[field].toFixed(3)} g`;mark.append(tip);chart.append(mark);
      }
      section.append(chart);
      const scale=document.createElement('p');scale.className='telemetry-empty';
      scale.textContent=`0–${max.toFixed(3)} g · ${new Date(start).toLocaleTimeString()} – ${new Date(end).toLocaleTimeString()}`;section.append(scale);
    }
    host.append(section);
  }
  if(!host.childElementCount){const empty=document.createElement('p');empty.className='telemetry-empty';empty.textContent=connected?'Waiting for motion telemetry.':'Connect to see motion.';host.append(empty);}
};
