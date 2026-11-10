04 - Character Device with Blocking Read and kfifo (driver_v14.c)

Adds a userspace data path. Samples produced by the interrupt work are queued in a kernel FIFO, and a process reading /dev/my_test_sensor sleeps until data is available.

What it does

Registers a character device (alloc_chrdev_region, cdev_add, class_create, device_create) that appears as /dev/my_test_sensor
The workqueue is the producer: it pushes each temperature sample into a kfifo (8 samples)
read() is the consumer: it sleeps with wait_event_interruptible() while the FIFO is empty, then returns one sample as text
The producer calls wake_up_interruptible() after queuing a sample
When the FIFO is full, the newest sample is dropped and logged
Keeps the sysfs attributes from the previous version

Concepts: character device registration, file_operations, wait queues, kfifo, copy_to_user, producer/consumer pattern, interruptible sleep (Ctrl+C works).

Try it

sh
insmod driver_v14.ko
cat /dev/my_test_sensor     # blocks until the next interrupt, then prints one value

Design note: an earlier hand-written ring buffer was replaced by kfifo, which takes care of wrap-around and index handling, and kfifo_out() returns the number of bytes copied, so an empty FIFO is handled safely.

Known limitations / TODO

The sample is removed from the FIFO before the user buffer size is checked, so a too-small buffer loses data
No O_NONBLOCK or poll() support yet
Teardown order: the IRQ should be disabled before cancel_work_sync() and kfifo_free()
Build

Out-of-tree module build against your kernel tree (example Kbuild):

make
obj-m += driver_v14.o
sh
make -C <path-to-kernel-tree> M=$PWD modules
License

GPL-2.0, same as the Linux kernel.
