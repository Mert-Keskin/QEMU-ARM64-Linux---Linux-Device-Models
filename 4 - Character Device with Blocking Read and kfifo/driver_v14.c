// Kfifo
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/errno.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/kfifo.h>

#define SENSOR_REG_TEMPERATURE 0x00
#define SENSOR_REG_IRQ_CLEAR   0x0C

#define SENSOR_FIFO_SIZE 8

struct my_test_data {
    void __iomem *base;
    struct work_struct irq_work;
    int irq;

    u32 temperature;
    u32 threshold;
    bool alarm;

    struct mutex lock;

    /*
     * Wait queue for processes waiting for
     * new sensor data.
     */
    wait_queue_head_t wait_queue;

    /*
     * Kernel FIFO.
     *
     * The FIFO stores u32 temperature values.
     */
    struct kfifo sensor_fifo;

    /*
     * Character device information.
     */
    dev_t devt;
    struct cdev cdev;
    struct class *class;
    struct device *device;
};

/*
 * Workqueue function.
 *
 * Runs in process context.
 *
 * The workqueue is the producer of sensor data.
 */
static void my_test_irq_work(struct work_struct *work)
{
    struct my_test_data *data =
        container_of(work,
                     struct my_test_data,
                     irq_work);

    u32 temperature;
    u32 threshold;
    bool alarm;

    pr_info("MY_TEST_DRIVER: IRQ work started\n");

    mutex_lock(&data->lock);

    /*
     * Read temperature from the sensor.
     */
    temperature =
        readl(data->base + SENSOR_REG_TEMPERATURE);

    /*
     * Save latest temperature for sysfs.
     */
    data->temperature = temperature;

    threshold = data->threshold;

    /*
     * Check temperature against threshold.
     */
    if (temperature >= threshold)
        data->alarm = true;
    else
        data->alarm = false;

    alarm = data->alarm;

    /*
     * Put temperature into the kernel FIFO.
     *
     * kfifo_in() handles the internal
     * read/write positions and wrapping.
     */
    if (kfifo_avail(&data->sensor_fifo) >= sizeof(temperature)) {

        kfifo_in(&data->sensor_fifo,
                 &temperature,
                 sizeof(temperature));

        pr_info("MY_TEST_DRIVER: sample added to FIFO\n");

    } else {

        /*
         * FIFO is full.
         *
         * For this exercise, drop the
         * newest sample.
         */
        pr_info("MY_TEST_DRIVER: sensor FIFO full, sample dropped\n");
    }

    mutex_unlock(&data->lock);

    /*
     * Wake processes sleeping in read().
     */
    wake_up_interruptible(&data->wait_queue);

    pr_info("MY_TEST_DRIVER: temperature = %u, threshold = %u, alarm = %s\n",
            temperature,
            threshold,
            alarm ? "ON" : "OFF");
}

/*
 * Interrupt handler.
 *
 * Runs in interrupt context.
 */
static irqreturn_t my_test_irq_handler(int irq, void *dev_id)
{
    struct my_test_data *data = dev_id;

    pr_info("MY_TEST_DRIVER: IRQ received! irq=%d\n",
            irq);

    /*
     * Acknowledge the interrupt in the sensor.
     */
    writel(1, data->base + SENSOR_REG_IRQ_CLEAR);

    pr_info("MY_TEST_DRIVER: IRQ acknowledged\n");

    /*
     * Defer sensor processing to the workqueue.
     */
    schedule_work(&data->irq_work);

    pr_info("MY_TEST_DRIVER: IRQ work scheduled\n");

    return IRQ_HANDLED;
}

/*
 * sysfs temperature read function.
 */
static ssize_t temperature_show(struct device *dev,
                                struct device_attribute *attr,
                                char *buf)
{
    struct my_test_data *data = dev_get_drvdata(dev);
    u32 temperature;

    mutex_lock(&data->lock);

    temperature = data->temperature;

    mutex_unlock(&data->lock);

    return sysfs_emit(buf, "%u\n", temperature);
}

