from flask import Response, Flask, request, send_from_directory, redirect
import os
import time
import random

def millis():
    return int(time.time() * 1000)

class PIDummy:
    def append(self, time, temperature):
        self.log.append({'time': time, 'temperature': temperature})

    def __init__(self, setpoint):
        now = millis()

        self.setpoint = float(setpoint)
        self.wlan_ap_ssid = "barepid"
        self.wlan_ap_password = "barepid42"
        self.wlan_join_hostname = "barepid"
        self.wlan_join_ssid = ""
        self.wlan_join_password = ""

        self.last = now
        self.log = []
        self.append(now, setpoint)
        self.append(now - 1000, setpoint)
        self.append(now - 2000, setpoint)
        self.append(now - 3000, setpoint)

    def update(self):
        now = millis()
        while self.last < now:
            self.last = self.last + 1000
            e0 = random.uniform(-3, 3)
            e1 = self.log[-1]['temperature'] - self.setpoint
            e2 = self.log[-2]['temperature'] - self.setpoint
            e3 = self.log[-3]['temperature'] - self.setpoint
            e4 = self.log[-4]['temperature'] - self.setpoint
            t0 = self.setpoint + .8 * e1 + .5 * e2 - .4 * e3 - .1 * e4 + e0
            self.append(self.last,t0)
            if (len(self.log) > 300):
                self.log.pop(0)

root_dir = os.getcwd()
app = Flask(__name__)
pid = PIDummy(98)

@app.route('/')
def hello():
    return redirect("/index.html", code=302)

@app.route('/<path:path>')
def get_static(path):
    pid.update()
    return send_from_directory(os.path.join(root_dir, 'data'), path)

config_fields = ['setpoint',
        'wlan_ap_ssid','wlan_ap_password',
        'wlan_join_hostname','wlan_join_ssid','wlan_join_password']

@app.route('/api/config', methods=['POST'])
def post_config():
    obj = request.json
    for k in config_fields:
        if k in obj:
            try:
                new = type(pid.__dict__[k])(obj[k])
                if new != pid.__dict__[k]:
                    print(f"change {k} from {pid.__dict__[k]} to {new}")
                    pid.__dict__[k] = new
            except:
                pass
    return redirect('/api/config')

@app.route('/api/config', methods=['GET'])
def get_config():
    obj = {}
    for k in config_fields:
        obj[k] = pid.__dict__[k]
    return obj

@app.route('/api/log', methods=['GET'])
def get_log():
    pid.update()
    r = "time,temperature"
    for entry in pid.log:
        r += f"\n{entry['time']},{entry['temperature']}"
    resp = Response(r)
    resp.headers['Content-Type'] = 'text/csv'
    return resp

@app.route('/api/status', methods=['GET'])
def get_status():
    pid.update()
    r = {}
    r['output'] = "420"
    r['input'] = pid.log[-1]['temperature']
    return r

if __name__ == '__main__':
    app.run()
