// prepare temperatureChart
// Set new default font family and font color to mimic Bootstrap's default styling
Chart.defaults.global.defaultFontFamily = '-apple-system,system-ui,BlinkMacSystemFont,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif';
Chart.defaults.global.defaultFontColor = '#292b2c';

// Area Chart Example
var ctx = document.getElementById("temperatureChart");
var temperatureChart = new Chart(ctx, {
  type: 'line',
  data: {
    labels: [],
    datasets: [{
      label: "Temperature (°C)",
      backgroundColor: "rgba(2,117,216,0.2)",
      borderColor: "rgba(2,117,216,1)",
      pointRadius: 0,
      data: [],
    }],
  },
  options: {
    animation: false,
    parsing: false, // data is numeric already
    normalized: true, // data is sorted by x-axis already
    scales: {
      xAxes: [{
        ticks: {
          sampleSize: 10,
          // maxTicksLimit: 11
        }
      }],
      yAxes: [{
        ticks: {
          // maxTicksLimit: 7
        },
        gridLines: {
          color: "rgba(0, 0, 0, .125)",
        }
      }],
    },
    legend: {
      // display: false
    }
  }
});

// get and insert status variables
function getStatus() {
  fetch('/api/status')
    .then(response => response.json())
    .then(data => {
      var heaterPercent = (Number (data.output) / 10).toFixed(1);
      var sensorTemp = (Number (data.input)).toFixed(1);
      document.getElementById("heaterPercent").textContent=heaterPercent;
      document.getElementById("sensorTemp").textContent=sensorTemp;
    }
    );
}

function dateToTimeString(d) {
  let t = [ d.getHours(), d.getMinutes(), d.getSeconds() ]
    .map(x => String(x).padStart(2,'0'));
  return `${t[0]}:${t[1]}:${t[2]}`;
}

// get log data and plot
function getLog() {
  fetch('/api/log')
    .then(response => response.text())
    .then(response => {
      // "parse" csv
      let data =
        response.split('\n')
        .map(line => {
          let row = line.split(',');
          return { x: parseInt(row[0]), y: parseFloat(row[1]) }
        });
      data.shift(); // ignore first line
      // reconstruct time
      let maxms = data[data.length - 1].x;
      let labels = data.map(e => {
        let ms = maxms - e.x + 1000;
        let h = Math.floor(ms / 1000 / 60);
        let s = Math.floor((ms - h * 60 * 1000) / 1000);
        let t = [h, s].map(x => String(x).padStart(2,'0'));
        return `-${t[0]}:${t[1]}`;
      });
      // update plot
      temperatureChart.data.labels = labels;
      temperatureChart.data.datasets[0].data = data;
      temperatureChart.update();
    })
}

getStatus();
getLog();

setInterval(getStatus, 5000);
setInterval(getLog, 5000);

function setConfig(obj) {
  function setValue(id) {
    document.getElementById(id).setAttribute('value', obj[id]);
  }
  setValue('setpoint');
  setValue('wlan_ap_ssid');
  setValue('wlan_ap_password');
  setValue('wlan_join_hostname');
  setValue('wlan_join_ssid');
  setValue('wlan_join_password');
}

fetch('/api/config').then(r => r.json()).then(setConfig);

jQuery('#configForm').on('submit', function(event) {
  event.preventDefault();
  const obj = Object.fromEntries(new FormData(this));
  // TODO filter unmodified fields
  fetch("/api/config", {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify(obj)
  });
});
