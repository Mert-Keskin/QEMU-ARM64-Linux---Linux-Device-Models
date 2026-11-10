//manuel error handling
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

#define SENSOR_REG_TEMPERATURE 0x00
#define SENSOR_REG_IRQ_CLEAR   0x0C

struct my_test_data {
    void __iomem *base;
    struct work_struct irq_work;
    int irq;

    u32 temperature;
    u32 threshold;
    bool alarm;

    struct mutex lock;
};

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

    data->temperature =
        readl(data->base + SENSOR_REG_TEMPERATURE);

    temperature = data->temperature;
    threshold = data->threshold;

    if (temperature >= threshold)
        data->alarm = true;
    else
        data->alarm = false;

    alarm = data->alarm;

    mutex_unlock(&data->lock);

    pr_info("MY_TEST_DRIVER: temperature = %u, threshold = %u, alarm = %s\n",
            temperature,
            threshold,
            alarm ? "ON" : "OFF");
}

static irqreturn_t my_test_irq_handler(int irq, void *dev_id)
{
    struct my_test_data *data = dev_id;

    pr_info("MY_TEST_DRIVER: IRQ received! irq=%d\n",
            irq);

    writel(1, data->base + SENSOR_REG_IRQ_CLEAR);

    pr_info("MY_TEST_DRIVER: IRQ acknowledged\n");

    schedule_work(&data->irq_work);

    pr_info("MY_TEST_DRIVER: IRQ work scheduled\n");

    return IRQ_HANDLED;
}

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

static int my_test_probe(struct platform_device *pdev)
{
    struct resource *res;
    struct my_test_data *data;
    int ret;

    pr_info("MY_TEST_DRIVER: probe() called!\n");

    data = kzalloc(sizeof(*data), GFP_KERNEL);

    if (!data)
        return -ENOMEM;

    mutex_init(&data->lock);

    data->threshold = 80;

    /*
     * Get MMIO resource.
     */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    if (!res) {
        pr_err("MY_TEST_DRIVER: failed to get MMIO resource\n");
        ret = -ENODEV;
        goto err_free_data;
    }

    pr_info("MY_TEST_DRIVER: resource start = 0x%llx\n",
            (unsigned long long)res->start);

    pr_info("MY_TEST_DRIVER: resource end = 0x%llx\n",
            (unsigned long long)res->end);

    /*
     * Map MMIO.
     */
    data->base = ioremap(res->start,
                         resource_size(res));

    if (!data->base) {
        pr_err("MY_TEST_DRIVER: ioremap failed\n");
        ret = -ENOMEM;
        goto err_free_data;
    }

    pr_info("MY_TEST_DRIVER: MMIO mapped successfully!\n");

    /*
     * Get IRQ.
     */
    data->irq = platform_get_irq(pdev, 0);

    if (data->irq < 0) {
        pr_err("MY_TEST_DRIVER: failed to get IRQ\n");
        ret = data->irq;
        goto err_iounmap;
    }

    pr_info("MY_TEST_DRIVER: IRQ number = %d\n",
            data->irq);

    /*
     * Initialize workqueue.
     */
    INIT_WORK(&data->irq_work, my_test_irq_work);

    /*
     * Register IRQ.
     */
    ret = request_irq(data->irq,
                      my_test_irq_handler,
                      0,
                      "my_test_sensor",
                      data);

    if (ret) {
        pr_err("MY_TEST_DRIVER: failed to request IRQ %d: %d\n",
               data->irq,
               ret);
        goto err_iounmap;
    }

    pr_info("MY_TEST_DRIVER: IRQ registered successfully\n");

    platform_set_drvdata(pdev, data);

    /*
     * Create temperature sysfs attribute.
     */
    ret = device_create_file(&pdev->dev,
                             &dev_attr_temperature);

    if (ret) {
        dev_err(&pdev->dev,
                "failed to create temperature: %d\n",
                ret);
        goto err_free_irq;
    }

    /*
     * Create threshold sysfs attribute.
     */
    ret = device_create_file(&pdev->dev,
                             &dev_attr_threshold);

    if (ret) {
        dev_err(&pdev->dev,
                "failed to create threshold: %d\n",
                ret);
        goto err_remove_temperature;
    }

    /*
     * Create alarm sysfs attribute.
     */
    ret = device_create_file(&pdev->dev,
                             &dev_attr_alarm);

    if (ret) {
        dev_err(&pdev->dev,
                "failed to create alarm: %d\n",
                ret);
        goto err_remove_threshold;
    }

    pr_info("MY_TEST_DRIVER: interrupt-driven sensor started\n");

    return 0;


/*
 * Error handling.
 *
 * Cleanup happens in reverse order of acquisition.
 */

err_remove_threshold:
    device_remove_file(&pdev->dev,
                       &dev_attr_threshold);

err_remove_temperature:
    device_remove_file(&pdev->dev,
                       &dev_attr_temperature);

err_free_irq:
    free_irq(data->irq, data);

err_iounmap:
    iounmap(data->base);

err_free_data:
    kfree(data);

    return ret;
}

static void my_test_remove(struct platform_device *pdev)
{
    struct my_test_data *data =
        platform_get_drvdata(pdev);

    /*
     * Stop work before destroying resources it may use.
     */
    cancel_work_sync(&data->irq_work);

    /*
     * Remove sysfs attributes.
     */
    device_remove_file(&pdev->dev,
                       &dev_attr_alarm);

    device_remove_file(&pdev->dev,
                       &dev_attr_threshold);

    device_remove_file(&pdev->dev,
                       &dev_attr_temperature);

    /*
     * Free IRQ.
     */
    free_irq(data->irq, data);

    /*
     * Unmap MMIO.
     */
    iounmap(data->base);

    /*
     * Free driver data.
     */
    kfree(data);

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