import json, os, subprocess, sys, time, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PORT = "18765"
env = os.environ.copy()
env.update({"ADMIN_PASSWORD":"ci-test-password", "ADMIN_PORT":PORT})
proc = subprocess.Popen([sys.executable, "server.py"], cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
base = "http://127.0.0.1:" + PORT

def request(path, method="GET", body=None, token=None):
    data = None if body is None else json.dumps(body).encode()
    headers = {"Content-Type":"application/json"}
    if token: headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(base+path, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=3) as response:
        return json.loads(response.read())

try:
    for _ in range(50):
        try:
            if request("/api/health")["ok"]: break
        except Exception: time.sleep(.2)
    else: raise RuntimeError("server did not start")
    admin = request("/api/login","POST",{"password":"ci-test-password"})["token"]
    created = request("/api/users","POST",{"phone":"+1234567890"},admin)
    uid, code = created["user"]["id"], created["verification_code"]
    user = request("/api/user/verify","POST",{"phone":"+1234567890","code":code})["token"]
    request("/api/users/%d/stars"%uid,"POST",{"amount":150,"note":"CI seed"},admin)
    request("/api/users/%d/username"%uid,"POST",{"username":"local_test_user"},admin)
    number = request("/api/users/%d/number"%uid,"POST",{"generate":True},admin)["virtual_number"]
    if not number.startswith("+888"): raise AssertionError(number)
    request("/api/store/buy","POST",{"item":"theme_dark"},user)
    profile = request("/api/user/me",token=user)["user"]
    if profile["stars"] != 125: raise AssertionError(profile["stars"])
    if profile["username"] != "local_test_user": raise AssertionError(profile["username"])
    print("Local admin smoke test passed")
finally:
    proc.terminate()
    try: proc.wait(timeout=5)
    except subprocess.TimeoutExpired: proc.kill()
