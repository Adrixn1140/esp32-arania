# AGENTS.md

Guía para agentes/IA que trabajen en este repositorio. Proyecto de firmware ESP-IDF:
araña cuadrúpeda con 4 servos SG90 via PCA9685 (I2C) y teclado HX-543 3x4.

## Comandos

```bash
source ~/.espressif/v6.1/esp-idf/export.sh   # siempre antes de idf.py
idf.py build                                    # compila
idf.py -p /dev/ttyUSB0 flash                    # flashea (puerto del DevKit, CH340)
idf.py -p /dev/ttyUSB0 monitor                  # monitor serie (salir: Ctrl+] o Ctrl+T X)
idf.py set-target esp32                         # una sola vez (target = ESP32 clásico)
```

- Nunca editar `sdkconfig` ni `build/` (generados). Cambios de config van en `sdkconfig.defaults`.
- Después de cualquier cambio: `idf.py build`. Si hay placa conectada, `flash` + `monitor` para validar.
- El teclado se valida en hardware presionando teclas y mirando el monitor.

## Estructura

```
CMakeLists.txt            # project(esp32_arania)
sdkconfig.defaults        # CONFIG_IDF_TARGET="esp32"
main/
  app_main.c              # init I2C + PCA9685 + keypad; MODO CALIBRACION por teclas
  legs.c/h                # LEGS[] (ch12=FR, ch13=BR, ch14=FL, ch15=BL) + invert; set_angle/rest
  test_mode.c/h           # barrido lento de una pata o de las 4 (sincronizado)
  pca9685.c/h             # driver PCA9685 (I2C 0x40): init 50Hz, set_angle -90..+90
  keypad.c/h              # driver HX-543: scan columnas en LOW + filas pull-up, debounce
  CMakeLists.txt          # SRCS: app_main.c legs.c test_mode.c pca9685.c keypad.c
README.md                 # wiring, modo calibracion, teclas, comandos
```

## Convenciones de código

- C (estándar del proyecto), siguiendo estilo de ESP-IDF / ejemplos existentes.
- No añadir comentarios salvo que aporten contexto (p. ej. fórmulas del datasheet).
- Tabla de patas y su `invert` viven SOLO en `main/legs.c` (`LEGS[]`); el resto del código
  usa `legs_set_angle(dev, idx, grados)` / `legs_rest_all(dev)` y `legs_name()`/`legs_channel()`.
- Mapeo: `LEGS[]` = ch12 FR, ch13 BR, ch14 FL, ch15 BL. Patas izquierdas con `invert = true`.
- Ángulos del driver: **-90..+90**, **neutro 0°** (sprawling, patas abiertas).
- **Estado actual: MODO CALIBRACION.** Al encender nada se mueve; cada tecla mueve una sola
  pata (lenta). Las marchas/gait están en `git history` / roadmap y se montan sobre `legs.c`.
- `pca9685_set_angle(dev, ch, grados)` acepta `int16_t` y acota fuera de rango.

## Modo calibración (test)

- Al encender: `test_mode_boot_sequence()` mueve cada pata una por una sin teclado (para
  verificar que responden), y luego queda en reposo esperando teclas.
- Teclas: 1=FR, 2=BR, 3=FL, 4=BL (una pata sola, lenta), 5=las 4 juntas, 0=reposo.
- Cada barrido: 0° → +40° → 0° → −40° → 0° con `TEST_MS_PER_DEG=30` y vuelve a neutro.
- Si una pata física no corresponde al canal, o una izquierda barre al revés de su espejo
  derecho: ajustar `LEGS[]` en `main/legs.c`, no el cableado.

## Hardware / cableado (no cambiar sin confirmar con el usuario)

| Componente | Conexión |
|---|---|
| PCA9685 | SDA→GPIO21, SCL→GPIO22, VDD→3V3, GND común |
| Servos | V+→5V externo, ch12=FR, ch13=BR, ch14=FL, ch15=BL |
| Teclado HX-543 | conector 1..7 → GPIO 13(R1) 14(R2) 27(R3) 26(R4) 25(C1) 33(C2) 32(C3) |

Si una tecla sale intercambiada en el monitor: ajustar `s_keys`/GPIOS en `main/keypad.c`,
no el cableado. Si una pata gira al revés: ajustar `invert` en `LEGS[]`.

## Entorno conocido

- ESP-IDF v6.1 en `~/.espressif/v6.1/esp-idf` (export.sh creo el venv; si falta,
  `IDF_PATH=~/.espressif/v6.1/esp-idf <idf>/install.sh esp32`).
- Placa: ESP32 DevKit 1 (CH340, `/dev/ttyUSB0`).
- Grupo `dialout` ya asignado; usuario puede flashear sin sudo.
- Un `timeout ... idf.py monitor` puede dejar un `esp_idf_monitor` huérfano sujetando el puerto:
  revisar con `fuser /dev/ttyUSB0` antes de flashear y matar el proceso si hace falta.
