#!/usr/bin/env python3
import http.server
import json
import os
import socketserver
import sys

PORT = 8080
NOTE_FILE = os.path.abspath(os.path.join(os.path.dirname(__file__), "pront.txt"))
MAX_LENGTH = 800
PASSWORD = "412412"

HTML_CONTENT = f"""<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Nota Rápida</title>
  <style>
    * {{
      box-sizing: border-box;
      margin: 0;
      padding: 0;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    }}
    body {{
      background-color: #0f172a;
      color: #f8fafc;
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 16px;
    }}
    .card {{
      background-color: #1e293b;
      border: 1px solid #334155;
      border-radius: 16px;
      padding: 24px;
      width: 100%;
      max-width: 500px;
      box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.4);
    }}
    .header-bar {{
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 16px;
    }}
    h1 {{
      font-size: 1.35rem;
      font-weight: 700;
      color: #38bdf8;
      display: flex;
      align-items: center;
      gap: 8px;
    }}
    .btn-logout {{
      background: transparent;
      border: 1px solid #475569;
      color: #94a3b8;
      font-size: 0.75rem;
      padding: 5px 10px;
      border-radius: 6px;
      cursor: pointer;
      display: flex;
      align-items: center;
      gap: 4px;
    }}
    .btn-logout:hover {{
      color: #f8fafc;
      border-color: #64748b;
    }}
    .path-section {{
      background-color: #0f172a;
      border: 1px solid #334155;
      border-radius: 10px;
      padding: 12px;
      margin-bottom: 16px;
    }}
    .path-label {{
      font-size: 0.75rem;
      color: #94a3b8;
      text-transform: uppercase;
      font-weight: 600;
      margin-bottom: 4px;
    }}
    .path-box {{
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 8px;
    }}
    .path-text {{
      font-family: monospace;
      font-size: 0.8rem;
      color: #e2e8f0;
      word-break: break-all;
      background: #1e293b;
      padding: 6px 8px;
      border-radius: 6px;
      flex: 1;
      border: 1px solid #475569;
    }}
    .btn-copy {{
      background-color: #334155;
      color: #f8fafc;
      border: 1px solid #475569;
      border-radius: 6px;
      padding: 6px 12px;
      font-size: 0.8rem;
      font-weight: 600;
      cursor: pointer;
      white-space: nowrap;
      transition: all 0.2s;
    }}
    .btn-copy:active {{
      background-color: #0284c7;
    }}
    .textarea-container {{
      position: relative;
      margin-bottom: 8px;
    }}
    textarea {{
      width: 100%;
      height: 180px;
      background-color: #0f172a;
      color: #f8fafc;
      border: 1.5px solid #334155;
      border-radius: 12px;
      padding: 12px;
      font-size: 1rem;
      resize: vertical;
      outline: none;
      transition: border-color 0.2s;
      line-height: 1.45;
    }}
    textarea:focus {{
      border-color: #38bdf8;
    }}
    .counter {{
      text-align: right;
      font-size: 0.8rem;
      color: #94a3b8;
      margin-bottom: 16px;
    }}
    .counter.near-limit {{
      color: #f59e0b;
    }}
    .counter.limit {{
      color: #ef4444;
      font-weight: bold;
    }}
    .btn-group {{
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
    }}
    button.action-btn {{
      padding: 12px;
      border-radius: 10px;
      font-size: 0.95rem;
      font-weight: 600;
      border: none;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 6px;
      transition: transform 0.1s, opacity 0.2s;
    }}
    button.action-btn:active {{
      transform: scale(0.97);
    }}
    .btn-save {{
      background-color: #0284c7;
      color: white;
    }}
    .btn-save:hover {{
      background-color: #0369a1;
    }}
    .btn-clear {{
      background-color: #ef4444;
      color: white;
    }}
    .btn-clear:hover {{
      background-color: #dc2626;
    }}
    .toast {{
      position: fixed;
      bottom: 24px;
      background-color: #10b981;
      color: white;
      padding: 10px 20px;
      border-radius: 30px;
      font-size: 0.85rem;
      font-weight: 600;
      box-shadow: 0 4px 12px rgba(0,0,0,0.3);
      opacity: 0;
      transform: translateY(20px);
      transition: all 0.3s ease;
      pointer-events: none;
      z-index: 999;
    }}
    .toast.show {{
      opacity: 1;
      transform: translateY(0);
    }}

    /* Lock Screen Form */
    #lockScreen {{
      display: flex;
      flex-direction: column;
      align-items: center;
      text-align: center;
      gap: 16px;
    }}
    .lock-icon {{
      font-size: 3rem;
      margin-bottom: 4px;
    }}
    .lock-title {{
      font-size: 1.3rem;
      font-weight: 700;
      color: #38bdf8;
    }}
    .lock-desc {{
      font-size: 0.85rem;
      color: #94a3b8;
      max-width: 320px;
    }}
    .pass-input {{
      width: 100%;
      max-width: 280px;
      background-color: #0f172a;
      border: 1.5px solid #334155;
      color: #f8fafc;
      font-size: 1.2rem;
      text-align: center;
      letter-spacing: 4px;
      padding: 12px;
      border-radius: 10px;
      outline: none;
      transition: border-color 0.2s;
    }}
    .pass-input:focus {{
      border-color: #38bdf8;
    }}
    .btn-login {{
      width: 100%;
      max-width: 280px;
      background-color: #0284c7;
      color: white;
      border: none;
      padding: 12px;
      border-radius: 10px;
      font-size: 1rem;
      font-weight: 600;
      cursor: pointer;
    }}
    .hidden {{
      display: none !important;
    }}
  </style>
</head>
<body>

  <!-- Pantalla de Login / Bloqueo -->
  <div class="card" id="lockCard">
    <div id="lockScreen">
      <div class="lock-icon">🔒</div>
      <div class="lock-title">Acceso Protegido</div>
      <div class="lock-desc">Introduce la contraseña para acceder a la nota</div>
      <input type="password" id="passInput" class="pass-input" placeholder="••••••" inputmode="numeric" autocomplete="current-password" autofocus />
      <button class="btn-login" onclick="attemptLogin()">Desbloquear</button>
    </div>
  </div>

  <!-- Pantalla Principal de la Nota -->
  <div class="card hidden" id="mainCard">
    <div class="header-bar">
      <h1>📝 Nota Rápida</h1>
      <button class="btn-logout" onclick="logout()">🔒 Salir</button>
    </div>
    
    <div class="path-section">
      <div class="path-label">Ruta del archivo</div>
      <div class="path-box">
        <span class="path-text" id="filePath">{NOTE_FILE}</span>
        <button class="btn-copy" id="btnCopy" onclick="copyPath()">📋 Copiar</button>
      </div>
    </div>

    <div class="textarea-container">
      <textarea id="noteInput" maxlength="{MAX_LENGTH}" placeholder="Escribe aquí tu nota (máx. {MAX_LENGTH} caracteres)..."></textarea>
    </div>
    <div class="counter" id="charCounter">0 / {MAX_LENGTH}</div>

    <div class="btn-group">
      <button class="action-btn btn-save" onclick="saveNote()">💾 Guardar</button>
      <button class="action-btn btn-clear" onclick="clearNote()">🗑️ Borrar</button>
    </div>
  </div>

  <div class="toast" id="toast"></div>

  <script>
    const lockCard = document.getElementById('lockCard');
    const mainCard = document.getElementById('mainCard');
    const passInput = document.getElementById('passInput');
    const noteInput = document.getElementById('noteInput');
    const charCounter = document.getElementById('charCounter');
    const toast = document.getElementById('toast');
    const maxChars = {MAX_LENGTH};
    const targetPath = "{NOTE_FILE}";

    function getAuth() {{
      return localStorage.getItem('note_auth') || '';
    }}

    function setAuth(token) {{
      localStorage.setItem('note_auth', token);
    }}

    function showToast(msg, bg = '#10b981') {{
      toast.textContent = msg;
      toast.style.backgroundColor = bg;
      toast.classList.add('show');
      setTimeout(() => toast.classList.remove('show'), 2500);
    }}

    function updateCounter() {{
      const len = noteInput.value.length;
      charCounter.textContent = `${{len}} / ${{maxChars}}`;
      charCounter.className = 'counter';
      if (len >= maxChars) {{
        charCounter.classList.add('limit');
      }} else if (len >= maxChars * 0.85) {{
        charCounter.classList.add('near-limit');
      }}
    }}

    noteInput.addEventListener('input', updateCounter);
    passInput.addEventListener('keydown', (e) => {{
      if (e.key === 'Enter') attemptLogin();
    }});

    async function attemptLogin() {{
      const pass = passInput.value.trim();
      if (!pass) return;

      try {{
        const res = await fetch('/api/auth', {{
          method: 'POST',
          headers: {{ 'Content-Type': 'application/json' }},
          body: JSON.stringify({{ password: pass }})
        }});

        if (res.ok) {{
          setAuth(pass);
          passInput.value = '';
          unlockUI();
          loadNote();
        }} else {{
          showToast('❌ Contraseña incorrecta', '#ef4444');
          passInput.value = '';
          passInput.focus();
        }}
      }} catch (err) {{
        showToast('❌ Error de conexión', '#ef4444');
      }}
    }}

    function unlockUI() {{
      lockCard.classList.add('hidden');
      mainCard.classList.remove('hidden');
    }}

    function lockUI() {{
      mainCard.classList.add('hidden');
      lockCard.classList.remove('hidden');
      passInput.focus();
    }}

    function logout() {{
      localStorage.removeItem('note_auth');
      lockUI();
      showToast('🔒 Sesión cerrada', '#64748b');
    }}

    async function loadNote() {{
      const token = getAuth();
      if (!token) {{
        lockUI();
        return;
      }}

      try {{
        const res = await fetch('/api/note', {{
          headers: {{ 'X-Auth': token }}
        }});

        if (res.status === 401) {{
          lockUI();
          return;
        }}

        if (res.ok) {{
          const data = await res.json();
          noteInput.value = data.text || '';
          updateCounter();
          unlockUI();
        }}
      }} catch (err) {{
        console.error('Error al cargar nota:', err);
      }}
    }}

    async function saveNote() {{
      const token = getAuth();
      const text = noteInput.value.slice(0, maxChars);
      try {{
        const res = await fetch('/api/note', {{
          method: 'POST',
          headers: {{ 
            'Content-Type': 'application/json',
            'X-Auth': token
          }},
          body: JSON.stringify({{ text }})
        }});

        if (res.status === 401) {{
          showToast('🔒 Sesión expirada o contraseña inválida', '#ef4444');
          lockUI();
          return;
        }}

        if (res.ok) {{
          showToast('✅ Guardado en pront.txt (' + text.length + ' chars)', '#10b981');
        }} else {{
          showToast('❌ Error al guardar', '#ef4444');
        }}
      }} catch (err) {{
        showToast('❌ Error de conexión', '#ef4444');
      }}
    }}

    async function clearNote() {{
      const token = getAuth();
      try {{
        const res = await fetch('/api/note', {{
          method: 'DELETE',
          headers: {{ 'X-Auth': token }}
        }});

        if (res.status === 401) {{
          lockUI();
          return;
        }}

        if (res.ok) {{
          noteInput.value = '';
          updateCounter();
          showToast('🗑️ Nota borrada', '#f59e0b');
        }} else {{
          showToast('❌ Error al borrar', '#ef4444');
        }}
      }} catch (err) {{
        showToast('❌ Error al borrar', '#ef4444');
      }}
    }}

    function copyPath() {{
      if (navigator.clipboard && window.isSecureContext) {{
        navigator.clipboard.writeText(targetPath).then(() => {{
          showToast('📋 Ruta copiada al portapapeles', '#0284c7');
        }}).catch(() => {{
          fallbackCopy(targetPath);
        }});
      }} else {{
        fallbackCopy(targetPath);
      }}
    }}

    function fallbackCopy(text) {{
      const ta = document.createElement('textarea');
      ta.value = text;
      ta.style.position = 'fixed';
      ta.style.left = '-9999px';
      document.body.appendChild(ta);
      ta.focus();
      ta.select();
      try {{
        document.execCommand('copy');
        showToast('📋 Ruta copiada al portapapeles', '#0284c7');
      }} catch (err) {{
        showToast('❌ No se pudo copiar automáticamente', '#ef4444');
      }}
      document.body.removeChild(ta);
    }}

    // Auto-login if token exists in localStorage
    if (getAuth()) {{
      loadNote();
    }}
  </script>
</body>
</html>
"""

