// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPI-DSI based s6e3ha2 AMOLED 5.7 inch panel driver.
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 * Donghwa Lee <dh09.lee@samsung.com>
 * Hyungwon Hwang <human.hwang@samsung.com>
 * Hoegeun Kwon <hoegeun.kwon@samsung.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define S6E3HA2_MIN_BRIGHTNESS		0
#define S6E3HA2_MAX_BRIGHTNESS		100
#define S6E3HA2_DEFAULT_BRIGHTNESS	80

#define S6E3HA2_NUM_GAMMA_STEPS		46
#define S6E3HA2_GAMMA_CMD_CNT		35
#define S6E3HA2_VINT_STATUS_MAX		10

static const u8 gamma_tbl[S6E3HA2_NUM_GAMMA_STEPS][S6E3HA2_GAMMA_CMD_CNT] = {
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x89, 0x87, 0x87, 0x82, 0x83,
	  0x85, 0x88, 0x8b, 0x8b, 0x84, 0x88, 0x82, 0x82, 0x89, 0x86, 0x8c,
	  0x94, 0x84, 0xb1, 0xaf, 0x8e, 0xcf, 0xad, 0xc9, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x89, 0x87, 0x87, 0x84, 0x84,
	  0x85, 0x87, 0x8b, 0x8a, 0x84, 0x88, 0x82, 0x82, 0x89, 0x86, 0x8a,
	  0x93, 0x84, 0xb0, 0xae, 0x8e, 0xc9, 0xa8, 0xc5, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x89, 0x87, 0x87, 0x83, 0x83,
	  0x85, 0x86, 0x8a, 0x8a, 0x84, 0x88, 0x81, 0x84, 0x8a, 0x88, 0x8a,
	  0x91, 0x84, 0xb1, 0xae, 0x8b, 0xd5, 0xb2, 0xcc, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x89, 0x87, 0x87, 0x83, 0x83,
	  0x85, 0x86, 0x8a, 0x8a, 0x84, 0x87, 0x81, 0x84, 0x8a, 0x87, 0x8a,
	  0x91, 0x85, 0xae, 0xac, 0x8a, 0xc3, 0xa3, 0xc0, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x88, 0x86, 0x87, 0x85, 0x85,
	  0x86, 0x85, 0x88, 0x89, 0x84, 0x89, 0x82, 0x84, 0x87, 0x85, 0x8b,
	  0x91, 0x88, 0xad, 0xab, 0x8a, 0xb7, 0x9b, 0xb6, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x89, 0x87, 0x87, 0x83, 0x83,
	  0x85, 0x86, 0x89, 0x8a, 0x84, 0x89, 0x83, 0x83, 0x86, 0x84, 0x8b,
	  0x90, 0x84, 0xb0, 0xae, 0x8b, 0xce, 0xad, 0xc8, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x89, 0x87, 0x87, 0x83, 0x83,
	  0x85, 0x87, 0x89, 0x8a, 0x83, 0x87, 0x82, 0x85, 0x88, 0x87, 0x89,
	  0x8f, 0x84, 0xac, 0xaa, 0x89, 0xb1, 0x98, 0xaf, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x89, 0x87, 0x87, 0x83, 0x83,
	  0x85, 0x86, 0x88, 0x89, 0x84, 0x88, 0x83, 0x82, 0x85, 0x84, 0x8c,
	  0x91, 0x86, 0xac, 0xaa, 0x89, 0xc2, 0xa5, 0xbd, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x88, 0x86, 0x87, 0x84, 0x84,
	  0x85, 0x87, 0x89, 0x8a, 0x83, 0x87, 0x82, 0x85, 0x88, 0x87, 0x88,
	  0x8b, 0x82, 0xad, 0xaa, 0x8a, 0xc2, 0xa5, 0xbd, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x89, 0x87, 0x87, 0x83, 0x83,
	  0x85, 0x86, 0x87, 0x89, 0x84, 0x88, 0x83, 0x82, 0x85, 0x84, 0x8a,
	  0x8e, 0x84, 0xae, 0xac, 0x89, 0xda, 0xb7, 0xd0, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x88, 0x86, 0x87, 0x84, 0x84,
	  0x85, 0x86, 0x87, 0x89, 0x84, 0x88, 0x83, 0x80, 0x83, 0x82, 0x8b,
	  0x8e, 0x85, 0xac, 0xaa, 0x89, 0xc8, 0xaa, 0xc1, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x88, 0x86, 0x87, 0x84, 0x84,
	  0x85, 0x86, 0x87, 0x89, 0x81, 0x85, 0x81, 0x84, 0x86, 0x84, 0x8c,
	  0x8c, 0x84, 0xa9, 0xa8, 0x87, 0xa3, 0x92, 0xa1, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x88, 0x86, 0x87, 0x84, 0x84,
	  0x85, 0x86, 0x87, 0x89, 0x84, 0x86, 0x83, 0x80, 0x83, 0x81, 0x8c,
	  0x8d, 0x84, 0xaa, 0xaa, 0x89, 0xce, 0xaf, 0xc5, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x88, 0x86, 0x87, 0x84, 0x84,
	  0x85, 0x86, 0x87, 0x89, 0x81, 0x83, 0x80, 0x83, 0x85, 0x85, 0x8c,
	  0x8c, 0x84, 0xa8, 0xa8, 0x88, 0xb5, 0x9f, 0xb0, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x88, 0x86, 0x87, 0x84, 0x84,
	  0x86, 0x86, 0x87, 0x88, 0x81, 0x83, 0x80, 0x83, 0x85, 0x85, 0x8c,
	  0x8b, 0x84, 0xab, 0xa8, 0x86, 0xd4, 0xb4, 0xc9, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x88, 0x86, 0x87, 0x84, 0x84,
	  0x86, 0x86, 0x87, 0x88, 0x81, 0x83, 0x80, 0x84, 0x84, 0x85, 0x8b,
	  0x8a, 0x83, 0xa6, 0xa5, 0x84, 0xbb, 0xa4, 0xb3, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x88, 0x86, 0x87, 0x84, 0x84,
	  0x86, 0x85, 0x86, 0x86, 0x82, 0x85, 0x81, 0x82, 0x83, 0x84, 0x8e,
	  0x8b, 0x83, 0xa4, 0xa3, 0x8a, 0xa1, 0x93, 0x9d, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x88, 0x86, 0x87, 0x83, 0x83,
	  0x85, 0x86, 0x87, 0x87, 0x82, 0x85, 0x81, 0x82, 0x82, 0x84, 0x8e,
	  0x8b, 0x83, 0xa4, 0xa2, 0x86, 0xc1, 0xa9, 0xb7, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x88, 0x86, 0x87, 0x83, 0x83,
	  0x85, 0x86, 0x87, 0x87, 0x82, 0x85, 0x81, 0x82, 0x82, 0x84, 0x8d,
	  0x89, 0x82, 0xa2, 0xa1, 0x84, 0xa7, 0x98, 0xa1, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xb8, 0x00, 0xc3, 0x00, 0xb1, 0x88, 0x86, 0x87, 0x83, 0x83,
	  0x85, 0x86, 0x87, 0x87, 0x82, 0x85, 0x81, 0x83, 0x83, 0x85, 0x8c,
	  0x87, 0x7f, 0xa2, 0x9d, 0x88, 0x8d, 0x88, 0x8b, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xbb, 0x00, 0xc5, 0x00, 0xb4, 0x87, 0x86, 0x86, 0x84, 0x83,
	  0x86, 0x87, 0x87, 0x87, 0x80, 0x82, 0x7f, 0x86, 0x86, 0x88, 0x8a,
	  0x84, 0x7e, 0x9d, 0x9c, 0x82, 0x8d, 0x88, 0x8b, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xbd, 0x00, 0xc7, 0x00, 0xb7, 0x87, 0x85, 0x85, 0x84, 0x83,
	  0x86, 0x86, 0x86, 0x88, 0x81, 0x83, 0x80, 0x83, 0x84, 0x85, 0x8a,
	  0x85, 0x7e, 0x9c, 0x9b, 0x85, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xc0, 0x00, 0xca, 0x00, 0xbb, 0x87, 0x86, 0x85, 0x83, 0x83,
	  0x85, 0x86, 0x86, 0x88, 0x81, 0x83, 0x80, 0x84, 0x85, 0x86, 0x89,
	  0x83, 0x7d, 0x9c, 0x99, 0x87, 0x7b, 0x7b, 0x7c, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xc4, 0x00, 0xcd, 0x00, 0xbe, 0x87, 0x86, 0x85, 0x83, 0x83,
	  0x86, 0x85, 0x85, 0x87, 0x81, 0x82, 0x80, 0x82, 0x82, 0x83, 0x8a,
	  0x85, 0x7f, 0x9f, 0x9b, 0x86, 0xb4, 0xa1, 0xac, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xc7, 0x00, 0xd0, 0x00, 0xc2, 0x87, 0x85, 0x85, 0x83, 0x82,
	  0x85, 0x85, 0x85, 0x86, 0x82, 0x83, 0x80, 0x82, 0x82, 0x84, 0x87,
	  0x86, 0x80, 0x9e, 0x9a, 0x87, 0xa7, 0x98, 0xa1, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xca, 0x00, 0xd2, 0x00, 0xc5, 0x87, 0x85, 0x84, 0x82, 0x82,
	  0x84, 0x85, 0x85, 0x86, 0x81, 0x82, 0x7f, 0x82, 0x82, 0x84, 0x88,
	  0x86, 0x81, 0x9d, 0x98, 0x86, 0x8d, 0x88, 0x8b, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xce, 0x00, 0xd6, 0x00, 0xca, 0x86, 0x85, 0x84, 0x83, 0x83,
	  0x85, 0x84, 0x84, 0x85, 0x81, 0x82, 0x80, 0x81, 0x81, 0x82, 0x89,
	  0x86, 0x81, 0x9c, 0x97, 0x86, 0xa7, 0x98, 0xa1, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xd1, 0x00, 0xd9, 0x00, 0xce, 0x86, 0x84, 0x83, 0x83, 0x82,
	  0x85, 0x85, 0x85, 0x86, 0x81, 0x83, 0x81, 0x82, 0x82, 0x83, 0x86,
	  0x83, 0x7f, 0x99, 0x95, 0x86, 0xbb, 0xa4, 0xb3, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xd4, 0x00, 0xdb, 0x00, 0xd1, 0x86, 0x85, 0x83, 0x83, 0x82,
	  0x85, 0x84, 0x84, 0x85, 0x80, 0x83, 0x82, 0x80, 0x80, 0x81, 0x87,
	  0x84, 0x81, 0x98, 0x93, 0x85, 0xae, 0x9c, 0xa8, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xd8, 0x00, 0xde, 0x00, 0xd6, 0x86, 0x84, 0x83, 0x81, 0x81,
	  0x83, 0x85, 0x85, 0x85, 0x82, 0x83, 0x81, 0x81, 0x81, 0x83, 0x86,
	  0x84, 0x80, 0x98, 0x91, 0x85, 0x7b, 0x7b, 0x7c, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xdc, 0x00, 0xe2, 0x00, 0xda, 0x85, 0x84, 0x83, 0x82, 0x82,
	  0x84, 0x84, 0x84, 0x85, 0x81, 0x82, 0x82, 0x80, 0x80, 0x81, 0x83,
	  0x82, 0x7f, 0x99, 0x93, 0x86, 0x94, 0x8b, 0x92, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xdf, 0x00, 0xe5, 0x00, 0xde, 0x85, 0x84, 0x82, 0x82, 0x82,
	  0x84, 0x83, 0x83, 0x84, 0x81, 0x81, 0x80, 0x83, 0x82, 0x84, 0x82,
	  0x81, 0x7f, 0x99, 0x92, 0x86, 0x7b, 0x7b, 0x7c, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xe4, 0x00, 0xe9, 0x00, 0xe3, 0x84, 0x83, 0x82, 0x81, 0x81,
	  0x82, 0x83, 0x83, 0x84, 0x80, 0x81, 0x80, 0x83, 0x83, 0x84, 0x80,
	  0x81, 0x7c, 0x99, 0x92, 0x87, 0xa1, 0x93, 0x9d, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xe4, 0x00, 0xe9, 0x00, 0xe3, 0x85, 0x84, 0x83, 0x81, 0x81,
	  0x82, 0x82, 0x82, 0x83, 0x80, 0x81, 0x80, 0x81, 0x80, 0x82, 0x83,
	  0x82, 0x80, 0x91, 0x8d, 0x83, 0x9a, 0x90, 0x96, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xe4, 0x00, 0xe9, 0x00, 0xe3, 0x84, 0x83, 0x82, 0x81, 0x81,
	  0x82, 0x83, 0x83, 0x84, 0x80, 0x81, 0x80, 0x81, 0x80, 0x82, 0x83,
	  0x81, 0x7f, 0x91, 0x8c, 0x82, 0x8d, 0x88, 0x8b, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xe4, 0x00, 0xe9, 0x00, 0xe3, 0x84, 0x83, 0x82, 0x81, 0x81,
	  0x82, 0x83, 0x83, 0x83, 0x82, 0x82, 0x81, 0x81, 0x80, 0x82, 0x82,
	  0x82, 0x7f, 0x94, 0x89, 0x84, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xe4, 0x00, 0xe9, 0x00, 0xe3, 0x84, 0x83, 0x82, 0x81, 0x81,
	  0x82, 0x83, 0x83, 0x83, 0x82, 0x82, 0x81, 0x81, 0x80, 0x82, 0x83,
	  0x82, 0x7f, 0x91, 0x85, 0x81, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xe4, 0x00, 0xe9, 0x00, 0xe3, 0x84, 0x83, 0x82, 0x81, 0x81,
	  0x82, 0x83, 0x83, 0x83, 0x80, 0x80, 0x7f, 0x83, 0x82, 0x84, 0x83,
	  0x82, 0x7f, 0x90, 0x84, 0x81, 0x9a, 0x90, 0x96, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xe4, 0x00, 0xe9, 0x00, 0xe3, 0x84, 0x83, 0x82, 0x80, 0x80,
	  0x82, 0x83, 0x83, 0x83, 0x80, 0x80, 0x7f, 0x80, 0x80, 0x81, 0x81,
	  0x82, 0x83, 0x7e, 0x80, 0x7c, 0xa4, 0x97, 0x9f, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xe9, 0x00, 0xec, 0x00, 0xe8, 0x84, 0x83, 0x82, 0x81, 0x81,
	  0x82, 0x82, 0x82, 0x83, 0x7f, 0x7f, 0x7f, 0x81, 0x80, 0x82, 0x83,
	  0x83, 0x84, 0x79, 0x7c, 0x79, 0xb1, 0xa0, 0xaa, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xed, 0x00, 0xf0, 0x00, 0xec, 0x83, 0x83, 0x82, 0x80, 0x80,
	  0x81, 0x82, 0x82, 0x82, 0x7f, 0x7f, 0x7e, 0x81, 0x81, 0x82, 0x80,
	  0x81, 0x81, 0x84, 0x84, 0x83, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xf1, 0x00, 0xf4, 0x00, 0xf1, 0x83, 0x82, 0x82, 0x80, 0x80,
	  0x81, 0x82, 0x82, 0x82, 0x80, 0x80, 0x80, 0x80, 0x80, 0x81, 0x7d,
	  0x7e, 0x7f, 0x84, 0x84, 0x83, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xf6, 0x00, 0xf7, 0x00, 0xf5, 0x82, 0x82, 0x81, 0x80, 0x80,
	  0x80, 0x82, 0x82, 0x82, 0x80, 0x80, 0x80, 0x7f, 0x7f, 0x7f, 0x82,
	  0x82, 0x82, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x00, 0xfa, 0x00, 0xfb, 0x00, 0xfa, 0x81, 0x81, 0x81, 0x80, 0x80,
	  0x80, 0x82, 0x82, 0x82, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80,
	  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00,
	  0x00, 0x00 },
	{ 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80,
	  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00,
	  0x00, 0x00 }
};

