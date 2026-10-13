# Monitor Nativo v2.0 (Android 11 - 16) — 100% Pure C

[![Build Native Android C App](https://github.com/anhot11/native-clock-wifi/actions/workflows/build.yml/badge.svg)](https://github.com/anhot11/native-clock-wifi/actions/workflows/build.yml)
[![NDK NativeActivity](https://img.shields.io/badge/NDK-NativeActivity%20(Pure%20C)-0ea5e9.svg)](https://developer.android.com/ndk)
[![APK Size](https://img.shields.io/badge/APK%20Size-158%20KB-22c55e.svg)](https://github.com/anhot11/native-clock-wifi/releases)
[![Allocs/Frame](https://img.shields.io/badge/Memory%20Allocs-0%20%2F%20frame-22c55e.svg)](https://github.com/anhot11/native-clock-wifi)

Monitor de sistema de nivel profesional para Android, desarrollado **100% en C puro** utilizando el **NDK** y `NativeActivity`, sin una sola línea de Java ni Kotlin en el código de la aplicación, y sin librerías externas pesadas.

Diseñado para demostrar por qué **C es la herramienta definitiva**:
- **0 asignaciones de memoria por frame** (Zero Garbage Collection, cero pausas GC).
- **Arranque instantáneo en frío** (~35 ms).
- **Consumo mínimo de memoria** (< 12 MB de RAM RSS).
- **Binario ultra-optimizado** (`libnative-app.so`: 95 KB, APK completo: 158 KB).

---

## 📱 Navegación de 4 Páginas con Swipe Fluido

Desliza horizontalmente entre 4 páginas con animación suave (*spring easing*) e indicador dinámico de 4 puntos:

### 1. INICIO (HOME)
- **Reloj Hero con Tipografía Tabular**: Horas y minutos gigantes (`HH:MM`) con segundos independientes (`:SS`) y fecha completa en español.
- **3 Anillos de Telemetría Anti-Aliased**:
  - **CPU %**: Color semántico (verde / ámbar / rojo) e icono vectorial.
  - **RAM %**: Consumo de memoria con icono de chip RAM.
  - **BATERÍA %**: Nivel, rayo de carga activa y porcentaje.
- **Puntuación de Salud Global (0-100)**: Algoritmo combinado de salud del hardware con anillo dinámico y etiqueta de diagnóstico.
- **Doble Sparkline Histórico**: Curvas continuas de actividad de CPU y RAM.

### 2. RED & ENLACE (NETWORK)
- **Velocímetro de Enlace Wi-Fi**: Tacómetro circular con velocidad en Mbps, SSID, barras de señal vectoriales y frecuencia (2.4 GHz / 5 GHz).
- **Flujo en Tiempo Real (60s)**: Gráfica dual de velocidad de bajada (verde) y subida (cian) con detección automática de picos máximos.
- **Latencia a 3 Destinos en Vivo**:
  - Puerta de Enlace (Router Gateway)
  - Cloudflare DNS (`1.1.1.1`)
  - Google DNS (`8.8.8.8`)
  - Mide **Ping (ms)**, **Jitter (variación ±ms)**, **Pérdida de paquetes (%)** y semáforo de calidad.

### 3. SISTEMA (SYSTEM)
- **Núcleos de CPU (Multi-Core)**: Barras verticales independientes para cada núcleo (Octa-Core) con altura y color mapeados según frecuencia y carga. Carga promedio (*Loadavg 1m, 5m, 15m*).
- **Temperaturas Térmicas (`/sys/class/thermal`)**: Lectura directa de sensores térmicos de CPU, Batería y GPU con alerta visual en caso de sobrecalentamiento (> 75°C).
- **Almacenamiento (`statvfs`)**: Barra segmentada de uso del almacenamiento interno (`/data`).
- **Batería Detallada**: Voltaje real (V), corriente (mA), potencia disipada (W), estado y tiempo estimado restante.

### 4. MOTOR C (PRUEBA DE EFICIENCIA)
- **Métricas de la Propia Aplicación en Vivo**:
  - **CPU de la App**: Medida desde `/proc/self/stat`.
  - **RAM de la App**: Consumo real VmRSS desde `/proc/self/status`.
  - **Allocs / Frame**: **0** garantizado.
- **Telemetría de Frame-Time**: Tiempo de renderizado exacto en microsegundos (`µs`), FPS reales medidos y percentil p99.
- **Huella de Binario**: Tamaño exacto del `.so`, APK, hilos y tiempo de arranque en frío.
- **BENCHMARK Interactivo (2 Segundos)**: Botón táctil que ejecuta 4 pruebas intensivas en vivo:
  1. *Pixel Fill Rate* (MPix/s)
  2. *Ancho de Banda Memcpy* (GB/s)
  3. *Criba de Eratóstenes* (MPrimes/s)
  4. *Cálculo Vectorial NEON SIMD* (GFLOPS)

---

## 🎨 Temas Visuales Glassmorphism

Toca dos veces sobre el reloj o pulsa el botón **TEMA** para alternar:
1. **Obsidian**: Superficies de pizarra oscura glassmorphism con acento cian cielo.
2. **AMOLED Puro**: Negro absoluto `#000000` con acentos esmeralda para máximo ahorro de batería.
3. **Cyber Neón**: Azul medianoche y violeta con resplandor neón magenta.
4. **Claro Titán**: Modo claro de lujo con contraste de alta legibilidad.

*La preferencia de tema se almacena de forma persistente en almacenamiento local.*

---

## 🛠️ Estructura Modular del Código Fuente

```text
app/src/main/cpp/
├── main.c           # Ciclo de vida NativeActivity, threads (UI + Sampler), VSync
├── ui.h / ui.c      # Controlador de UI, 4 páginas, swipe horizontal, easing
├── render.h / .c    # Rasterizador SDF anti-aliased, glass cards, sparklines, iconos vectoriales
├── text.h / text.c  # Atlas de fuentes TrueType, números tabulares, decodificador UTF-8
├── embedded_font.h  # Fuente TrueType embebida en el binario (100% autocontenida)
├── theme.h / .c     # 4 paletas de color semántico y persistencia en disco
├── sys_cpu.h / .c   # Parser /proc/stat delta, cpufreq, loadavg, /sys/class/thermal
├── sys_mem.h / .c   # Parser /proc/meminfo y statvfs (/data)
├── sys_net.h / .c   # Medición TCP ping/jitter/loss a 3 IPs, interfaces getifaddrs, tráfico
├── sys_batt.h / .c  # Telemetría de batería sysfs y fallback JNI
├── bench.h / .c     # Telemetría de proceso propio (/proc/self) y benchmark interactivo
└── stb_truetype.h   # Librería single-header de rasterización TrueType de dominio público
```

---

## 🧪 Pruebas Unitarias en Host

Incluye una suite de pruebas unitarias que se ejecuta en la máquina anfitriona (Linux / GCC / Clang):

```bash
make test
```

Verifica el cálculo de deltas de CPU, el parser de memoria, la máquina de estados del benchmark y la aserción de cero asignaciones por frame.

---

## 📦 Compilación y CI/CD

El workflow de **GitHub Actions** (`.github/workflows/build.yml`):
1. Ejecuta las pruebas unitarias en host.
2. Compila las librerías nativas para `arm64-v8a` y `armeabi-v7a`.
3. Aplica optimizaciones agresivas (`-O3 -flto -fvisibility=hidden -ffunction-sections -fdata-sections -Wl,--gc-sections`, `strip`).
4. Valida el presupuesto estricto de tamaño del APK (< 400 KB).
5. Firma y publica los artefactos y releases automáticos.
