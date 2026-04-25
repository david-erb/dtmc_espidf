#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <dtcore/dtobject.h>

#include <dtmc_base/dtiox.h>

#include <dtmc/dtiox_espidf_modbus_rtu_slave.h>

#include "dtiox_espidf_modbus_rtu_slave__private.h"

// -----------------------------------------------------------------------------
void
dtiox_espidf_modbus_rtu_slave_copy(dtiox_espidf_modbus_rtu_slave_t* this, dtiox_espidf_modbus_rtu_slave_t* that)
{
    (void)this;
    (void)that;
}

// -----------------------------------------------------------------------------
bool
dtiox_espidf_modbus_rtu_slave_equals(dtiox_espidf_modbus_rtu_slave_t* a, dtiox_espidf_modbus_rtu_slave_t* b)
{
    if (a == NULL || b == NULL)
        return false;

    return (a->model_number == b->model_number);
}

// -----------------------------------------------------------------------------
const char*
dtiox_espidf_modbus_rtu_slave_get_class(dtiox_espidf_modbus_rtu_slave_t* self)
{
    (void)self;
    return "dtiox_espidf_modbus_rtu_slave_t";
}

// -----------------------------------------------------------------------------
bool
dtiox_espidf_modbus_rtu_slave_is_iface(dtiox_espidf_modbus_rtu_slave_t* self, const char* iface_name)
{
    (void)self;
    return strcmp(iface_name, DTIOX_IFACE_NAME) == 0 || strcmp(iface_name, "dtobject_iface") == 0;
}

// -----------------------------------------------------------------------------
void
dtiox_espidf_modbus_rtu_slave_to_string(dtiox_espidf_modbus_rtu_slave_t* self, char* buffer, size_t buffer_size)
{
    if (self == NULL || buffer == NULL || buffer_size == 0)
        return;

    char tmp[128];
    dtuart_helper_to_string(&self->cfg.uart_config, tmp, sizeof(tmp));

    snprintf(buffer,
      buffer_size,
      "uart%" PRId32 "@%" PRId32 " rxpin=%" PRId32 " txpin=%" PRId32 " %s",
      self->cfg.uart_port_num,
      self->cfg.slave_id,
      self->cfg.rx_pin,
      self->cfg.tx_pin,
      tmp);
    buffer[buffer_size - 1] = '\0';
}
