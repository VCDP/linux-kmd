#ifndef _BACKPORT_LINUX_I2C_H
#define _BACKPORT_LINUX_I2C_H
#include <linux/version.h>
#include_next <linux/i2c.h>
#include <linux/acpi.h>
bool i2c_acpi_get_i2c_resource(struct acpi_resource *ares,
			       struct acpi_resource_i2c_serialbus **i2c);

struct i2c_adapter *i2c_acpi_find_adapter_by_handle(acpi_handle handle);

#endif
