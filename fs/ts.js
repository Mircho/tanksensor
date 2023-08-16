let tsWS = null;

const processWSData = (tsState) => {
  Object.entries(tsState).forEach(([key, value]) => {
    const el = document.querySelector(`#${key}`);
    if (el) {
      if (key == 'timestamp') {
        const tsDate = new Date(value * 1000);
        const tsHoursStr = new String(tsDate.getHours());
        const tsMinutesStr = new String(tsDate.getMinutes());
        const tsSecondsStr = new String(tsDate.getSeconds());
        el.innerHTML = tsHoursStr.padStart(2, '0') + ':' + tsMinutesStr.padStart(2, '0') + ':' + tsSecondsStr.padStart(2, '0');
        return;
      }
      el.innerHTML = value;
    }
  })
}

const connectToHostWS = () => {
  let reconnect = null;
  console.log('Connecting to: ', window.location.host);
  tsWS = new WebSocket(`ws://${window.location.host}`);
  tsWS.addEventListener('message', (event) => {
    try {
      let tsState = JSON.parse(event.data);
      processWSData(tsState);
      console.log(tsState);
    } catch (e) {
      console.log('Error parsing data', e);
    }
  });
  tsWS.addEventListener('close', (event) => {
    console.log('WebSocket disconnect...');
    if(reconnect == null) {
      reconnect = setTimeout(connectToHostWS, 2000);
    }
  })
  tsWS.addEventListener('error', (event) => {
    console.log('WebSocket error...');
    if(reconnect == null) {
      reconnect = setTimeout(connectToHostWS, 2000);
    }
  })
}

let deviceConfig = null;

const updateConfigForms = () => {
  if(deviceConfig == null) return;
  document.querySelector('#config-tank-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    event.submitter.ariaBusy = "true";
    let formData = new FormData(event.target);
    // console.log(formData);
    let postData = {};
    for(const item of formData) {
      let key = item[0].replace('tank-','');
      key = key.replace('-', '_');
      postData[key] = parseFloat(item[1]);
    };
    let saveResult = await fetch(`http://${window.location.host}/rpc/tank.setlimits`, {
      method: 'POST',
      cache: 'no-cache',
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify(postData)
    });
    let saveResultJSON = await saveResult.json();
    event.submitter.ariaBusy = "false";
    return saveResultJSON;
  })

  document.querySelector('#tank-low-thr').addEventListener('change', (event)=>{
    document.querySelector('#tank-low-thr-value').textContent = event.target.value;
  });
  document.querySelector('#tank-high-thr').addEventListener('change', (event)=>{
    document.querySelector('#tank-high-thr-value').textContent = event.target.value;
  });
  document.querySelector('#tank-low-thr').value = deviceConfig.tank.liters.low_threshold;
  document.querySelector('#tank-low-thr').dispatchEvent(new Event('change'));
  document.querySelector('#tank-high-thr').value = deviceConfig.tank.liters.high_threshold;
  document.querySelector('#tank-high-thr').dispatchEvent(new Event('change'));


  document.querySelector('#config-pressure-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    event.submitter.ariaBusy = "true";
    let formData = new FormData(event.target);
    // console.log(formData);
    let postData = {};
    for(const item of formData) {
      let key = item[0].replace('pressure-','');
      key = key.replace('-', '_');
      postData[key] = parseFloat(item[1]);
    };
    let saveResult = await fetch(`http://${window.location.host}/rpc/pressure.setlimits`, {
      method: 'POST',
      cache: 'no-cache',
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify(postData)
    });
    let saveResultJSON = await saveResult.json();
    event.submitter.ariaBusy = "false";
    return saveResultJSON;
  })

  document.querySelector('#pressure-low-thr').addEventListener('change', (event)=>{
    document.querySelector('#pressure-low-thr-value').textContent = event.target.value;
  });
  document.querySelector('#pressure-high-thr').addEventListener('change', (event)=>{
    document.querySelector('#pressure-high-thr-value').textContent = event.target.value;
  });
  document.querySelector('#pressure-low-thr').value = deviceConfig.tank.adc_pressure.low_threshold;
  document.querySelector('#pressure-low-thr').dispatchEvent(new Event('change'));
  document.querySelector('#pressure-high-thr').value = deviceConfig.tank.adc_pressure.high_threshold;
  document.querySelector('#pressure-high-thr').dispatchEvent(new Event('change'));

  document.querySelector('#config-counter-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    event.submitter.ariaBusy = "true";
    let formData = new FormData(event.target);
    // console.log(formData);
    let postData = {};
    for(const item of formData) {
      let key = item[0].replace('counter-','');
      key = key.replace('-', '_');
      postData[key] = parseFloat(item[1]);
    };
    let saveResult = await fetch(`http://${window.location.host}/rpc/counter.setlimits`, {
      method: 'POST',
      cache: 'no-cache',
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify(postData)
    });
    let saveResultJSON = await saveResult.json();
    event.submitter.ariaBusy = "false";
    return saveResultJSON;
  })

  document.querySelector('#counter-freq-thr').addEventListener('change', (event)=>{
    document.querySelector('#counter-freq-thr-value').textContent = event.target.value;
  });
  document.querySelector('#counter-freq-thr').value = deviceConfig.tank.frequency.high_threshold;
  document.querySelector('#counter-freq-thr').dispatchEvent(new Event('change'));

}


const loadDeviceConfig = async () => {
  let config = await fetch(`http://${window.location.host}/rpc/config.get`)
  deviceConfig = await config.json();
  updateConfigForms();
  console.log(deviceConfig);
}

window.addEventListener('load', (event) => {
  connectToHostWS();
  loadDeviceConfig();
})