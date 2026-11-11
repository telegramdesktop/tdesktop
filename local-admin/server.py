#!/usr/bin/env python3
"""Local-only account and virtual-assets service for the project.
Binds to 127.0.0.1; it is not connected to Telegram's production network.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse
import hashlib, hmac, json, os, secrets, sqlite3, time, re

ROOT = Path(__file__).resolve().parent
DB = ROOT / "data.sqlite3"
HOST, PORT = "127.0.0.1", int(os.getenv("ADMIN_PORT", "8765"))
ADMIN_PASSWORD = os.getenv("ADMIN_PASSWORD", "")
if not ADMIN_PASSWORD:
    ADMIN_PASSWORD = secrets.token_urlsafe(18)
    print("\n[LOCAL ADMIN] Temporary admin password (save it):", ADMIN_PASSWORD, "\n")
ADMIN_HASH = hashlib.sha256(ADMIN_PASSWORD.encode()).hexdigest()
SESSIONS = {}
USER_SESSIONS = {}
LOGIN_ATTEMPTS = {}

def db():
    con = sqlite3.connect(DB)
    con.row_factory = sqlite3.Row
    con.execute("PRAGMA foreign_keys=ON")
    return con

def init_db():
    with db() as c:
        c.executescript("""
        CREATE TABLE IF NOT EXISTS users(
          id INTEGER PRIMARY KEY AUTOINCREMENT, phone TEXT NOT NULL UNIQUE,
          otp_hash TEXT NOT NULL, otp_expires INTEGER NOT NULL,
          username TEXT UNIQUE, virtual_number TEXT UNIQUE,
          stars INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL,
          active INTEGER NOT NULL DEFAULT 1
        );
        CREATE TABLE IF NOT EXISTS ledger(
          id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL,
          kind TEXT NOT NULL, amount INTEGER NOT NULL, note TEXT NOT NULL,
          created_at INTEGER NOT NULL, FOREIGN KEY(user_id) REFERENCES users(id)
        );
        CREATE TABLE IF NOT EXISTS purchases(
          id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL,
          item TEXT NOT NULL, cost INTEGER NOT NULL, created_at INTEGER NOT NULL,
          FOREIGN KEY(user_id) REFERENCES users(id)
        );
        """)

def safe_user(row):
    d = dict(row)
    d.pop("otp_hash", None)
    d.pop("otp_expires", None)
    return d

class Handler(BaseHTTPRequestHandler):
    server_version = "LocalAssets/1.0"
    def log_message(self, fmt, *args):
        print("[HTTP]", self.address_string(), fmt % args)

    def send_json(self, status, data):
        raw = json.dumps(data, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(raw)

    def body(self):
        n = min(int(self.headers.get("Content-Length", "0")), 100_000)
        return json.loads(self.rfile.read(n) or b"{}")

    def admin(self):
        token = self.headers.get("Authorization", "").removeprefix("Bearer ").strip()
        return token in SESSIONS and SESSIONS[token] > time.time()

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/account":
            data = (ROOT / "account.html").read_bytes()
            self.send_response(200); self.send_header("Content-Type","text/html; charset=utf-8")
            self.send_header("Content-Length",str(len(data))); self.end_headers(); self.wfile.write(data); return
        if path in ("/", "/index.html"):
            data = (ROOT / "index.html").read_bytes()
            self.send_response(200); self.send_header("Content-Type","text/html; charset=utf-8")
            self.send_header("Content-Length",str(len(data))); self.end_headers(); self.wfile.write(data); return
        if path == "/api/health":
            return self.send_json(200, {"ok": True, "mode": "local-only"})
        if path == "/api/user/me":
            token = self.headers.get("Authorization","").removeprefix("Bearer ").strip()
            if token not in USER_SESSIONS or USER_SESSIONS[token][1] <= time.time():
                return self.send_json(401, {"error":"Войдите как пользователь"})
            with db() as c:
                u = c.execute("SELECT * FROM users WHERE id=?",(USER_SESSIONS[token][0],)).fetchone()
            return self.send_json(200, {"user":safe_user(u)})
        if path == "/api/users":
            if not self.admin():
                return self.send_json(401, {"error":"Требуется вход администратора"})
            with db() as c:
                rows = c.execute("SELECT * FROM users ORDER BY id DESC").fetchall()
                users = [safe_user(x) for x in rows]
                for u in users:
                    u["last_transactions"] = [dict(x) for x in c.execute(
                      "SELECT kind,amount,note,created_at FROM ledger WHERE user_id=? ORDER BY id DESC LIMIT 5",(u["id"],))]
            return self.send_json(200, {"users":users})
        if not self.admin():
            return self.send_json(401, {"error":"Требуется вход администратора"})
        if path.startswith("/api/users/") and path.endswith("/history"):
            try: uid = int(path.split("/")[3])
            except: return self.send_json(400, {"error":"Некорректный ID"})
            with db() as c:
                rows = c.execute("SELECT * FROM ledger WHERE user_id=? ORDER BY id DESC LIMIT 100",(uid,)).fetchall()
            return self.send_json(200, {"history":[dict(x) for x in rows]})
        return self.send_json(404, {"error":"Не найдено"})

    def do_POST(self):
        path = urlparse(self.path).path
        try: data = self.body()
        except Exception: return self.send_json(400, {"error":"Некорректный JSON"})
        if path == "/api/login":
            now = time.time()
            ip = self.client_address[0]
            attempts, until = LOGIN_ATTEMPTS.get(ip, (0, 0))
            if until > now: return self.send_json(429, {"error":"Слишком много попыток. Подождите минуту."})
            supplied = hashlib.sha256(str(data.get("password","")).encode()).hexdigest()
            if not hmac.compare_digest(supplied, ADMIN_HASH):
                attempts += 1
                LOGIN_ATTEMPTS[ip] = (attempts, now + 60 if attempts >= 6 else 0)
                return self.send_json(401, {"error":"Неверный пароль"})
            LOGIN_ATTEMPTS.pop(ip, None)
            token = secrets.token_urlsafe(32); SESSIONS[token] = now + 8*3600
            return self.send_json(200, {"token":token,"expires_in":28800})
        now = int(time.time())
        if path == "/api/user/verify":
            phone = str(data.get("phone","")).strip()
            code = str(data.get("code","")).strip()
            with db() as c:
                u = c.execute("SELECT * FROM users WHERE phone=?",(phone,)).fetchone()
                if not u or not u["active"] or u["otp_expires"] < now or not hmac.compare_digest(u["otp_hash"],hashlib.sha256(code.encode()).hexdigest()):
                    return self.send_json(401, {"error":"Неверный или просроченный код"})
                token = secrets.token_urlsafe(32); USER_SESSIONS[token] = (u["id"], now+86400)
                c.execute("UPDATE users SET otp_hash=?,otp_expires=? WHERE id=?",
                          (hashlib.sha256(secrets.token_bytes(32)).hexdigest(),0,u["id"]))
                return self.send_json(200, {"token":token,"user":safe_user(u)})
        if path == "/api/store/buy":
            token = self.headers.get("Authorization","").removeprefix("Bearer ").strip()
            if token not in USER_SESSIONS or USER_SESSIONS[token][1] <= time.time():
                return self.send_json(401, {"error":"Войдите как пользователь"})
            item = str(data.get("item",""))
            catalog = {"theme_dark":("Тёмная тема",25),"username_style":("Оформление профиля",50),"profile_badge":("Значок профиля",100)}
            if item not in catalog: return self.send_json(400, {"error":"Товар не найден"})
            title,cost = catalog[item]
            uid = USER_SESSIONS[token][0]
            with db() as c:
                u = c.execute("SELECT * FROM users WHERE id=?",(uid,)).fetchone()
                if not u or u["stars"] < cost: return self.send_json(400, {"error":"Недостаточно Stars"})
                c.execute("UPDATE users SET stars=stars-? WHERE id=?",(cost,uid))
                c.execute("INSERT INTO ledger(user_id,kind,amount,note,created_at) VALUES(?,?,?,?,?)",(uid,"purchase",-cost,title,now))
                c.execute("INSERT INTO purchases(user_id,item,cost,created_at) VALUES(?,?,?,?)",(uid,item,cost,now))
            return self.send_json(200, {"success":True,"item":title,"spent":cost})
        if not self.admin():
            return self.send_json(401, {"error":"Требуется вход администратора"})
        now = int(time.time())
        if path == "/api/users":
            phone = str(data.get("phone","")).strip()
            if not re.fullmatch(r"\+?[0-9]{3,20}", phone):
                return self.send_json(400, {"error":"Номер: от 3 до 20 цифр, можно с +"})
            phone = phone if phone.startswith("+") else "+" + phone
            otp = f"{secrets.randbelow(1000000):06d}"
            with db() as c:
                try:
                    cur = c.execute("INSERT INTO users(phone,otp_hash,otp_expires,created_at) VALUES(?,?,?,?)",
                       (phone,hashlib.sha256(otp.encode()).hexdigest(),now+600,now))
                    uid = cur.lastrowid
                except sqlite3.IntegrityError:
                    return self.send_json(409, {"error":"Пользователь с таким номером уже существует"})
                c.execute("INSERT INTO ledger(user_id,kind,amount,note,created_at) VALUES(?,?,?,?,?)",
                          (uid,"account",0,"Создание аккаунта",now))
            return self.send_json(201, {"user":{"id":uid,"phone":phone,"stars":0},"verification_code":otp,"code_expires_in":600})
        m = re.fullmatch(r"/api/users/(\d+)/(stars|username|number|otp|active)", path)
        if m:
            uid, action = int(m.group(1)), m.group(2)
            with db() as c:
                user = c.execute("SELECT * FROM users WHERE id=?",(uid,)).fetchone()
                if not user: return self.send_json(404, {"error":"Пользователь не найден"})
                if action == "stars":
                    amount = data.get("amount")
                    if not isinstance(amount,int) or isinstance(amount,bool) or amount == 0 or abs(amount)>1000000000:
                        return self.send_json(400, {"error":"Укажите целое ненулевое количество"})
                    if user["stars"] + amount < 0: return self.send_json(400, {"error":"Недостаточно Stars"})
                    c.execute("UPDATE users SET stars=stars+? WHERE id=?",(amount,uid))
                    c.execute("INSERT INTO ledger(user_id,kind,amount,note,created_at) VALUES(?,?,?,?,?)",
                              (uid,"credit" if amount>0 else "debit",amount,str(data.get("note") or "Операция администратора")[:160],now))
                    return self.send_json(200, {"stars":user["stars"]+amount})
                if action == "username":
                    name = str(data.get("username","")).strip().lstrip("@")
                    if name and not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{4,31}",name):
                        return self.send_json(400, {"error":"Username: 5–32 символа, латиница, цифры и _"})
                    try: c.execute("UPDATE users SET username=? WHERE id=?",(name or None,uid))
                    except sqlite3.IntegrityError: return self.send_json(409, {"error":"Username уже занят"})
                    return self.send_json(200, {"username":name or None})
                if action == "number":
                    number = str(data.get("number","")).strip()
                    if data.get("generate"): number = "+888" + "".join(str(secrets.randbelow(10)) for _ in range(8))
                    if number and not re.fullmatch(r"\+888[0-9]{8,12}",number):
                        return self.send_json(400, {"error":"Внутренний номер должен иметь формат +888 и 8–12 цифр"})
                    try: c.execute("UPDATE users SET virtual_number=? WHERE id=?",(number or None,uid))
                    except sqlite3.IntegrityError: return self.send_json(409, {"error":"Этот номер уже занят"})
                    return self.send_json(200, {"virtual_number":number})
                if action == "otp":
                    otp = f"{secrets.randbelow(1000000):06d}"
                    c.execute("UPDATE users SET otp_hash=?,otp_expires=? WHERE id=?",(hashlib.sha256(otp.encode()).hexdigest(),now+600,uid))
                    return self.send_json(200, {"verification_code":otp,"code_expires_in":600})
                if action == "active":
                    active = 1 if data.get("active") else 0
                    c.execute("UPDATE users SET active=? WHERE id=?",(active,uid))
                    return self.send_json(200, {"active":bool(active)})
        return self.send_json(404, {"error":"Не найдено"})

    def do_DELETE(self):
        if not self.admin(): return self.send_json(401, {"error":"Требуется вход администратора"})
        path = urlparse(self.path).path
        m = re.fullmatch(r"/api/users/(\d+)",path)
        if not m: return self.send_json(404, {"error":"Не найдено"})
        with db() as c:
            c.execute("DELETE FROM purchases WHERE user_id=?",(int(m.group(1)),))
            c.execute("DELETE FROM ledger WHERE user_id=?",(int(m.group(1)),))
            cur = c.execute("DELETE FROM users WHERE id=?",(int(m.group(1)),))
        return self.send_json(200 if cur.rowcount else 404, {"deleted":bool(cur.rowcount)})

if __name__ == "__main__":
    init_db()
    print(f"Local admin panel: http://{HOST}:{PORT}")
    print("Local-only service. Do not expose this port to the internet.")
    ThreadingHTTPServer((HOST,PORT),Handler).serve_forever()