static DEVICE_ATTR_RO(temperature);

/*
 * sysfs threshold read function.
 */
static ssize_t threshold_show(struct device *dev,
                              struct device_attribute *attr,
                              char *buf)
{
    struct my_test_data *data = dev_get_drvdata(dev);
    u32 threshold;

    mutex_lock(&data->lock);

    threshold = data->threshold;

    mutex_unlock(&data->lock);

    return sysfs_emit(buf, "%u\n", threshold);
}

/*
 * sysfs threshold write function.
 */
static ssize_t threshold_store(struct device *dev,
                               struct device_attribute *attr,
                               const char *buf,
                               size_t count)
{
    struct my_test_data *data = dev_get_drvdata(dev);
    u32 threshold;
    int ret;

    ret = kstrtou32(buf, 10, &threshold);

    if (ret)
        return ret;

    mutex_lock(&data->lock);

    data->threshold = threshold;

    mutex_unlock(&data->lock);

    return count;
}

static DEVICE_ATTR_RW(threshold);

/*
 * sysfs alarm read function.
 */
static ssize_t alarm_show(struct device *dev,
                          struct device_attribute *attr,
                          char *buf)
{
    struct my_test_data *data = dev_get_drvdata(dev);
    bool alarm;

    mutex_lock(&data->lock);

    alarm = data->alarm;

    mutex_unlock(&data->lock);

    return sysfs_emit(buf, "%u\n", alarm);
}

static DEVICE_ATTR_RO(alarm);

/*
 * Character-device open().
 *
 * inode->i_cdev identifies our cdev.
 *
 * container_of() gives us our my_test_data structure.
 */
static int my_test_open(struct inode *inode,
                        struct file *file)
{
    struct my_test_data *data =
        container_of(inode->i_cdev,
                     struct my_test_data,
                     cdev);

    pr_info("MY_TEST_DRIVER: open() called\n");

    /*
     * Save driver data for future operations
     * on this open file.
     */
    file->private_data = data;

    return 0;
}

/*
 * Character-device read().
 *
 * If the FIFO is empty, put the calling
 * process to sleep.
 *
 * When data becomes available, consume
 * one temperature sample.
 */
static ssize_t my_test_read(struct file *file,
                            char __user *buf,
                            size_t count,
                            loff_t *ppos)
{
    struct my_test_data *data = file->private_data;

    char temperature_str[32];

    u32 temperature;

    int len;
    int ret;

    /*
     * Wait until the FIFO contains data.
     */
    ret = wait_event_interruptible(
        data->wait_queue,
        !kfifo_is_empty(&data->sensor_fifo)
    );

    if (ret)
        return ret;

    /*
     * Protect FIFO access.
     */
    mutex_lock(&data->lock);

    /*
     * Remove one temperature from
     * the FIFO.
     */
    if (kfifo_out(&data->sensor_fifo,
                  &temperature,
                  sizeof(temperature)) != sizeof(temperature)) {

        mutex_unlock(&data->lock);

        return -EIO;
    }

    mutex_unlock(&data->lock);

    /*
     * Convert temperature to text.
     */
    len = scnprintf(temperature_str,
                    sizeof(temperature_str),
                    "%u\n",
                    temperature);

    /*
     * Make sure userspace supplied
     * a large enough buffer.
     */
    if (count < len)
        return -EINVAL;

    /*
     * Copy data from kernel space
     * to userspace.
     */
    if (copy_to_user(buf,
                     temperature_str,
                     len))
        return -EFAULT;

    return len;
}

/*
 * Character-device operations.
 */
static const struct file_operations my_test_fops = {
    .owner = THIS_MODULE,
    .open = my_test_open,
    .read = my_test_read,
};

