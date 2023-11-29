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

window.addEventListener('load', async (event) => {
  connectWebSockets();
  const deviceConfig = await loadDeviceConfig();
  updateConfigForms(deviceConfig);
})