#!/data/data/com.termux/files/usr/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Detect current IP on WiFi or active network
IP=$(ifconfig 2>/dev/null | awk '/wlan0/{flag=1} flag && /inet / {print $2; exit}' || echo "")
if [ -z "$IP" ]; then
    IP=$(python3 -c "import socket; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.connect(('8.8.8.8', 80)); print(s.getsockname()[0]); s.close()" 2>/dev/null || echo "127.0.0.1")
fi

echo "================================================="
echo "  Servidor de Nota Rápida (pront.txt)"
echo "================================================="
echo "[*] IP actual detectada: $IP"
echo "[*] URL para tu otro celular:"
echo "    👉 http://$IP:8080"
echo "================================================="

# Kill previous instances if any
fuser -k 8080/tcp 2>/dev/null || pkill -f "python3.*web_server.py" 2>/dev/null || true
sleep 0.5

exec python3 "$DIR/web_server.py"
