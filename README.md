# dtmc_espidf

ESP-IDF implementations of the `dtmc_base` porting contracts — GPIO, ADC, UART, CAN bus, Modbus RTU, MQTT, Ethernet, NVS, LCD panels, and DAC peripherals — plus a set of demo applications that exercise each one.

`dtmc_espidf` is part of the **[Embedded Applications Lab](https://david-erb.github.io/embedded)** — a set of working applications based on a set of reusable libraries across MCU, Linux, and RTOS targets.

---

## What's inside

| Area | Headers |
|------|---------|
| GPIO / ADC | `dtgpiopin_espidf`, `dtadc_espidf_oneshot` |
| Serial / CAN | `dtiox_espidf_uart`, `dtiox_espidf_canbus` |
| Modbus RTU | `dtiox_espidf_modbus_rtu_slave` |
| Networking | `dtethernet_espidf`, `dtstation_espidf`, `dtnetportal_espmqtt`, `dtmqttclient_espidf` |
| Storage | `dtnvblob_espidf_nvs` |
| Display | `dtdisplay_lcd_ili9341` |
| Peripherals | `dtmcp4728_espidf` (quad-channel DAC), `dtdotstar_espidf` (LED strip) |
| Startup | `dtprepper_espidf`, `dtinterval_espidf` |

---

## Demo applications

| App | What it shows |
|-----|--------------|
| `demo_gpiopin_button` | Interrupt-driven GPIO input |
| `demo_adc_oneshot` | One-shot ADC sampling |
| `demo_iox_uart` / `demo_iox_canbus` | UART and CAN I/O streams |
| `demo_iox_modbus` / `demo_netportal_modbus` | Modbus RTU slave and netportal bridging |
| `demo_netportal_espmqtt` / `mqtt_wired` | MQTT over Wi-Fi and wired Ethernet |
| `demo_netportal_can` / `demo_netportal_uart` | Netportal over CAN and UART transports |
| `demo_display_hello` / `demo_lvgl_card` | ILI9341 LCD and LVGL widget demo |
| `demo_mcp4728_square` | MCP4728 DAC waveform generation |
| `demo_write_kvp` | NVS key-value persistence |
| `demo_runtime_info` | Runtime diagnostics and task registry |
| `test_dotstar` | DotStar LED strip validation |

---

## Dependencies

`dtmc_espidf` depends on `dtcore`, `dtmc_base`, and `dtmc_services`, which are included as git submodules under `submodules/`. After cloning, initialize them with:

```sh
git submodule update --init --recursive
```

---

## Docs

See the [dtmc_espidf documentation site](https://david-erb.github.io/dtmc_espidf/) for the full API reference.
