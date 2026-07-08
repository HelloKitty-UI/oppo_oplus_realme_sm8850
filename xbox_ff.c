/*
 * xbox_ff.c - Xbox Bluetooth Controller Force Feedback Driver
 *
 * Compiles as a standalone kernel module for Android GKI 6.12.
 * Does NOT conflict with the built-in hid-microsoft driver.
 * Registers as an independent HID driver for Xbox Bluetooth controllers
 * and adds FF_RUMBLE support that hid-microsoft lacks for these devices.
 *
 * Part of oppo_oplus_realme_sm8850 kernel project.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/slab.h>

MODULE_AUTHOR("xbox-ff");
MODULE_DESCRIPTION("Xbox Bluetooth Controller Force Feedback for GKI 6.12");
MODULE_LICENSE("GPL");

/* HID output report for Xbox Bluetooth rumble */
struct xbox_rumble_report {
	u8 report_id;
	u8 flags;
	u8 reserved1;
	u8 reserved2;
	u8 reserved3;
	u8 left_motor;
	u8 right_motor;
	u8 left_trigger;
	u8 right_trigger;
	u8 duration;
} __packed;

static const struct hid_device_id xbox_ff_devices[] = {
	{ HID_BLUETOOTH_DEVICE(0x045E, 0x0B13) }, /* Xbox Wireless Controller */
	{ HID_BLUETOOTH_DEVICE(0x045E, 0x0B05) }, /* Xbox One S */
	{ HID_BLUETOOTH_DEVICE(0x045E, 0x0B20) }, /* Xbox Elite Series 2 */
	{ HID_BLUETOOTH_DEVICE(0x045E, 0x0B22) }, /* Xbox Series X|S */
	{ HID_BLUETOOTH_DEVICE(0x045E, 0x02E0) }, /* Xbox One S (older) */
	{ HID_BLUETOOTH_DEVICE(0x045E, 0x02FD) }, /* Xbox One S (older) */
	{ }
};
MODULE_DEVICE_TABLE(hid, xbox_ff_devices);

static int xbox_ff_play(struct input_dev *dev, void *data,
			struct ff_effect *effect)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct xbox_rumble_report rpt = {0};
	u8 strong = 0, weak = 0;

	if (effect) {
		switch (effect->type) {
		case FF_RUMBLE:
			strong = (u8)(effect->u.rumble.strong_magnitude >> 8);
			weak   = (u8)(effect->u.rumble.weak_magnitude >> 8);
			break;
		case FF_CONSTANT: {
			s16 lvl = effect->u.constant.level;
			strong = weak = (u8)((lvl > 0 ? lvl : -lvl) >> 7);
			break;
		}
		}
	}

	rpt.report_id   = 0x03;
	rpt.flags       = 0x0F;
	rpt.left_motor  = strong;
	rpt.right_motor = weak;

	if (hid_hw_output_report(hdev, (u8 *)&rpt, sizeof(rpt)) < 0)
		dev_err(&hdev->dev, "xbox_ff: failed to send rumble report\n");

	return 0;
}

static int xbox_ff_probe(struct hid_device *hdev,
			 const struct hid_device_id *id)
{
	struct input_dev *idev;
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "xbox_ff: hid_parse failed: %d\n", ret);
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "xbox_ff: hid_hw_start failed: %d\n", ret);
		return ret;
	}

	idev = hdev->dev_rdesc->application->input;
	if (!idev) {
		hid_err(hdev, "xbox_ff: no input device\n");
		hid_hw_stop(hdev);
		return -ENODEV;
	}

	input_set_drvdata(idev, hdev);

	/* Register FF rumble support */
	input_set_capability(idev, EV_FF, FF_RUMBLE);
	input_set_capability(idev, EV_FF, FF_CONSTANT);

	ret = input_ff_create_memless(idev, NULL, xbox_ff_play);
	if (ret) {
		hid_err(hdev, "xbox_ff: input_ff_create_memless failed: %d\n", ret);
		hid_hw_stop(hdev);
		return ret;
	}

	hid_info(hdev, "xbox_ff: FF_RUMBLE enabled for %s\n",
		 dev_name(&hdev->dev));
	return 0;
}

static void xbox_ff_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
}

static struct hid_driver xbox_ff_driver = {
	.name    = "xbox_ff",
	.id_table = xbox_ff_devices,
	.probe   = xbox_ff_probe,
	.remove  = xbox_ff_remove,
};

/*
 * On module load, walk all existing HID devices and rebind matching ones
 * from the built-in hid-microsoft to our driver.
 */
static int xbox_ff_rebind_existing(void)
{
	struct hid_device *hdev;
	int count = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
	hid_for_each_dev(hdev) {
		if (hid_match_device(hdev, &xbox_ff_driver)) {
			hid_info(hdev, "xbox_ff: rebinding %s from built-in driver\n",
				 dev_name(&hdev->dev));
			device_release_driver(&hdev->dev);
			device_attach(&hdev->dev);
			count++;
		}
	}
#endif
	return count;
}

static int __init xbox_ff_init(void)
{
	int ret;

	ret = __hid_register_driver(&xbox_ff_driver, THIS_MODULE, "xbox_ff");
	if (ret) {
		pr_err("xbox_ff: failed to register HID driver: %d\n", ret);
		return ret;
	}

	xbox_ff_rebind_existing();
	pr_info("xbox_ff: driver loaded\n");
	return 0;
}

static void __exit xbox_ff_exit(void)
{
	hid_unregister_driver(&xbox_ff_driver);
	pr_info("xbox_ff: driver unloaded\n");
}

module_init(xbox_ff_init);
module_exit(xbox_ff_exit);
