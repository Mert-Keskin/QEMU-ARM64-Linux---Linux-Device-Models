#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/errno.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>

#define SENSOR_REG_TEMPERATURE 0x00

struct my_test_data {
    void __iomem *base;
    struct delayed_work work;
};

static void my_test_work(struct work_struct *work)
{
    struct my_test_data *data =
        container_of(to_delayed_work(work),
                     struct my_test_data,
                     work);

    u32 temperature;

    temperature = readl(data->base + SENSOR_REG_TEMPERATURE);

    pr_info("MY_TEST_DRIVER: periodic temperature = %u\n",
            temperature);

    /*
     * Run this work again after 2 seconds.
     */
    schedule_delayed_work(&data->work, msecs_to_jiffies(2000));
}

static int my_test_probe(struct platform_device *pdev)
{
    struct resource *res;
    struct my_test_data *data;

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
     * Get MMIO resource from Device Tree.
     */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    if (!res) {
        pr_err("MY_TEST_DRIVER: failed to get MMIO resource\n");
        return -ENODEV;
    }

    pr_info("MY_TEST_DRIVER: resource start = 0x%llx\n",
            (unsigned long long)res->start);

    pr_info("MY_TEST_DRIVER: resource end = 0x%llx\n",
            (unsigned long long)res->end);

    /*
     * Map the hardware registers.
     */
    data->base = devm_ioremap_resource(&pdev->dev, res);

    if (IS_ERR(data->base)) {
        pr_err("MY_TEST_DRIVER: ioremap failed\n");
        return PTR_ERR(data->base);
    }

    pr_info("MY_TEST_DRIVER: MMIO mapped successfully!\n");

    /*
     * Initialize delayed work.
     */
    INIT_DELAYED_WORK(&data->work, my_test_work);

    /*
     * Start the first reading after 2 seconds.
     */
    schedule_delayed_work(&data->work,
                          msecs_to_jiffies(2000));

    /*
     * Save driver data in the platform device.
     */
    platform_set_drvdata(pdev, data);

    pr_info("MY_TEST_DRIVER: periodic sensor reading started\n");

    return 0;
}

static void my_test_remove(struct platform_device *pdev)
{
    struct my_test_data *data = platform_get_drvdata(pdev);

    /*
     * Stop any scheduled work before removing the driver.
     */
    cancel_delayed_work_sync(&data->work);

    pr_info("MY_TEST_DRIVER: removed\n");

}

static const struct of_device_id my_test_of_match[] = {
    {
        .compatible = "mert,my-test-device",
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