static int my_test_probe(struct platform_device *pdev)
{
    struct resource *res;
    struct my_test_data *data;
    int ret;

    pr_info("MY_TEST_DRIVER: probe() called!\n");

    pr_info("MY_TEST_DRIVER: device name = %s\n",
            dev_name(&pdev->dev));

    /*
     * Allocate driver-private data.
     */
    data = devm_kzalloc(&pdev->dev,
                        sizeof(*data),
                        GFP_KERNEL);

    if (!data)
        return -ENOMEM;

    /*
     * Initialize mutex.
     */
    mutex_init(&data->lock);

    /*
     * Initialize wait queue.
     */
    init_waitqueue_head(&data->wait_queue);

    /*
     * Allocate kernel FIFO.
     *
     * The FIFO holds SENSOR_FIFO_SIZE
     * u32 temperature values.
     */
    ret = kfifo_alloc(&data->sensor_fifo,
                      SENSOR_FIFO_SIZE * sizeof(u32),
                      GFP_KERNEL);

    if (ret) {
        dev_err(&pdev->dev,
                "failed to allocate sensor FIFO: %d\n",
                ret);
        return ret;
    }

    /*
     * Default threshold.
     */
    data->threshold = 80;

    /*
     * Get MMIO resource from Device Tree.
     */
    res = platform_get_resource(pdev,
                                IORESOURCE_MEM,
                                0);

    if (!res) {
        pr_err("MY_TEST_DRIVER: failed to get MMIO resource\n");
        ret = -ENODEV;
        goto err_fifo;
    }

    pr_info("MY_TEST_DRIVER: resource start = 0x%llx\n",
            (unsigned long long)res->start);

    pr_info("MY_TEST_DRIVER: resource end = 0x%llx\n",
            (unsigned long long)res->end);

    /*
     * Map hardware registers.
     */
    data->base = devm_ioremap_resource(&pdev->dev,
                                       res);

    if (IS_ERR(data->base)) {
        pr_err("MY_TEST_DRIVER: ioremap failed\n");
        ret = PTR_ERR(data->base);
        goto err_fifo;
    }

    pr_info("MY_TEST_DRIVER: MMIO mapped successfully!\n");

    /*
     * Get IRQ from Device Tree.
     */
    data->irq = platform_get_irq(pdev, 0);

    if (data->irq < 0) {
        pr_err("MY_TEST_DRIVER: failed to get IRQ\n");
        ret = data->irq;
        goto err_fifo;
    }

    pr_info("MY_TEST_DRIVER: IRQ number = %d\n",
            data->irq);

    /*
     * Initialize workqueue.
     */
    INIT_WORK(&data->irq_work,
              my_test_irq_work);

    /*
     * Register interrupt handler.
     */
    ret = devm_request_irq(&pdev->dev,
                           data->irq,
                           my_test_irq_handler,
                           0,
                           "my_test_sensor",
                           data);

    if (ret) {
        pr_err("MY_TEST_DRIVER: failed to request IRQ %d: %d\n",
               data->irq,
               ret);
        goto err_fifo;
    }

    pr_info("MY_TEST_DRIVER: IRQ registered successfully\n");

    /*
     * Save driver data in the platform device.
     */
    platform_set_drvdata(pdev, data);

    /*
     * Create sysfs temperature file.
     */
    ret = device_create_file(&pdev->dev,
                             &dev_attr_temperature);

    if (ret) {
        dev_err(&pdev->dev,
                "failed to create temperature: %d\n",
                ret);
        goto err_fifo;
    }

    /*
     * Create sysfs threshold file.
     */
    ret = device_create_file(&pdev->dev,
                             &dev_attr_threshold);

    if (ret) {
        dev_err(&pdev->dev,
                "failed to create threshold: %d\n",
                ret);

        device_remove_file(&pdev->dev,
                           &dev_attr_temperature);

        goto err_fifo;
    }

    /*
     * Create sysfs alarm file.
     */
    ret = device_create_file(&pdev->dev,
                             &dev_attr_alarm);

    if (ret) {
        dev_err(&pdev->dev,
                "failed to create alarm: %d\n",
                ret);

        device_remove_file(&pdev->dev,
                           &dev_attr_threshold);

        device_remove_file(&pdev->dev,
                           &dev_attr_temperature);

        goto err_fifo;
    }

    /*
     * Allocate character-device number.
     */
    ret = alloc_chrdev_region(&data->devt,
                              0,
                              1,
                              "my_test_sensor");

    if (ret) {
        dev_err(&pdev->dev,
                "failed to allocate device number: %d\n",
                ret);
        goto err_sysfs;
    }

    /*
     * Initialize cdev with our file operations.
     */
    cdev_init(&data->cdev,
              &my_test_fops);

    /*
     * Add cdev to the kernel.
     */
    ret = cdev_add(&data->cdev,
                   data->devt,
                   1);

    if (ret) {
        dev_err(&pdev->dev,
                "failed to add cdev: %d\n",
                ret);
        goto err_chrdev;
    }

    /*
     * Create device class.
     */
    data->class = class_create("my_test_sensor");

    if (IS_ERR(data->class)) {
        ret = PTR_ERR(data->class);

        dev_err(&pdev->dev,
                "failed to create class: %d\n",
                ret);

        goto err_cdev;
    }

    /*
     * Create /dev/my_test_sensor.
     */
    data->device = device_create(data->class,
                                 &pdev->dev,
                                 data->devt,
                                 NULL,
                                 "my_test_sensor");

    if (IS_ERR(data->device)) {
        ret = PTR_ERR(data->device);

        dev_err(&pdev->dev,
                "failed to create device: %d\n",
                ret);

        goto err_class;
    }

    pr_info("MY_TEST_DRIVER: character device created\n");
    pr_info("MY_TEST_DRIVER: interrupt-driven sensor started\n");

    return 0;

err_class:
    class_destroy(data->class);

err_cdev:
    cdev_del(&data->cdev);

err_chrdev:
    unregister_chrdev_region(data->devt, 1);

err_sysfs:
    device_remove_file(&pdev->dev,
                       &dev_attr_alarm);

    device_remove_file(&pdev->dev,
                       &dev_attr_threshold);

    device_remove_file(&pdev->dev,
                       &dev_attr_temperature);

err_fifo:
    kfifo_free(&data->sensor_fifo);

    return ret;
}