static const unsigned char vint_table[S6E3HA2_VINT_STATUS_MAX] = {
	0x18, 0x19, 0x1a, 0x1b, 0x1c,
	0x1d, 0x1e, 0x1f, 0x20, 0x21
};

enum s6e3ha2_type {
	HA2_TYPE,
	HF2_TYPE,
};

struct s6e3ha2_panel_desc {
	const struct drm_display_mode *mode;
	enum s6e3ha2_type type;
};

struct s6e3ha2 {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *bl_dev;

	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;

	const struct s6e3ha2_panel_desc *desc;
};

static int s6e3ha2_dcs_write(struct s6e3ha2 *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	return mipi_dsi_dcs_write_buffer(dsi, data, len);
}

#define s6e3ha2_dcs_write_seq_static(ctx, seq...) do {	\
	static const u8 d[] = { seq };			\
	int ret;					\
	ret = s6e3ha2_dcs_write(ctx, d, ARRAY_SIZE(d));	\
	if (ret < 0)					\
		return ret;				\
} while (0)

#define s6e3ha2_call_write_func(ret, func) do {	\
	ret = (func);				\
	if (ret < 0)				\
		return ret;			\
} while (0)

static int s6e3ha2_test_key_on_f0(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xf0, 0x5a, 0x5a);
	return 0;
}

