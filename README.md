# esp32_arania

Araña cuadrúpeda servocontrolada: **ESP32 DevKit 1** + **PCA9685** (I2C) + **4× SG90**.
Un PCA9685 de 16 canales PWM a 50 Hz controla un servo por pata.

## Estado actual

- 4 patas en postura **sprawling (reptiliana)**: cada SG90 gira sobre un eje vertical (yaw)
  y "adelante/atrás" es un barrido horizontal.
- **Marcha wave** (una pata a la vez) en bucle, orden contralateral `FR → BL → FL → BR`.
- Teclado **HX-543 (3×4)** cableado y validado: controla marcha, sentido y velocidad.

## Wiring

### PCA9685 ↔ ESP32 (I2C)

| ESP32 | PCA9685 | Nota |
|---|---|---|
| GPIO21 | SDA | I2C data |
| GPIO22 | SCL | I2C clock |
| 3V3 | VDD | Lógica |
| GND | GND | Tierra común |

Target `esp32` (DevKit 1) según `sdkconfig.defaults`. Protocolo I2C: driver `i2c_master`
de ESP-IDF v6.1, 100 kHz, pull-ups internos, dirección del módulo `0x40`.

### Servos (alimentados con fuente externa 5 V)

| PCA9685 | Pata | Índice |
|---|---|---|
| ch12 | delantera derecha | FR |
| ch13 | trasera derecha | BR |
| ch14 | delantera izquierda | FL |
| ch15 | trasera izquierda | BL |

- Las patas **izquierdas** van montadas espejadas → `invert = true` en `LEGS[]`
  (`main/app_main.c`). Si una pata gira al revés en hardware, ajustar ese flag.
- **V+** del PCA9685 → **5 V externo** (alimentación de los servos, NO 3V3).
- **GND** externo común con el ESP32.

### Teclado HX-543 3×4 (7 pines)

| Pin conector (1→7) | Función | ESP32 | Modo |
|---|---|---|---|
| 1 | Fila R1 | GPIO 13 | entrada + pull-up interno |
| 2 | Fila R2 | GPIO 14 | entrada + pull-up |
| 3 | Fila R3 | GPIO 27 | entrada + pull-up |
| 4 | Fila R4 | GPIO 26 | entrada + pull-up |
| 5 | Col C1 | GPIO 25 | salida |
| 6 | Col C2 | GPIO 33 | salida |
| 7 | Col C3 | GPIO 32 | salida |

Tabla de teclas configurable en `main/keypad.c` (constante `s_keys`).

## Firmware

```
main/
  app_main.c     Araña: LEGS[] (ch12..ch15), marcha wave, reposo en 0°, control por teclado
  pca9685.c/h    Driver PCA9685: init (50 Hz), set_angle (-90..+90), set_channel_us
  keypad.c/h     Driver HX-543 3x4: scan por columnas, debounce, tecla por pulsacion
  CMakeLists.txt
```

- **Ángulos**: rango **-90…+90** del driver; **neutro 0°** = postura sprawling con patas abiertas.
  Mapeo del driver: -90° → 500 µs, 0° → 1450 µs, +90° → 2400 µs.
- **Barrido**: `STEP_DEG = 40`, así cada pata barre de `-40°` a `+40°` alrededor del neutro.

### Marcha wave

Una pata a la vez, en bucle continuo, orden contralateral `FR → BL → FL → BR`. Cada pata:

```
neutro (0) ──▶ adelante (dir·40) ──▶ atrás (−dir·40) ──▶ neutro (0)
```

mientras las otras 3 permanecen en neutro. `dir` es el sentido (`+1` adelante, `-1` atrás).
Solo la tarea `walk_task` mueve los servos; el bucle principal solo lee el teclado y
actualiza el estado (marcha, sentido, velocidad).

### Teclas

- `'1'` iniciar / parar la marcha.
- `'2'` invertir sentido (adelante / atrás).
- `'3'` subir velocidad (bajar `ms/grado`, mínimo 4).
- `'4'` bajar velocidad (subir `ms/grado`, máximo 40).
- `'0'` parar y reposo total (todas las patas a neutro).

## Compilar y flashear

Requisitos: ESP-IDF **v6.1** (ruta usada: `~/.espressif/v6.1/esp-idf`).

```bash
source ~/.espressif/v6.1/esp-idf/export.sh
idf.py set-target esp32      # una sola vez
idf.py build
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor
```

El firmware busca el PCA9685 en `0x40`; si no lo encuentra loguea un error claro en el
monitor (`PCA9685 not found at 0x40 - check wiring/pull-ups`).
