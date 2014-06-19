
#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input-polldev.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/pm_runtime.h>

#define XOUT	0x00
#define	 YOUT	0X01
#define ZOUT	0x02
#define TILT	0x03
#define SRST	0x04
#define SPCNT	0x05
#define INTSU	0X06
#define MODE	0X07
#define SR		0X08
#define PDET	0X09
#define PD		0X0A

#define MMA7660_POLL_INTERVAL 10

struct mma7660_dev {
	struct i2c_client *client;
	struct input_polled_dev *ipdev;

	struct dentry *stat;

	bool shake_enable;
	bool tap_enable;
};

struct mma7660_xyz {
	s8 xout;
	s8 yout;
	s8 zout;
};

ssize_t shake_enable_show(struct device *d, struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(d);
	struct mma7660_dev *dev = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", !!dev->shake_enable);
}

ssize_t shake_enable_store(struct device *d, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(d);
	struct mma7660_dev *dev = i2c_get_clientdata(client);

	if ('1' == buf[0])
		dev->shake_enable = true;
	else if ('0' == buf[0])
		dev->shake_enable = false;
	else
		return -EINVAL;

	return count;
}

ssize_t tap_enable_show(struct device *d, struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(d);
	struct mma7660_dev *dev = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", !!dev->tap_enable);
}

ssize_t tap_enable_store(struct device *d, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(d);
	struct mma7660_dev *dev = i2c_get_clientdata(client);

	if ('1' == buf[0])
		dev->tap_enable = true;
	else if ('0' == buf[0])
		dev->tap_enable = false;
	else
		return -EINVAL;

	return count;
}

static DEVICE_ATTR_RW(shake_enable);
static DEVICE_ATTR_RW(tap_enable);

static struct attribute *mma7660_attrs[] = {
		&dev_attr_shake_enable.attr,
		&dev_attr_tap_enable.attr,
		NULL
};

static const struct attribute_group mma7660_attr_grp = {
		.attrs = mma7660_attrs
};

void mma7660_get_tilt_buf(struct mma7660_dev *dev, u8 tilt_stat, char *tilt_buf)
{
	if (dev->shake_enable) {
		if (tilt_stat & (1 << 7))
			strcpy(tilt_buf, "Experiencing shake\n");
		else
			strcpy(tilt_buf, "Not experiencing shake\n");
	} else {
		strcpy(tilt_buf, "Shake disabled\n");
	}

	if (dev->tap_enable) {
		if (tilt_stat & (1 << 5))
			strcat(tilt_buf, "Tap detected\n");
		else
			strcat(tilt_buf, "No tap detected\n");
	} else {
		strcat(tilt_buf, "Tap disabled\n");
	}

	strcat(tilt_buf, "Facing : ");
	switch (tilt_stat & 0x03) {
	case 0:
		strcat(tilt_buf, "Unknown\n");
		break;
	case 1:
		strcat(tilt_buf, "Front\n");
		break;
	case 2:
		strcat(tilt_buf, "Back\n");
		break;
	}

	switch ((tilt_stat & 0x1c) >> 2) {
	case 0:
		strcat(tilt_buf, "Unknown PoLa");
		break;
	case 1:
		strcat(tilt_buf, "Landscape-Left");
		break;
	case 2:
		strcat(tilt_buf, "Landscape-Right");
		break;
	case 5:
		strcat(tilt_buf, "Portrait-Inverted");
		break;
	case 6:
		strcat(tilt_buf, "Portrait-Normal");
		break;
	}
}