static int s6e3ha2_test_key_off_f0(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xf0, 0xa5, 0xa5);
	return 0;
}

static int s6e3ha2_test_key_on_fc(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xfc, 0x5a, 0x5a);
	return 0;
}

static int s6e3ha2_test_key_off_fc(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xfc, 0xa5, 0xa5);
	return 0;
}

static int s6e3ha2_single_dsi_set(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xf2, 0x67);
	s6e3ha2_dcs_write_seq_static(ctx, 0xf9, 0x09);
	return 0;
}

static int s6e3ha2_freq_calibration(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xfd, 0x1c);
	if (ctx->desc->type == HF2_TYPE)
		s6e3ha2_dcs_write_seq_static(ctx, 0xf2, 0x67, 0x40, 0xc5);
	s6e3ha2_dcs_write_seq_static(ctx, 0xfe, 0x20, 0x39);
	s6e3ha2_dcs_write_seq_static(ctx, 0xfe, 0xa0);
	s6e3ha2_dcs_write_seq_static(ctx, 0xfe, 0x20);

	if (ctx->desc->type == HA2_TYPE)
		s6e3ha2_dcs_write_seq_static(ctx, 0xce, 0x03, 0x3b, 0x12, 0x62,
						  0x40, 0x80, 0xc0, 0x28, 0x28,
						  0x28, 0x28, 0x39, 0xc5);
	else
		s6e3ha2_dcs_write_seq_static(ctx, 0xce, 0x03, 0x3b, 0x14, 0x6d,
						  0x40, 0x80, 0xc0, 0x28, 0x28,
						  0x28, 0x28, 0x39, 0xc5);

	return 0;
}