static void my_test_remove(struct platform_device *pdev)
{
    struct my_test_data *data =
        platform_get_drvdata(pdev);

    /*
     * Stop pending work.
     */
    cancel_work_sync(&data->irq_work);

    /*
     * Remove character device.
     */
    device_destroy(data->class,
                   data->devt);

    class_destroy(data->class);

    cdev_del(&data->cdev);

    unregister_chrdev_region(data->devt, 1);

    /*
     * Remove sysfs attributes.
     */
    device_remove_file(&pdev->dev,
                       &dev_attr_temperature);

    device_remove_file(&pdev->dev,
                       &dev_attr_threshold);

    device_remove_file(&pdev->dev,
                       &dev_attr_alarm);

    /*
     * Free the kernel FIFO.
     */
    kfifo_free(&data->sensor_fifo);

    /*
     * IRQ, MMIO and driver memory are
     * automatically released by devm.
     */

    pr_info("MY_TEST_DRIVER: removed\n");
}

static const struct of_device_id my_test_of_match[] = {
    {
        .compatible = "mert,my-fake-sensor",
    },
    { }
};

MODULE_DEVICE_TABLE(of, my_test_of_match);

static struct platform_driver my_test_driver = {
    .probe = my_test_probe,
    .remove = my_test_remove,

    .driver = {
        .name = "my_test_driver",
        .of_match_table = my_test_of_match,
    },
};

module_platform_driver(my_test_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mert");
MODULE_DESCRIPTION("My test platform driver");