int mma7660_get_xyz(struct i2c_client *client, struct mma7660_xyz *xyz)
{
	do {
		xyz->xout = i2c_smbus_read_byte_data(client, XOUT);
		if (xyz->xout < 0) {
			dev_err(&client->dev, "Failed to read XOUT");
			return xyz->xout;
		}
	/* reg. was read while device was updating */
	} while (xyz->xout & (1 << 6));
	xyz->xout = (xyz->xout & (1 << 5)) ? (xyz->xout | (~0x3f)) :
				(xyz->xout & 0x3f);

	do {
		xyz->yout = i2c_smbus_read_byte_data(client, YOUT);
		if (xyz->yout < 0) {
			dev_err(&client->dev, "Failed to read YOUT");
			return xyz->yout;
		}
	/* reg. was read while device was updating */
	} while (xyz->yout & (1 << 6));
	xyz->yout = (xyz->yout & (1 << 5)) ? (xyz->yout | (~0x3f)) :
				(xyz->yout & 0x3f);

	do {
		xyz->zout = i2c_smbus_read_byte_data(client, ZOUT);
		if (xyz->zout < 0) {
			dev_err(&client->dev, "Failed to read ZOUT");
			return xyz->zout;
		}
	/* reg. was read while device was updating */
	} while (xyz->zout & (1 << 6));
	xyz->zout = (xyz->zout & (1 << 5)) ? (xyz->zout | (~0x3f)) :
				(xyz->zout & 0x3f);

	return 0;
}

int mma7660_get_tilt(struct i2c_client *client, u8 *tilt_stat)
{
	do {
		*tilt_stat = i2c_smbus_read_byte_data(client, TILT);
		if (*tilt_stat < 0) {
			dev_err(&client->dev, "Failed to read TILT");
			return *tilt_stat;
		}
	} while (*tilt_stat & (1 << 6));

	return 0;
}

void mma7660_poll(struct input_polled_dev *ipdev)
{

	struct mma7660_dev *dev = ipdev->private;
	struct input_dev *idev = ipdev->input;
	struct i2c_client *client = dev->client;
	struct mma7660_xyz xyz;
	u8 tilt_stat;

	if (mma7660_get_xyz(client, &xyz))
		return;

	if (mma7660_get_tilt(client, &tilt_stat))
		return;

	input_report_abs(idev, ABS_X, xyz.xout);
	input_report_abs(idev, ABS_Y, xyz.yout);
	input_report_abs(idev, ABS_Z, xyz.zout);

	/* Send only potrait_lanscape and front_back vales */
	input_report_abs(idev, ABS_MT_ORIENTATION, tilt_stat & 0x1f);

	/* Report the shake event, for values :
	 * 0 : No shake detected
	 * 1 : Shake detected
	 */
	if (dev->shake_enable)
		input_event(idev, EV_MSC, MSC_GESTURE, (tilt_stat >> 7) & 0x01);

	/* Report tap event */
	if (dev->tap_enable)
		input_report_key(idev, BTN_SELECT, (tilt_stat >> 5) & 0x01);

	input_sync(idev);
}

void mma7660_open(struct input_polled_dev *ipdev)
{
	struct mma7660_dev *dev = ipdev->private;
	struct i2c_client *client = dev->client;

	pm_runtime_get_sync(&client->dev);
}

void mma7660_close(struct input_polled_dev *ipdev)
{
	struct mma7660_dev *dev = ipdev->private;
	struct i2c_client *client = dev->client;

	pm_runtime_put_sync_suspend(&client->dev);
}

ssize_t mma7660_debug_read(struct file *filp, char __user *ubuff,
							size_t cnt, loff_t *off)
{
	int retcnt;
	u8 tilt_stat;
	char buff[128];
	char tilt_buf[128];
	struct mma7660_xyz xyz;
	struct mma7660_dev *dev = filp->private_data;
	struct i2c_client *client = dev->client;

	retcnt = mma7660_get_xyz(client, &xyz);
	if (retcnt)
		return retcnt;

	retcnt = mma7660_get_tilt(client, &tilt_stat);
	if (retcnt)
		return retcnt;

	memset(tilt_buf, 0, sizeof(tilt_buf));
	mma7660_get_tilt_buf(dev, tilt_stat, tilt_buf);

	retcnt = sprintf(buff,
			"===========================\n"
			" X : %3d\n Y : %3d\n Z : %3d\n\nTilt info :\n%s\n"
			"===========================\n",
			xyz.xout, xyz.yout, xyz.zout, tilt_buf);

	if (copy_to_user(ubuff, buff, retcnt))
		return -EFAULT;

	return cnt;
}