static int s6e3ha2_aor_control(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xb2, 0x03, 0x10);
	return 0;
}

static int s6e3ha2_caps_elvss_set(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xb6, 0x9c, 0x0a);
	return 0;
}

static int s6e3ha2_acl_off(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0x55, 0x00);
	return 0;
}

static int s6e3ha2_acl_off_opr(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xb5, 0x40);
	return 0;
}

static int s6e3ha2_test_global(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xb0, 0x07);
	return 0;
}

static int s6e3ha2_test(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xb8, 0x19);
	return 0;
}

static int s6e3ha2_touch_hsync_on1(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xbd, 0x33, 0x11, 0x02,
					0x16, 0x02, 0x16);
	return 0;
}

static int s6e3ha2_pentile_control(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xc0, 0x00, 0x00, 0xd8, 0xd8);
	return 0;
}

static int s6e3ha2_poc_global(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xb0, 0x20);
	return 0;
}

static int s6e3ha2_poc_setting(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xfe, 0x08);
	return 0;
}

static int s6e3ha2_pcd_set_off(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xcc, 0x40, 0x51);
	return 0;
}

static int s6e3ha2_err_fg_set(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xed, 0x44);
	return 0;
}

static int s6e3ha2_hbm_off(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0x53, 0x00);
	return 0;
}

