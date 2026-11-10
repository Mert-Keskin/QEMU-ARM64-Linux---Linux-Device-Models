02 - Interrupt-Driven Driver with Workqueue and sysfs (driver_v8.c)

The first complete version. The sensor raises an interrupt, the handler acknowledges it quickly, and the real processing runs in process context.

What it does

Top half: the IRQ handler writes IRQ_CLEAR and calls schedule_work()
Bottom half: the work function reads the temperature and compares it with a threshold to set an alarm flag
Protects temperature, threshold and alarm with a mutex
Exposes three sysfs attributes:
Attribute	Access	Description
temperature	read-only	Latest reading
threshold	read/write	Alarm threshold (default 80), parsed with kstrtou32()
alarm	read-only	1 when temperature >= threshold
Creates sysfs files with proper error unwinding if one of them fails

Concepts: interrupt context vs. process context, deferring work, devm_request_irq(), platform_get_irq(), mutex-protected shared state, DEVICE_ATTR_RO/RW, sysfs_emit().

Try it

sh
insmod driver_v8.ko
cat /sys/bus/platform/devices/<device>/temperature
echo 90 > /sys/bus/platform/devices/<device>/threshold
cat /sys/bus/platform/devices/<device>/alarm

Known limitations / TODO

Teardown order: the IRQ can reschedule work after cancel_work_sync() in remove()
device_create_file() could be replaced with an attribute group (.dev_groups)
Changing the threshold does not re-evaluate the alarm until the next interrupt
