Linux Kernel Platform Driver Exercises

A progression of Linux platform driver exercises for a fake memory-mapped temperature sensor emulated in QEMU. Each folder is a self-contained driver that builds on the ideas of the previous one.

Folder	Topic	Key concepts
01-polling-mmio	Polling platform driver	Device Tree matching, devm, MMIO, delayed work
02-irq-workqueue-sysfs	Interrupt-driven driver with sysfs	Top/bottom half, workqueue, mutex, sysfs attributes
03-manual-resource-management	Same driver without devm	kzalloc, ioremap, request_irq, goto error unwinding
04-chardev-blocking-kfifo	Character device with blocking read	Wait queues, kfifo, producer/consumer, copy_to_user

Tested on: <kernel version> / QEMU <version> / <architecture> (fill in)

Kernel requirements: .remove returning void needs Linux 6.11+, and the one-argument class_create() needs 6.4+.

Sensor register map (QEMU model):

Offset	Name	Description
0x00	TEMPERATURE	Current temperature value (read)
0x04	CONTROL	Control register (used in early versions to trigger an interrupt)
0x0C	IRQ_CLEAR	Write 1 to acknowledge the interrupt and deassert the IRQ line
