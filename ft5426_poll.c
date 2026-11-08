// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 x3r0
 *
 * Polled multi-touch driver for FocalTech FT5426 panels without an IRQ line
 * (Raspberry Pi 7" DSI clones on Radxa boards). Replaces the vendor
 * raspits_ft5426 driver, which exposes one slot and no INPUT_PROP_DIRECT.
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/of.h>

#define FT_MAX_POINTS	5
#define FT_POINT_LEN	6
#define FT_TD_STATUS	0x02
#define FT_FW_VER	0xA6
#define FT_ID_LIMIT	10	/* slots, finger ids 0..9 */

static unsigned int width = 800, height = 480, poll_ms = 16;
static bool invert = true;
module_param(width, uint, 0444);
module_param(height, uint, 0444);
module_param(poll_ms, uint, 0444);
module_param(invert, bool, 0644);
MODULE_PARM_DESC(invert, "Rotate 180 degrees (vendor panel default)");

struct ft_data {
	struct i2c_client *client;
	struct input_dev *input;
};

static void ft_poll(struct input_dev *input)
{
	struct ft_data *ft = input_get_drvdata(input);
	u8 reg = FT_TD_STATUS;
	u8 buf[1 + FT_MAX_POINTS * FT_POINT_LEN];
	struct i2c_msg msg[2] = {
		{ .addr = ft->client->addr, .len = 1, .buf = &reg },
		{ .addr = ft->client->addr, .flags = I2C_M_RD, .len = sizeof(buf), .buf = buf },
	};
	int i, n;

	if (i2c_transfer(ft->client->adapter, msg, 2) != 2)
		return;
	n = buf[0] & 0x0f;
	if (n > FT_MAX_POINTS)
		return;	/* garbage frame; keep previous state */
	for (i = 0; i < n; i++) {
		u8 *p = &buf[1 + i * FT_POINT_LEN];
		unsigned int ev = p[0] >> 6, id = p[2] >> 4;
		unsigned int x = ((p[0] & 0x0f) << 8) | p[1];
		unsigned int y = ((p[2] & 0x0f) << 8) | p[3];

		if (ev == 1 || id >= FT_ID_LIMIT)	/* 1 = lift-up */
			continue;
		if (x >= width || y >= height)
			continue;
		if (invert) {
			x = width - 1 - x;
			y = height - 1 - y;
		}
		input_mt_slot(input, id);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
		input_report_abs(input, ABS_MT_POSITION_X, x);
		input_report_abs(input, ABS_MT_POSITION_Y, y);
	}
	input_mt_sync_frame(input);	/* releases slots not reported above */
	input_sync(input);
}

static int ft_probe(struct i2c_client *client)
{
	struct ft_data *ft;
	struct input_dev *input;
	int ver, err;

	ver = i2c_smbus_read_byte_data(client, FT_FW_VER);
	if (ver < 0)
		return dev_err_probe(&client->dev, ver, "no FT5426 at 0x%02x\n", client->addr);

	ft = devm_kzalloc(&client->dev, sizeof(*ft), GFP_KERNEL);
	input = devm_input_allocate_device(&client->dev);
	if (!ft || !input)
		return -ENOMEM;
	ft->client = client;
	ft->input = input;
	input->name = "FT5426 touchscreen";
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, width - 1, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, height - 1, 0, 0);
	err = input_mt_init_slots(input, FT_ID_LIMIT, INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (err)
		return err;
	input_set_drvdata(input, ft);
	err = input_setup_polling(input, ft_poll);
	if (err)
		return err;
	input_set_poll_interval(input, poll_ms);
	err = input_register_device(input);
	if (err)
		return err;
	dev_info(&client->dev, "FT5426 fw 0x%02x, %u points, polled every %u ms\n",
		 ver, FT_MAX_POINTS, poll_ms);
	return 0;
}

static const struct of_device_id ft_of_match[] = {
	{ .compatible = "focaltech,ft5426-poll" },
	{ }
};
MODULE_DEVICE_TABLE(of, ft_of_match);

static const struct i2c_device_id ft_id[] = {
	{ "ft5426-poll", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ft_id);

static struct i2c_driver ft_driver = {
	.driver = { .name = "ft5426_poll", .of_match_table = ft_of_match },
	.probe_new = ft_probe,
	.id_table = ft_id,
};
module_i2c_driver(ft_driver);

MODULE_DESCRIPTION("Polled FT5426 multi-touch for RPi 7in DSI clones");
MODULE_LICENSE("GPL");