static int s6e3ha2_te_start_setting(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xb9, 0x10, 0x09, 0xff, 0x00, 0x09);
	return 0;
}

static int s6e3ha2_gamma_update(struct s6e3ha2 *ctx)
{
	s6e3ha2_dcs_write_seq_static(ctx, 0xf7, 0x03);
	ndelay(100); /* need for 100ns delay */
	s6e3ha2_dcs_write_seq_static(ctx, 0xf7, 0x00);
	return 0;
}

static int s6e3ha2_get_brightness(struct backlight_device *bl_dev)
{
	return bl_dev->props.brightness;
}

static int s6e3ha2_set_vint(struct s6e3ha2 *ctx)
{
	struct backlight_device *bl_dev = ctx->bl_dev;
	unsigned int brightness = bl_dev->props.brightness;
	unsigned char data[] = { 0xf4, 0x8b,
			vint_table[brightness * (S6E3HA2_VINT_STATUS_MAX - 1) /
			S6E3HA2_MAX_BRIGHTNESS] };

	return s6e3ha2_dcs_write(ctx, data, ARRAY_SIZE(data));
}

static unsigned int s6e3ha2_get_brightness_index(unsigned int brightness)
{
	return (brightness * (S6E3HA2_NUM_GAMMA_STEPS - 1)) /
		S6E3HA2_MAX_BRIGHTNESS;
}

