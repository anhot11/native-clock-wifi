# EIFISIER - Network Device Discovery & Inspector (C)

Herramienta profesional desarrollada en **C puro** para descubrimiento, inventario y análisis de dispositivos en redes locales / Wi-Fi.

## 🚀 Características
- **Detección automática de interfaz y subred**: Identifica interfaces activas (`wlan0`, `eth0`, etc.) y calcula el rango CIDR de la subred automáticamente.
- **Escaneo ultrarrápido sin root**: Utiliza datagramas ICMP no privilegiados (`SOCK_DGRAM`, `IPPROTO_ICMP`) soportados en Linux y Android/Termux sin requerir permisos de superusuario.
- **Medición de latencia precisa**: Calcula el RTT (*Round Trip Time*) exacto en milisegundos para cada dispositivo detectado.
- **Sondeo de servicios y puertos (`-p`)**: Inspecciona puertos comunes de red (SSH, DNS, HTTP, HTTPS, RTSP, mDNS, Web-Proxy) en modo no bloqueante con timeout controlado.
- **Salida profesional y exportación JSON (`-j`)**: Tabla formateada en terminal con colores ANSI o salida JSON estructurada para integración con scripts.

## 🛠️ Compilación
Para compilar:
```bash
cd eifisier
make
```

Para limpiar archivos compilados:
```bash
make clean
```

## 📖 Uso y Ejemplos

### 1. Escaneo rápido de la red Wi-Fi actual
```bash
./eifisier
```

### 2. Escaneo con detección de puertos y servicios
```bash
./eifisier -p
```

### 3. Escaneo de una subred o rango específico
```bash
./eifisier -r 192.168.7.0/24 -p
./eifisier -r 192.168.7.1-192.168.7.50
```

### 4. Salida en formato JSON
```bash
./eifisier -j > red_dispositivos.json
```
