# Native C Clock & Wi-Fi Speed App (Android 11 - 16)

Aplicación Android desarrollada **100% en C puro** (sin código Java en la aplicación), diseñada específicamente para versiones de Android desde **Android 11 (API 30)** hasta **Android 16 (API 36)**.

---

## 🚀 Características

1. **Arquitectura 100% C (`NativeActivity`)**:
   - Punto de entrada nativo `ANativeActivity_onCreate`.
   - Renderizado directo por software en el búfer de `ANativeWindow` (`RGBA_8888`), sin capas intermedias ni sobrecarga de shaders.
   - Puntero de eventos y cola táctil directa (`AInputQueue`).
   - Hilo de renderizado dedicado a 60 FPS con frame-pacing preciso.

2. **Reloj Digital y Estado del Sistema en Tiempo Real**:
   - Dígitos estilizados de 7 segmentos para horas, minutos y segundos (`HH:MM:SS`).
   - Fecha completa en español (`Día, DD de Mes de AAAA`).
   - Tiempo de actividad del dispositivo (*Uptime*) obtenido mediante `sysinfo`.
   - Zona horaria activa.

3. **Monitor y Velocímetro de Wi-Fi en Vivo**:
   - Tacómetro / velocímetro circular con aguja animada que muestra la **rapidez del enlace Wi-Fi en Mbps** (`WifiInfo.getLinkSpeed`).
   - Velocidades de transmisión y recepción (`TxLinkSpeedMbps` y `RxLinkSpeedMbps`, API 29+).
   - Velocidad máxima soportada por el hardware (`MaxSupportedTxLinkSpeedMbps`, API 30+).
   - Medidor de intensidad de señal (barras y RSSI en dBm).
   - Identificación de estándar de red: Wi-Fi 4 (802.11n), Wi-Fi 5 (802.11ac), Wi-Fi 6/6E (802.11ax), Wi-Fi 7 (802.11be) y frecuencia en MHz (2.4 GHz vs 5 GHz).
   - Nombre de red (SSID).

4. **Tráfico de Red en Tiempo Real con Gráfico (Sparkline)**:
   - Medición por segundo de velocidad de descarga (KB/s o MB/s) y subida (KB/s o MB/s) mediante `TrafficStats`.
   - Gráfico de osciloscopio / sparkline en vivo con los últimos 40 muestreos.
   - Acumulador de datos transferidos totales (Rx / Tx).

5. **Interactividad y Temas**:
   - Toca cualquier parte de la pantalla para alternar entre 4 temas neón:
     - **Cyber Cyan** (Neón cian / estilo futurista)
     - **Emerald Matrix** (Verde matriz)
     - **Solar Amber** (Ámbar cálido)
     - **Sunset Purple** (Púrpura neón)
   - Efecto visual de ondas (*ripple*) al tocar la pantalla.
   - Diseño adaptable automático tanto en modo horizontal (*landscape*) de doble columna como vertical (*portrait*).

---

## 🛠️ Estructura del Proyecto

```
native/
├── .github/
│   └── workflows/
│       └── build.yml               # Workflow de GitHub Actions para compilar el APK
├── app/
│   ├── build.gradle.kts            # Configuración Gradle con compileSdk 36, minSdk 30
│   └── src/
│       └── main/
│           ├── AndroidManifest.xml # Manifiesto con NativeActivity y permisos
│           ├── cpp/
│           │   ├── CMakeLists.txt  # Configuración CMake para NDK
│           │   ├── main.c          # Ciclo de vida NativeActivity e hilo de render
│           │   ├── ui_renderer.c   # Renderizador de UI, tacómetro, reloj y temas
│           │   ├── ui_renderer.h
│           │   ├── wifi_stats.c    # Consultas NDK/JNI a WifiManager y TrafficStats
│           │   ├── wifi_stats.h
│           │   ├── font8x16.c      # Matriz de fuente bitmap limpia
│           │   └── font8x16.h
│           └── res/
│               └── values/
│                   └── strings.xml
├── build.gradle.kts
├── settings.gradle.kts
├── build_local.sh                  # Script de compilación, empaquetado y prueba local
└── README.md
```

---

## ⚙️ Compilación Automática en GitHub Actions

El repositorio incluye el archivo `.github/workflows/build.yml`. Cada vez que hagas `git push` a `main` o abras un Pull Request:
1. GitHub Actions configurará el entorno Ubuntu con Java 17, Android SDK Platform 36 y NDK.
2. Compilará automáticamente el proyecto usando Gradle y CMake.
3. Generará el artefacto descargable: `NativeC-Clock-WiFi-APK` (APK firmado y listo para instalar).

---

## 📱 Compilación y Prueba Local

Para compilar y probar directamente en un dispositivo Android con Termux o Linux:
```bash
# Compilar, empaquetar, firmar e instalar automáticamente mediante ADB:
./build_local.sh
```
