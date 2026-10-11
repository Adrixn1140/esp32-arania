# esp32_arania

Araña cuadrúpeda servocontrolada: **ESP32 DevKit 1** + **PCA9685** (I2C) + **4× SG90**.
Un PCA9685 de 16 canales PWM a 50 Hz controla un servo por pata.

## Estado actual

- 4 patas en postura **sprawling (reptiliana)**: cada SG90 gira sobre un eje vertical (yaw)
  y "adelante/atrás" es un barrido horizontal.
- **Modo posición fija (set-and-hold)**: al encender los 4 servos van a neutro 0° y quedan
  **quietos** (sin barrido automático). Con el teclado se posiciona una pata a la vez y se
  mantiene en el ángulo elegido para acomodarla. Las posiciones viven en RAM.
- Marchas (wave/trot), giro y control WiFi: en roadmap, cuando el mapeo esté calibrado.

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
| ch13 | delantera izquierda | FL |
| ch14 | trasera izquierda | BL |
| ch15 | trasera derecha | BR |

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
  app_main.c     Init I2C + PCA9685 + keypad; modo posición fija controlado por teclado
  legs.c/h       LEGS[] (ch12..ch15) + invert/offset de neutro; set_angle y rest
  test_mode.c/h  Barrido lento de una pata o de las 4 (legado, ya no se usa en boot)
  pca9685.c/h    Driver PCA9685: init (50 Hz), set_angle (-90..+90), read_channel_us
  keypad.c/h     Driver HX-543 3x4: scan por columnas, debounce, tecla por pulsacion
  CMakeLists.txt
```

- **Ángulos**: rango **-90…+90** del driver; **neutro 0°** = pose natural capturada por pata.
  Mapeo del driver: -90° → 500 µs, 0° → 1450 µs, +90° → 2400 µs.
- **Paso de posición**: `POS_STEP_DEG = 5` en `app_main.c` (`*`/`#`).

## Neutro capturado (NVS)

Al encender, el firmware carga los offsets de neutro de cada pata desde NVS. Si no existen,
los **captura automáticamente** leyendo el pulso actual de los canales 12–15 del PCA9685 (la
pose en la que dejaste las patas) y los guarda. Después `legs_rest_all()` **posiciona las 4
patas solas en esa pose**, y lo repite en cada arranque. Así **0° = pata estirada/natural**.

### Teclas

- `'1'` selecciona **FR** (ch12)  · `'2'` **FL** (ch13) · `'3'` **BL** (ch14) · `'4'` **BR** (ch15)
- `'*'` / `'#'` restan/suman **5°** a la pata seleccionada respecto a su natural
- `'5'` **captura la pose actual de la pata seleccionada como su nuevo 0** y lo guarda en NVS
- `'0'` todas a su **natural** (0° relativo)

### Marcha (test de caminado)

- `'6'` **iniciar/parar** la marcha
- `'7'` cambiar **patrón**: `TROT` (diagonales sincronizadas, por defecto) ↔ `WAVE` (una pata a la vez)
- `'8'` **más rápido** · `'9'` **más lento** (`gait.c`: `GAIT_AMP_DEG=35`, paso 4–40 ms/grado)
- Mientras camina, las teclas de posición (`1`‑`5`, `*`, `#`) se ignoran; `'6'` o `'0'` lo paran.

Qué validar en hardware:

1. **Canal**: la pata física que se mueve debe ser la del log (`FR ch12`, etc.). Si no, cambia
   el canal en `LEGS[]` (`main/legs.c`).
2. **Sentido/invert**: si una pata izquierda (FL/BL) se mueve al revés que su espejo derecho,
   cambia `invert` (true↔false) solo para esa pata en `LEGS[]`.
3. **Rango mecánico**: al subir/bajar con `*`/`#` que no truene ni choque piezas; si hace falta,
   ajusta el recorrido dentro de ±90° del driver.

Después de posicionar/calibrar, se habilitan las marchas (wave/trot), giro y WiFi (roadmap).

## Diagnóstico rápido (si no responde ningún servo)

El firmware ya escanea el bus I2C al arrancar y loguea todo. Fíjate en el monitor:

1. **`found device: 0x40`** aparece → el PCA9685 está detectado y el I2C funciona. Si NO aparece,
   revisa SDA→GPIO21, SCL→GPIO22, VDD→3V3 y GND común del PCA9685.
2. **`pca9685_init fallo`** → el módulo no confirma registros: probablemente sin alimentación.
3. Si el bus está OK pero **los servos no giran**: revisa la power de los servos:
   - **V+** del PCA9685 → **5 V externo** (NO 3V3). Sin esto ningún servo se mueve aunque el I2C funcione.
   - **GND** externo común con el ESP32.
   - Pin **OE** del PCA9685 en **LOW** (a GND o a un GPIO con `PCA9685_OE_GPIO` en `app_main.c`).
     En muchas placas, si OE queda flotando/alto, las salidas están deshabilitadas y todo queda mudo.

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
