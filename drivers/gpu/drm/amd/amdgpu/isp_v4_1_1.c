/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#include <linux/gpio/machine.h>
#include "amdgpu.h"
#include "isp_v4_1_1.h"

static const unsigned int isp_4_1_1_int_srcid[MAX_ISP411_INT_SRC] = {
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT9,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT10,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT11,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT12,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT13,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT14,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT15,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT16
};

static struct gpiod_lookup_table isp_gpio_table = {
	.dev_id = "amd_isp_capture",
	.table = {
		GPIO_LOOKUP("AMDI0030:00", 85, "enable_isp", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct gpiod_lookup_table isp_sensor_gpio_table = {
	.dev_id = "i2c-ov05c10",
	.table = {
		GPIO_LOOKUP("amdisp-pinctrl", 0, "enable", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static int isp_v4_1_1_hw_init(struct amdgpu_isp *isp)
{
	struct amdgpu_device *adev = isp->adev;
	int idx, int_idx, num_res, r;
	u8 isp_dev_hid[ACPI_ID_LEN];
	u64 isp_base;

	if (adev->rmmio_size == 0 || adev->rmmio_size < 0x5289)
		return -EINVAL;

	r = amdgpu_acpi_get_isp4_dev_hid(&isp_dev_hid);
	if (r) {
		drm_dbg(&adev->ddev, "Invalid isp platform detected (%d)", r);
		/* allow GPU init to progress */
		return 0;
	}

	/* add GPIO resources required for OMNI5C10 sensor */
	if (!strcmp("OMNI5C10", isp_dev_hid)) {
		gpiod_add_lookup_table(&isp_gpio_table);
		gpiod_add_lookup_table(&isp_sensor_gpio_table);
	}

	isp_base = adev->rmmio_base;

	isp->isp_cell = kcalloc(3, sizeof(struct mfd_cell), GFP_KERNEL);
	if (!isp->isp_cell) {
		r = -ENOMEM;
		drm_err(&adev->ddev,
			"%s: isp mfd cell alloc failed\n", __func__);
		goto failure;
	}

	num_res = MAX_ISP411_MEM_RES + MAX_ISP411_INT_SRC;

	isp->isp_res = kcalloc(num_res, sizeof(struct resource),
			       GFP_KERNEL);
	if (!isp->isp_res) {
		r = -ENOMEM;
		drm_err(&adev->ddev,
			"%s: isp mfd res alloc failed\n", __func__);
		goto failure;
	}

	isp->isp_pdata = kzalloc(sizeof(*isp->isp_pdata), GFP_KERNEL);
	if (!isp->isp_pdata) {
		r = -ENOMEM;
		drm_err(&adev->ddev,
			"%s: isp platform data alloc failed\n", __func__);
		goto failure;
	}

	/* initialize isp platform data */
	isp->isp_pdata->adev = (void *)adev;
	isp->isp_pdata->asic_type = adev->asic_type;
	isp->isp_pdata->base_rmmio_size = adev->rmmio_size;

	isp->isp_res[0].name = "isp_4_1_1_reg";
	isp->isp_res[0].flags = IORESOURCE_MEM;
	isp->isp_res[0].start = isp_base;
	isp->isp_res[0].end = isp_base + ISP_REGS_OFFSET_END;

	isp->isp_res[1].name = "isp_4_1_1_phy0_reg";
	isp->isp_res[1].flags = IORESOURCE_MEM;
	isp->isp_res[1].start = isp_base + ISP411_PHY0_OFFSET;
	isp->isp_res[1].end = isp_base + ISP411_PHY0_OFFSET + ISP411_PHY0_SIZE;

	for (idx = MAX_ISP411_MEM_RES, int_idx = 0; idx < num_res; idx++, int_idx++) {
		isp->isp_res[idx].name = "isp_4_1_1_irq";
		isp->isp_res[idx].flags = IORESOURCE_IRQ;
		isp->isp_res[idx].start =
			amdgpu_irq_create_mapping(adev, isp_4_1_1_int_srcid[int_idx]);
		isp->isp_res[idx].end =
			isp->isp_res[idx].start;
	}

	isp->isp_cell[0].name = "amd_isp_capture";
	isp->isp_cell[0].num_resources = num_res;
	isp->isp_cell[0].resources = &isp->isp_res[0];
	isp->isp_cell[0].platform_data = isp->isp_pdata;
	isp->isp_cell[0].pdata_size = sizeof(struct isp_platform_data);

	/* initialize isp i2c platform data */
	isp->isp_i2c_res = kcalloc(1, sizeof(struct resource), GFP_KERNEL);
	if (!isp->isp_i2c_res) {
		r = -ENOMEM;
		drm_err(&adev->ddev,
			"%s: isp mfd res alloc failed\n", __func__);
		goto failure;
	}

	isp->isp_i2c_res[0].name = "isp_i2c0_reg";
	isp->isp_i2c_res[0].flags = IORESOURCE_MEM;
	isp->isp_i2c_res[0].start = isp_base + ISP411_I2C0_OFFSET;
	isp->isp_i2c_res[0].end = isp_base + ISP411_I2C0_OFFSET + ISP411_I2C0_SIZE;

	isp->isp_cell[1].name = "amd_isp_i2c_designware";
	isp->isp_cell[1].num_resources = 1;
	isp->isp_cell[1].resources = &isp->isp_i2c_res[0];
	isp->isp_cell[1].platform_data = isp->isp_pdata;
	isp->isp_cell[1].pdata_size = sizeof(struct isp_platform_data);

	/* initialize isp gpiochip platform data */
	isp->isp_gpio_res = kcalloc(1, sizeof(struct resource), GFP_KERNEL);
	if (!isp->isp_gpio_res) {
		r = -ENOMEM;
		drm_err(&adev->ddev,
			"%s: isp gpio res alloc failed\n", __func__);
		goto failure;
	}

	isp->isp_gpio_res[0].name = "isp_gpio_reg";
	isp->isp_gpio_res[0].flags = IORESOURCE_MEM;
	isp->isp_gpio_res[0].start = isp_base + ISP411_GPIO_SENSOR_OFFSET;
	isp->isp_gpio_res[0].end = isp_base + ISP411_GPIO_SENSOR_OFFSET +
				   ISP411_GPIO_SENSOR_SIZE;

	isp->isp_cell[2].name = "amdisp-pinctrl";
	isp->isp_cell[2].num_resources = 1;
	isp->isp_cell[2].resources = &isp->isp_gpio_res[0];
	isp->isp_cell[2].platform_data = isp->isp_pdata;
	isp->isp_cell[2].pdata_size = sizeof(struct isp_platform_data);

	r = mfd_add_hotplug_devices(isp->parent, isp->isp_cell, 3);
	if (r) {
		drm_err(&adev->ddev,
			"%s: add mfd hotplug device failed\n", __func__);
		goto failure;
	}

	return 0;

failure:

	kfree(isp->isp_pdata);
	kfree(isp->isp_res);
	kfree(isp->isp_cell);
	kfree(isp->isp_i2c_res);
	kfree(isp->isp_gpio_res);

	return r;
}

static int isp_v4_1_1_hw_fini(struct amdgpu_isp *isp)
{
	mfd_remove_devices(isp->parent);

	kfree(isp->isp_res);
	kfree(isp->isp_cell);
	kfree(isp->isp_pdata);
	kfree(isp->isp_i2c_res);
	kfree(isp->isp_gpio_res);

	return 0;
}

static const struct isp_funcs isp_v4_1_1_funcs = {
	.hw_init = isp_v4_1_1_hw_init,
	.hw_fini = isp_v4_1_1_hw_fini,
};

void isp_v4_1_1_set_isp_funcs(struct amdgpu_isp *isp)
{
	isp->funcs = &isp_v4_1_1_funcs;
}
