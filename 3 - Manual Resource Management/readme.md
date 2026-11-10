03 - Manual Resource Management (driver_v9.c)

The same functionality as the previous driver, but without any devm_* helpers. Every resource is acquired and released by hand, which shows what devm does behind the scenes.

What it does

kzalloc() / kfree() instead of devm_kzalloc()
ioremap() / iounmap() instead of devm_ioremap_resource()
request_irq() / free_irq() instead of devm_request_irq()
Uses goto labels in probe() to release resources in reverse order of acquisition
remove() repeats the same cleanup in reverse order

Concepts: resource lifetime, reverse-order cleanup, the kernel goto err_* idiom.

Resource order

kzalloc -> ioremap -> request_irq -> sysfs files
  ^          ^            ^             |
kfree    iounmap      free_irq    device_remove_file
         (cleanup runs right to left)

Known limitations / TODO

In remove(), free_irq() should run before cancel_work_sync() so the work can't be rescheduled after it is cancelled
