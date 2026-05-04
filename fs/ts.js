class ReconnectingWS extends EventTarget {
  _url = ""
  _ws = null
  _retryTime = 0
  _retryTimeout = undefined
  constructor(url, retryTime = 1000) {
    super();
    this._url = url;
    this._retryTime = retryTime;
    this.connect();
  }
  connect() {
    console.log('Connecting to websocket at ', this._url);
    this._ws = new WebSocket(this._url);
    this._ws.addEventListener('open', ()=>{
      this.dispatchEvent(new Event('open'));
      if(this._retryTimeout) clearTimeout(this._retryTimeout);
    });
    this._ws.addEventListener('message', (message) => {
      this.dispatchEvent(new CustomEvent('message', { detail: JSON.parse(message.data) }));
    })
    this._ws.addEventListener('close', ()=>{
      this.dispatchEvent(new Event('close'));
      this.scheduleConnect();
    });
    this._ws.addEventListener('error', ()=>{
      this.dispatchEvent(new Event('error'));
      this.scheduleConnect();
    })
  }
  scheduleConnect() {
    this._ws = null;
    if(this._retryTimeout) clearTimeout(this._retryTimeout);
    this._retryTimeout = setTimeout(()=>{
      this.connect();
    }, this._retryTime);
  }
  disconnect() {
    this.ws?.close();
  }
}

const host = `${window.location.host}`;
const statusWSURL = '/status';
const rawDataWSURL = '/raw';

const processStatusWSData = (tsState, type='raw') => {
  for(const [key, value] of Object.entries(tsState)) {
    const elId = key;
    let el = document.querySelector(`#${elId}`);
    if (el) {
      el.innerHTML = value;
    }
    el = document.querySelector(`[${key}]`);
    if(el) {
      el.setAttribute(key,value);
    }
    if (key == 'timestamp') {
      const tsDate = new Date(value * 1000);
      const tsHoursStr = new String(tsDate.getHours());
      const tsMinutesStr = new String(tsDate.getMinutes());
      const tsSecondsStr = new String(tsDate.getSeconds());
      el = document.querySelector(`#${type}-${elId}`);
      el.innerHTML = tsHoursStr.padStart(2, '0') + ':' + tsMinutesStr.padStart(2, '0') + ':' + tsSecondsStr.padStart(2, '0');
    }
  }
}

const connectWebSockets = () => {
  const statusWS = new ReconnectingWS( `ws://${host}${statusWSURL}`, 1000 );
  const rawDataWS = new ReconnectingWS( `ws://${host}${rawDataWSURL}`, 1000 );

  statusWS.addEventListener( 'message', ({detail}) => {
    const message = detail;
    processStatusWSData(message, 'status');
  });

  rawDataWS.addEventListener( 'message', ({detail}) => {
    const message = detail;
    processStatusWSData(message, 'raw');
  });
}

const formHandler = async (event) => {
  event.preventDefault();
  const _form = event.target;
  const _submitter = event.submitter;
  _submitter.ariaBusy = "true";
  const formData = new FormData(_form);
  const postResults = await fetch(_form.action, {
    method: _form.method,
    cache: 'no-cache',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify(Object.fromEntries(formData))
  });
  const postJSON = await postResults.json();
  _submitter.ariaBusy = "false";
  return postJSON;
}

const updateConfigForms = (deviceConfig) => {
  if(deviceConfig == null) return;

  const inputs = document.querySelectorAll('input');

  inputs.forEach((input) => {
    const path = input.id.split('.');
    let configItem = deviceConfig;
    let idx = 0;
    while(idx < path.length && configItem[path[idx]]) {
      configItem = configItem[path[idx]];
      idx++;
    }
    input.addEventListener('change', (event) => {
      event.target.labels.forEach((label)=>{
        const valueContainer = label.querySelector('span');
        if(valueContainer) valueContainer.innerText = event.target.value;
      })
    })
    input.value = configItem;
    input.dispatchEvent(new Event('change'));
  });

  const forms = document.querySelectorAll('form');

  forms.forEach((form) => {
    form.addEventListener('submit', formHandler);
  });
}

const loadDeviceConfig = async () => {
  let config = await fetch(`http://${host}/rpc/config.get`)
  return await config.json();
}

const loadCalibrationStatus = async () => {
  const resp = await fetch(`http://${host}/rpc/Calibration.Status`);
  return await resp.json();
}

const formatTimestamp = (ts) => {
  if (!ts) return '—';
  const d = new Date(ts * 1000);
  const now = new Date();
  return d.toDateString() === now.toDateString()
    ? d.toLocaleTimeString()
    : d.toLocaleString();
}

const updateCalibrationUI = (status) => {
  document.getElementById('calib-current-slope').innerText = status.current_slope.toFixed(4);
  document.getElementById('calib-samples').innerText = status.n_samples;
  document.getElementById('calib-ready').innerText = status.ready ? 'Yes' : 'No';
  document.getElementById('calib-last-updated').innerText = formatTimestamp(status.last_updated);

  if (status.n_samples > 0) {
    document.getElementById('calib-r2').innerText = status.r2.toFixed(4);
    document.getElementById('calib-temp-range').innerText =
      `${status.temp_min.toFixed(1)} – ${status.temp_max.toFixed(1)} °C`;
  }

  if (status.ready) {
    document.getElementById('calib-calc-slope').innerText =
      status.calculated_slope.toFixed(4);
    document.getElementById('calib-slope-input').value =
      status.calculated_slope.toFixed(4);
  } else {
    document.getElementById('calib-calc-slope').innerText =
      status.n_samples > 0
        ? `collecting… (${status.temp_min.toFixed(1)}–${status.temp_max.toFixed(1)} °C, ${status.n_samples} pts)`
        : 'waiting for warming phase';
    document.getElementById('calib-slope-input').value =
      status.current_slope.toFixed(4);
  }
}

const calibrationFormHandler = async (event) => {
  event.preventDefault();
  const submitter = event.submitter;
  submitter.ariaBusy = 'true';
  const slope = parseFloat(document.getElementById('calib-slope-input').value);
  const resp = await fetch(`http://${host}/rpc/Calibration.SetSlope`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ slope })
  });
  const result = await resp.json();
  submitter.ariaBusy = 'false';
  if (result.status) {
    const status = await loadCalibrationStatus();
    updateCalibrationUI(status);
  }
}

window.addEventListener('load', async (event) => {
  connectWebSockets();
  const deviceConfig = await loadDeviceConfig();
  updateConfigForms(deviceConfig);

  const calibStatus = await loadCalibrationStatus();
  updateCalibrationUI(calibStatus);
  document.getElementById('calibration-form').addEventListener('submit', calibrationFormHandler);

  setInterval(async () => {
    const s = await loadCalibrationStatus();
    updateCalibrationUI(s);
  }, 30000);
})