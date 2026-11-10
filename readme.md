01 - Polling MMIO Platform Driver (driver_v1.c)

The baseline driver. It binds to a Device Tree node, maps the sensor registers and reads the temperature periodically.

What it does

Matches the node with compatible = "mert,my-test-device"
Allocates private data with devm_kzalloc()
Gets the MMIO resource with platform_get_resource() and maps it with devm_ioremap_resource()
Reads the temperature register with readl() from a delayed work item every 2 seconds
Cancels the work with cancel_delayed_work_sync() in remove()

Concepts: platform driver lifecycle (probe/remove), Device Tree matching, managed resources (devm), delayed work.

Try it

sh
insmod driver_v1.ko
dmesg | tail        # periodic temperature = ...
rmmod driver_v1

Limitation: polling wastes CPU and adds latency. The next version replaces it with an interrupt.
