#include <linux/refcount.h>
#include <linux/bitmap.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/sched.h>
#include <linux/ktime.h>
#include <linux/scatterlist.h>
#include <linux/vgaarb.h>
#include <linux/pinctrl/machine.h>
#include <linux/i2c.h>

#if !defined(CPTCFG_VGA_CONSOLE)
int vga_remove_vgacon(struct pci_dev *pdev)
{
        return 0;
}
#elif !defined(CPTCFG_DUMMY_CONSOLE)
int vga_remove_vgacon(struct pci_dev *pdev)
{
        return -ENODEV;
}
#else
int vga_remove_vgacon(struct pci_dev *pdev)
{
        int ret = 0;

        if (pdev != vga_default)
                return 0;
        vgaarb_info(&pdev->dev, "deactivate vga console\n");

        console_lock();
        if (con_is_bound(&vga_con))
                ret = do_take_over_console(&dummy_con, 0,
                                           MAX_NR_CONSOLES - 1, 1);
        if (ret == 0) {
                ret = do_unregister_con_driver(&vga_con);

                /* Ignore "already unregistered". */
                if (ret == -ENODEV)
                        ret = 0;
        }
        console_unlock();

        return ret;
}
#endif

#if IS_ENABLED(CPTCFG_ACPI)
bool i2c_acpi_get_i2c_resource(struct acpi_resource *ares,
                               struct acpi_resource_i2c_serialbus **i2c)
{
        struct acpi_resource_i2c_serialbus *sb;

        if (ares->type != ACPI_RESOURCE_TYPE_SERIAL_BUS)
                return false;

        sb = &ares->data.i2c_serial_bus;
        if (sb->type != ACPI_RESOURCE_SERIAL_TYPE_I2C)
                return false;

        *i2c = sb;
        return true;
}
static int i2c_acpi_find_match_adapter(struct device *dev, void *data)
{
        struct i2c_adapter *adapter = i2c_verify_adapter(dev);

        if (!adapter)
                return 0;

        return ACPI_HANDLE(dev) == (acpi_handle)data;
}

struct i2c_adapter *i2c_acpi_find_adapter_by_handle(acpi_handle handle)
{
        struct device *dev;

        dev = bus_find_device(&i2c_bus_type, NULL, handle,
                              i2c_acpi_find_match_adapter);
        return dev ? i2c_verify_adapter(dev) : NULL;
}
#endif

#ifdef CPTCFG_PINCTRL
/**
 * pinctrl_unregister_mappings() - unregister a set of pin controller mappings
 * @maps: the pincontrol mappings table passed to pinctrl_register_mappings()
 *  when registering the mappings.
 */
void pinctrl_unregister_mappings(const struct pinctrl_map *map)
{
    struct pinctrl_maps *maps_node;

    mutex_lock(&pinctrl_maps_mutex);
    list_for_each_entry(maps_node, &pinctrl_maps, node) {
        if (maps_node->maps == map) {
            list_del(&maps_node->node);
            kfree(maps_node);
            mutex_unlock(&pinctrl_maps_mutex);
            return;
        }
    }
    mutex_unlock(&pinctrl_maps_mutex);
}
EXPORT_SYMBOL_GPL(pinctrl_unregister_mappings);
#endif

EXPORT_SYMBOL(vga_remove_vgacon);