static int s6e3ha2_update_gamma(struct s6e3ha2 *ctx, unsigned int brightness)
{
	struct backlight_device *bl_dev = ctx->bl_dev;
	unsigned int index = s6e3ha2_get_brightness_index(brightness);
	u8 data[S6E3HA2_GAMMA_CMD_CNT + 1] = { 0xca, };
	int ret;

	memcpy(data + 1, gamma_tbl + index, S6E3HA2_GAMMA_CMD_CNT);
	s6e3ha2_call_write_func(ret,
				s6e3ha2_dcs_write(ctx, data, ARRAY_SIZE(data)));

	s6e3ha2_call_write_func(ret, s6e3ha2_gamma_update(ctx));
	bl_dev->props.brightness = brightness;

	return 0;
}

static int s6e3ha2_set_brightness(struct backlight_device *bl_dev)
{
	struct s6e3ha2 *ctx = bl_get_data(bl_dev);
	unsigned int brightness = bl_dev->props.brightness;
	int ret;

	if (brightness < S6E3HA2_MIN_BRIGHTNESS ||
		brightness > bl_dev->props.max_brightness) {
		dev_err(ctx->dev, "Invalid brightness: %u\n", brightness);
		return -EINVAL;
	}

	if (bl_dev->props.power > BACKLIGHT_POWER_REDUCED)
		return -EPERM;

	s6e3ha2_call_write_func(ret, s6e3ha2_test_key_on_f0(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_update_gamma(ctx, brightness));
	s6e3ha2_call_write_func(ret, s6e3ha2_aor_control(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_set_vint(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_test_key_off_f0(ctx));

	return 0;
}

static const struct backlight_ops s6e3ha2_bl_ops = {
	.get_brightness = s6e3ha2_get_brightness,
	.update_status = s6e3ha2_set_brightness,
};

static int s6e3ha2_panel_init(struct s6e3ha2 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	s6e3ha2_call_write_func(ret, mipi_dsi_dcs_exit_sleep_mode(dsi));
	usleep_range(5000, 6000);

	s6e3ha2_call_write_func(ret, s6e3ha2_test_key_on_f0(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_single_dsi_set(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_test_key_on_fc(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_freq_calibration(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_test_key_off_fc(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_test_key_off_f0(ctx));

	return 0;
}

static int s6e3ha2_power_off(struct s6e3ha2 *ctx)
{
	return regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
}

static int s6e3ha2_disable(struct drm_panel *panel)
{
	struct s6e3ha2 *ctx = container_of(panel, struct s6e3ha2, panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	s6e3ha2_call_write_func(ret, mipi_dsi_dcs_enter_sleep_mode(dsi));
	s6e3ha2_call_write_func(ret, mipi_dsi_dcs_set_display_off(dsi));

	msleep(40);
	ctx->bl_dev->props.power = BACKLIGHT_POWER_REDUCED;

	return 0;
}

static int s6e3ha2_unprepare(struct drm_panel *panel)
{
	struct s6e3ha2 *ctx = container_of(panel, struct s6e3ha2, panel);

	return s6e3ha2_power_off(ctx);
}

static int s6e3ha2_power_on(struct s6e3ha2 *ctx)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	msleep(120);

	gpiod_set_value(ctx->enable_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value(ctx->enable_gpio, 1);

	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);

	return 0;
}
static int s6e3ha2_prepare(struct drm_panel *panel)
{
	struct s6e3ha2 *ctx = container_of(panel, struct s6e3ha2, panel);
	int ret;

	ret = s6e3ha2_power_on(ctx);
	if (ret < 0)
		return ret;

	ret = s6e3ha2_panel_init(ctx);
	if (ret < 0)
		goto err;

	ctx->bl_dev->props.power = BACKLIGHT_POWER_REDUCED;

	return 0;

err:
	s6e3ha2_power_off(ctx);
	return ret;
}

static int s6e3ha2_enable(struct drm_panel *panel)
{
	struct s6e3ha2 *ctx = container_of(panel, struct s6e3ha2, panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	/* common setting */
	s6e3ha2_call_write_func(ret,
		mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK));

	s6e3ha2_call_write_func(ret, s6e3ha2_test_key_on_f0(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_test_key_on_fc(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_touch_hsync_on1(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_pentile_control(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_poc_global(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_poc_setting(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_test_key_off_fc(ctx));

	/* pcd setting off for TB */
	s6e3ha2_call_write_func(ret, s6e3ha2_pcd_set_off(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_err_fg_set(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_te_start_setting(ctx));

	/* brightness setting */
	s6e3ha2_call_write_func(ret, s6e3ha2_set_brightness(ctx->bl_dev));
	s6e3ha2_call_write_func(ret, s6e3ha2_aor_control(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_caps_elvss_set(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_gamma_update(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_acl_off(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_acl_off_opr(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_hbm_off(ctx));

	/* elvss temp compensation */
	s6e3ha2_call_write_func(ret, s6e3ha2_test_global(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_test(ctx));
	s6e3ha2_call_write_func(ret, s6e3ha2_test_key_off_f0(ctx));

	s6e3ha2_call_write_func(ret, mipi_dsi_dcs_set_display_on(dsi));
	ctx->bl_dev->props.power = BACKLIGHT_POWER_ON;

	return 0;
}

static const struct drm_display_mode s6e3ha2_mode = {
	.clock = 222372,
	.hdisplay = 1440,
	.hsync_start = 1440 + 1,
	.hsync_end = 1440 + 1 + 1,
	.htotal = 1440 + 1 + 1 + 1,
	.vdisplay = 2560,
	.vsync_start = 2560 + 1,
	.vsync_end = 2560 + 1 + 1,
	.vtotal = 2560 + 1 + 1 + 15,
	.flags = 0,
};

static const struct s6e3ha2_panel_desc samsung_s6e3ha2 = {
	.mode = &s6e3ha2_mode,
	.type = HA2_TYPE,
};

static const struct drm_display_mode s6e3hf2_mode = {
	.clock = 247856,
	.hdisplay = 1600,
	.hsync_start = 1600 + 1,
	.hsync_end = 1600 + 1 + 1,
	.htotal = 1600 + 1 + 1 + 1,
	.vdisplay = 2560,
	.vsync_start = 2560 + 1,
	.vsync_end = 2560 + 1 + 1,
	.vtotal = 2560 + 1 + 1 + 15,
	.flags = 0,
};

static const struct s6e3ha2_panel_desc samsung_s6e3hf2 = {
	.mode = &s6e3hf2_mode,
	.type = HF2_TYPE,
};

static int s6e3ha2_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct s6e3ha2 *ctx = container_of(panel, struct s6e3ha2, panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ctx->desc->mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			ctx->desc->mode->hdisplay, ctx->desc->mode->vdisplay,
			drm_mode_vrefresh(ctx->desc->mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 71;
	connector->display_info.height_mm = 125;

	return 1;
}

static const struct drm_panel_funcs s6e3ha2_drm_funcs = {
	.disable = s6e3ha2_disable,
	.unprepare = s6e3ha2_unprepare,
	.prepare = s6e3ha2_prepare,
	.enable = s6e3ha2_enable,
	.get_modes = s6e3ha2_get_modes,
};

static int s6e3ha2_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e3ha2 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct s6e3ha2, panel,
				   &s6e3ha2_drm_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	ctx->desc = of_device_get_match_data(dev);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS |
		MIPI_DSI_MODE_VIDEO_NO_HFP | MIPI_DSI_MODE_VIDEO_NO_HBP |
		MIPI_DSI_MODE_VIDEO_NO_HSA | MIPI_DSI_MODE_NO_EOT_PACKET;

	ctx->supplies[0].supply = "vdd3";
	ctx->supplies[1].supply = "vci";

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to get regulators: %d\n", ret);
		return ret;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}

	ctx->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->enable_gpio)) {
		dev_err(dev, "cannot get enable-gpios %ld\n",
			PTR_ERR(ctx->enable_gpio));
		return PTR_ERR(ctx->enable_gpio);
	}

	ctx->bl_dev = backlight_device_register("s6e3ha2", dev, ctx,
						&s6e3ha2_bl_ops, NULL);
	if (IS_ERR(ctx->bl_dev)) {
		dev_err(dev, "failed to register backlight device\n");
		return PTR_ERR(ctx->bl_dev);
	}

	ctx->bl_dev->props.max_brightness = S6E3HA2_MAX_BRIGHTNESS;
	ctx->bl_dev->props.brightness = S6E3HA2_DEFAULT_BRIGHTNESS;
	ctx->bl_dev->props.power = BACKLIGHT_POWER_OFF;

	ctx->panel.prepare_prev_first = true;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		goto remove_panel;

	return ret;

remove_panel:
	drm_panel_remove(&ctx->panel);
	backlight_device_unregister(ctx->bl_dev);

	return ret;
}

static void s6e3ha2_remove(struct mipi_dsi_device *dsi)
{
	struct s6e3ha2 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
	backlight_device_unregister(ctx->bl_dev);
}

static const struct of_device_id s6e3ha2_of_match[] = {
	{ .compatible = "samsung,s6e3ha2", .data = &samsung_s6e3ha2 },
	{ .compatible = "samsung,s6e3hf2", .data = &samsung_s6e3hf2 },
	{ }
};
MODULE_DEVICE_TABLE(of, s6e3ha2_of_match);

static struct mipi_dsi_driver s6e3ha2_driver = {
	.probe = s6e3ha2_probe,
	.remove = s6e3ha2_remove,
	.driver = {
		.name = "panel-samsung-s6e3ha2",
		.of_match_table = s6e3ha2_of_match,
	},
};
module_mipi_dsi_driver(s6e3ha2_driver);

MODULE_AUTHOR("Donghwa Lee <dh09.lee@samsung.com>");
MODULE_AUTHOR("Hyungwon Hwang <human.hwang@samsung.com>");
MODULE_AUTHOR("Hoegeun Kwon <hoegeun.kwon@samsung.com>");
MODULE_DESCRIPTION("MIPI-DSI based s6e3ha2 AMOLED Panel Driver");
MODULE_LICENSE("GPL v2");