int mma7660_debug_open(struct inode *inode, struct file *filp)
{
	int retval;
	struct mma7660_dev *dev = filp->private_data = inode->i_private;
	struct i2c_client *client = dev->client;

	retval = pm_runtime_get_sync(&client->dev);
	if (retval < 0)
		return retval;

	return 0;
}

int mma7660_debug_close(struct inode *inode, struct file *filp)
{
	struct mma7660_dev *dev = filp->private_data;
	struct i2c_client *client = dev->client;

	return pm_runtime_put_sync_suspend(&client->dev);
}

static const struct file_operations mma7660_debug_fops = {
		.open		= mma7660_debug_open,
		.read		= mma7660_debug_read,
		.release	= mma7660_debug_close
};

int mma7660_dev_init(struct mma7660_dev *dev)
{
	int retval;
	struct i2c_client *client = dev->client;

	/* Perform all the writes(configuration), prior to pushing the
	 * device into active mode(i.e in standby mode, with MODE bit==0,
	 * as the device cannot be configured while in active mode.
	 */

	/* Enable shake detection, on all axes */
	retval = i2c_smbus_write_byte_data(client, INTSU,
				(1 << 5) | (1 << 6) | (1 << 7));
	if (retval) {
		dev_err(&client->dev, "Failed to enable shake detection\n");
		return retval;
	}

	/* 120 samples per second, with tap detection enabled */
	retval = i2c_smbus_write_byte_data(client, SR, 0x00);
	if (retval) {
		dev_err(&client->dev, "Failed to write to SR register\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(client, PDET, 0x00);
	if (retval) {
		dev_err(&client->dev, "Failed to enable tap detection\n");
		return retval;
	}

	/* Optimal vale for tap debouncing filter */
	retval = i2c_smbus_write_byte_data(client, PD, 0x1f);
	if (retval) {
		dev_err(&client->dev, "Failed to write to PD register\n");
		return retval;
	}

	dev->shake_enable = true;
	dev->tap_enable = true;

	return 0;
}

void mma7660_input_init(struct input_polled_dev *ipdev)
{
	struct input_dev *idev = ipdev->input;

	ipdev->open = mma7660_open;	/* Only for power management purposes */
	ipdev->close = mma7660_close;

	ipdev->poll = mma7660_poll;
	ipdev->poll_interval = MMA7660_POLL_INTERVAL;

	idev->name = "MMA7660";

	set_bit(EV_ABS, idev->evbit);

	set_bit(ABS_X, idev->absbit);
	set_bit(ABS_Y, idev->absbit);
	set_bit(ABS_Z, idev->absbit);

	/* Orientation event */
	set_bit(ABS_MT_ORIENTATION, idev->absbit);

	/* Misc event : Shake gesture */
	set_bit(EV_MSC, idev->evbit);
	set_bit(MSC_GESTURE	, idev->mscbit);

	/* Button event : Tap detection */
	set_bit(EV_KEY, idev->evbit);
	set_bit(BTN_SELECT, idev->keybit);

	input_alloc_absinfo(idev);
}

static struct dentry *mma7660_dir;

int mma7660_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int retval;
	struct dentry *stat;
	struct mma7660_dev *dev;
	char fname[16];

	dev_info(&client->dev, "MMA7660 device probed");

	dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&client->dev, "Failed to allocate memory for private data\n");
		return -ENOMEM;
	}
	i2c_set_clientdata(client, dev);
	dev->client = client;

	sprintf(fname, "stat_%x", client->addr);
	stat = debugfs_create_file(fname, S_IRUGO, mma7660_dir,
					dev, &mma7660_debug_fops);
	if (!stat) {
		dev_err(&client->dev, "Failed to create xyz debug file");
		retval = PTR_ERR(stat);
		goto xyz_fail;
	}
	dev->stat = stat;

	dev->ipdev = input_allocate_polled_device();
	if (!dev->ipdev) {
		dev_err(&client->dev, "Failed to allocate input device\n");
		retval = PTR_ERR(dev->ipdev);
		goto input_alloc_fail;
	}

	dev->ipdev->private = dev;
	dev->ipdev->input->dev.parent = &client->dev;
	mma7660_input_init(dev->ipdev);

	retval = input_register_polled_device(dev->ipdev);
	if (retval) {
		dev_err(&client->dev, "Failed to register with the input subsystem\n");
		goto input_reg_fail;
	}

	retval = sysfs_create_group(&client->dev.kobj, &mma7660_attr_grp);
	if (retval) {
		dev_err(&client->dev, "Failed to create sysfs attribute group\n");
		goto sysfs_grp_fail;
	}

	retval = mma7660_dev_init(dev);
	if (retval) {
		dev_err(&client->dev, "Failed to initialise MMA7660");
		goto dev_init_fail;
	}

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);

	/* After returning from here, the device's suspend function is called
	 * which pushes the device into low-power state.
	 * The device's resume will be called as and when the reference count
	 * for the device (in terms of pm) is incremented, which is achieved
	 * from the pm_runtime_get_(), called in the open() function.
	 * Conversely, device's suspend function will be called as and when the
	 * reference count of the device reaches 0, using pm_runtime_put_(),
	 * which is called in close/release functions.
	 */

	return 0;

