# Changelog

## v1.2.0 - 2026-05-15

- **API change**: `callback_fn`, `callback_context`, and `periodic_task_handle` removed from `dtinterval_espidf_config_t`; set the callback at runtime with `dtinterval_espidf_set_callback()` instead.
- New `dtinterval_espidf_set_callback()` for attaching the periodic callback after construction.
- `dtinterval_espidf_start()` now runs the interval loop in the calling task, blocking until paused, replacing the previous external-task-notification model.
- Timer callback changed from an ISR (`IRAM_ATTR` + `vTaskNotifyGiveFromISR`) to a regular `esp_timer` callback using `xTaskNotifyGive`, removing the IRAM placement requirement.
- Fix: `dtinterval_espidf_pause()` now unblocks a running `start()` call via `xTaskNotifyGive` so the caller returns promptly.
- Fix: error code is now preserved through the `start()` failure path instead of always being wrapped as `DTERR_FAIL`.