class NoteHandler(http.server.BaseHTTPRequestHandler):
    def do_HEAD(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()

    def check_auth(self):
        auth = self.headers.get("X-Auth", "")
        return auth == PASSWORD

    def send_unauthorized(self):
        self.send_response(401)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.end_headers()
        self.wfile.write(json.dumps({"error": "Unauthorized"}).encode("utf-8"))

    def do_GET(self):
        if self.path == "/" or self.path == "/index.html":
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(HTML_CONTENT.encode("utf-8"))
        elif self.path == "/api/note":
            if not self.check_auth():
                self.send_unauthorized()
                return

            content = ""
            if os.path.exists(NOTE_FILE):
                try:
                    with open(NOTE_FILE, "r", encoding="utf-8") as f:
                        content = f.read()
                except Exception:
                    content = ""
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.end_headers()
            response = json.dumps({"text": content, "path": NOTE_FILE, "max_length": MAX_LENGTH})
            self.wfile.write(response.encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        if self.path == "/api/auth":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length).decode("utf-8")
            try:
                data = json.loads(body)
                if data.get("password") == PASSWORD:
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json; charset=utf-8")
                    self.end_headers()
                    self.wfile.write(json.dumps({"success": True}).encode("utf-8"))
                else:
                    self.send_unauthorized()
            except Exception:
                self.send_unauthorized()

        elif self.path == "/api/note":
            if not self.check_auth():
                self.send_unauthorized()
                return

            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length).decode("utf-8")
            try:
                data = json.loads(body)
                text = data.get("text", "")[:MAX_LENGTH]
                with open(NOTE_FILE, "w", encoding="utf-8") as f:
                    f.write(text)
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.end_headers()
                self.wfile.write(json.dumps({"success": True, "path": NOTE_FILE}).encode("utf-8"))
            except Exception as e:
                self.send_response(500)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.end_headers()
                self.wfile.write(json.dumps({"error": str(e)}).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def do_DELETE(self):
        if self.path == "/api/note":
            if not self.check_auth():
                self.send_unauthorized()
                return

            try:
                with open(NOTE_FILE, "w", encoding="utf-8") as f:
                    f.write("")
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.end_headers()
                self.wfile.write(json.dumps({"success": True}).encode("utf-8"))
            except Exception as e:
                self.send_response(500)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.end_headers()
                self.wfile.write(json.dumps({"error": str(e)}).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        sys.stderr.write(f"[{self.log_date_time_string()}] {format % args}\n")

class ReusableServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True

if __name__ == "__main__":
    server_address = ("0.0.0.0", PORT)
    with ReusableServer(server_address, NoteHandler) as httpd:
        print(f"Servidor web activo en http://0.0.0.0:{PORT}")
        print(f"Ruta de la nota: {NOTE_FILE}")
        httpd.serve_forever()