dev_init_fail:
	sysfs_remove_group(&client->dev.kobj, &mma7660_attr_grp);
sysfs_grp_fail:
	input_unregister_polled_device(dev->ipdev);
input_reg_fail:
	input_free_polled_device(dev->ipdev);
input_alloc_fail:
	debugfs_remove(stat);
xyz_fail:
	return retval;
}

int mma7660_remove(struct i2c_client *client)
{
	struct mma7660_dev *dev;

	dev = i2c_get_clientdata(client);

	pm_runtime_disable(&client->dev);

	sysfs_remove_group(&client->dev.kobj, &mma7660_attr_grp);

	debugfs_remove(dev->stat);

	input_unregister_polled_device(dev->ipdev);
	input_free_polled_device(dev->ipdev);

	dev_info(&client->dev, "MMA7660 device removed");
	return 0;
}

int mma7660_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	return i2c_smbus_write_byte_data(client, MODE, 0x00);
}

int mma7660_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	return i2c_smbus_write_byte_data(client, MODE, 0x01);
}

static struct i2c_device_id mma7660_id[] = {
		{"mma7660", 0}
};

static UNIVERSAL_DEV_PM_OPS(mma7660_pm, mma7660_suspend, mma7660_resume, NULL);

static struct i2c_driver mma7660_driver = {
		.driver		= {
				.name	= "mma7660",
				.pm		= &mma7660_pm
		},
		.probe		= mma7660_probe,
		.remove		= mma7660_remove,
		.id_table	= mma7660_id,
};

static int __init mma7660_init(void)
{
	int retval;

	mma7660_dir = debugfs_create_dir("mma7660", NULL);
	if (!mma7660_dir) {
		pr_info("Failed to create debugfs dir");
		retval = PTR_ERR(mma7660_dir);
		goto debugfs_fail;
	}

	retval = i2c_add_driver(&mma7660_driver);
	if (retval) {
		pr_err("Failed to add the i2c driver\n");
		goto i2c_fail;
	}

	return 0;

i2c_fail:
	debugfs_remove_recursive(mma7660_dir);
debugfs_fail:
	return retval;
}

static void __exit mma7660_exit(void)
{
	i2c_del_driver(&mma7660_driver);

	debugfs_remove_recursive(mma7660_dir);
}

module_init(mma7660_init);
module_exit(mma7660_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("A Raghavendra Rao <arrao@cdac.in>");



