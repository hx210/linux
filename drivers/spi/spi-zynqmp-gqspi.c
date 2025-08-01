// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Xilinx Zynq UltraScale+ MPSoC Quad-SPI (QSPI) controller driver
 * (host mode only)
 *
 * Copyright (C) 2009 - 2015 Xilinx, Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/spi/spi-mem.h>

/* Generic QSPI register offsets */
#define GQSPI_CONFIG_OFST		0x00000100
#define GQSPI_ISR_OFST			0x00000104
#define GQSPI_IDR_OFST			0x0000010C
#define GQSPI_IER_OFST			0x00000108
#define GQSPI_IMASK_OFST		0x00000110
#define GQSPI_EN_OFST			0x00000114
#define GQSPI_TXD_OFST			0x0000011C
#define GQSPI_RXD_OFST			0x00000120
#define GQSPI_TX_THRESHOLD_OFST		0x00000128
#define GQSPI_RX_THRESHOLD_OFST		0x0000012C
#define IOU_TAPDLY_BYPASS_OFST		0x0000003C
#define GQSPI_LPBK_DLY_ADJ_OFST		0x00000138
#define GQSPI_GEN_FIFO_OFST		0x00000140
#define GQSPI_SEL_OFST			0x00000144
#define GQSPI_GF_THRESHOLD_OFST		0x00000150
#define GQSPI_FIFO_CTRL_OFST		0x0000014C
#define GQSPI_QSPIDMA_DST_CTRL_OFST	0x0000080C
#define GQSPI_QSPIDMA_DST_SIZE_OFST	0x00000804
#define GQSPI_QSPIDMA_DST_STS_OFST	0x00000808
#define GQSPI_QSPIDMA_DST_I_STS_OFST	0x00000814
#define GQSPI_QSPIDMA_DST_I_EN_OFST	0x00000818
#define GQSPI_QSPIDMA_DST_I_DIS_OFST	0x0000081C
#define GQSPI_QSPIDMA_DST_I_MASK_OFST	0x00000820
#define GQSPI_QSPIDMA_DST_ADDR_OFST	0x00000800
#define GQSPI_QSPIDMA_DST_ADDR_MSB_OFST 0x00000828
#define GQSPI_DATA_DLY_ADJ_OFST         0x000001F8

/* GQSPI register bit masks */
#define GQSPI_SEL_MASK				0x00000001
#define GQSPI_EN_MASK				0x00000001
#define GQSPI_LPBK_DLY_ADJ_USE_LPBK_MASK	0x00000020
#define GQSPI_ISR_WR_TO_CLR_MASK		0x00000002
#define GQSPI_IDR_ALL_MASK			0x00000FBE
#define GQSPI_CFG_MODE_EN_MASK			0xC0000000
#define GQSPI_CFG_GEN_FIFO_START_MODE_MASK	0x20000000
#define GQSPI_CFG_ENDIAN_MASK			0x04000000
#define GQSPI_CFG_EN_POLL_TO_MASK		0x00100000
#define GQSPI_CFG_WP_HOLD_MASK			0x00080000
#define GQSPI_CFG_BAUD_RATE_DIV_MASK		0x00000038
#define GQSPI_CFG_CLK_PHA_MASK			0x00000004
#define GQSPI_CFG_CLK_POL_MASK			0x00000002
#define GQSPI_CFG_START_GEN_FIFO_MASK		0x10000000
#define GQSPI_GENFIFO_IMM_DATA_MASK		0x000000FF
#define GQSPI_GENFIFO_DATA_XFER			0x00000100
#define GQSPI_GENFIFO_EXP			0x00000200
#define GQSPI_GENFIFO_MODE_SPI			0x00000400
#define GQSPI_GENFIFO_MODE_DUALSPI		0x00000800
#define GQSPI_GENFIFO_MODE_QUADSPI		0x00000C00
#define GQSPI_GENFIFO_MODE_MASK			0x00000C00
#define GQSPI_GENFIFO_CS_LOWER			0x00001000
#define GQSPI_GENFIFO_CS_UPPER			0x00002000
#define GQSPI_GENFIFO_BUS_LOWER			0x00004000
#define GQSPI_GENFIFO_BUS_UPPER			0x00008000
#define GQSPI_GENFIFO_BUS_BOTH			0x0000C000
#define GQSPI_GENFIFO_BUS_MASK			0x0000C000
#define GQSPI_GENFIFO_TX			0x00010000
#define GQSPI_GENFIFO_RX			0x00020000
#define GQSPI_GENFIFO_STRIPE			0x00040000
#define GQSPI_GENFIFO_POLL			0x00080000
#define GQSPI_FIFO_CTRL_RST_RX_FIFO_MASK	0x00000004
#define GQSPI_FIFO_CTRL_RST_TX_FIFO_MASK	0x00000002
#define GQSPI_FIFO_CTRL_RST_GEN_FIFO_MASK	0x00000001
#define GQSPI_ISR_RXEMPTY_MASK			0x00000800
#define GQSPI_ISR_GENFIFOFULL_MASK		0x00000400
#define GQSPI_ISR_GENFIFONOT_FULL_MASK		0x00000200
#define GQSPI_ISR_TXEMPTY_MASK			0x00000100
#define GQSPI_ISR_GENFIFOEMPTY_MASK		0x00000080
#define GQSPI_ISR_RXFULL_MASK			0x00000020
#define GQSPI_ISR_RXNEMPTY_MASK			0x00000010
#define GQSPI_ISR_TXFULL_MASK			0x00000008
#define GQSPI_ISR_TXNOT_FULL_MASK		0x00000004
#define GQSPI_ISR_POLL_TIME_EXPIRE_MASK		0x00000002
#define GQSPI_IER_TXNOT_FULL_MASK		0x00000004
#define GQSPI_IER_RXEMPTY_MASK			0x00000800
#define GQSPI_IER_POLL_TIME_EXPIRE_MASK		0x00000002
#define GQSPI_IER_RXNEMPTY_MASK			0x00000010
#define GQSPI_IER_GENFIFOEMPTY_MASK		0x00000080
#define GQSPI_IER_TXEMPTY_MASK			0x00000100
#define GQSPI_QSPIDMA_DST_INTR_ALL_MASK		0x000000FE
#define GQSPI_QSPIDMA_DST_STS_WTC		0x0000E000
#define GQSPI_CFG_MODE_EN_DMA_MASK		0x80000000
#define GQSPI_ISR_IDR_MASK			0x00000994
#define GQSPI_QSPIDMA_DST_I_EN_DONE_MASK	0x00000002
#define GQSPI_QSPIDMA_DST_I_STS_DONE_MASK	0x00000002
#define GQSPI_IRQ_MASK				0x00000980

#define GQSPI_CFG_BAUD_RATE_DIV_SHIFT		3
#define GQSPI_GENFIFO_CS_SETUP			0x4
#define GQSPI_GENFIFO_CS_HOLD			0x3
#define GQSPI_TXD_DEPTH				64
#define GQSPI_RX_FIFO_THRESHOLD			32
#define GQSPI_RX_FIFO_FILL	(GQSPI_RX_FIFO_THRESHOLD * 4)
#define GQSPI_TX_FIFO_THRESHOLD_RESET_VAL	32
#define GQSPI_TX_FIFO_FILL	(GQSPI_TXD_DEPTH -\
				GQSPI_TX_FIFO_THRESHOLD_RESET_VAL)
#define GQSPI_GEN_FIFO_THRESHOLD_RESET_VAL	0X10
#define GQSPI_QSPIDMA_DST_CTRL_RESET_VAL	0x803FFA00
#define GQSPI_SELECT_FLASH_CS_LOWER		0x1
#define GQSPI_SELECT_FLASH_CS_UPPER		0x2
#define GQSPI_SELECT_FLASH_CS_BOTH		0x3
#define GQSPI_SELECT_FLASH_BUS_LOWER		0x1
#define GQSPI_SELECT_FLASH_BUS_UPPER		0x2
#define GQSPI_SELECT_FLASH_BUS_BOTH		0x3
#define GQSPI_BAUD_DIV_MAX	7	/* Baud rate divisor maximum */
#define GQSPI_BAUD_DIV_SHIFT	2	/* Baud rate divisor shift */
#define GQSPI_SELECT_MODE_SPI		0x1
#define GQSPI_SELECT_MODE_DUALSPI	0x2
#define GQSPI_SELECT_MODE_QUADSPI	0x4
#define GQSPI_DMA_UNALIGN		0x3
#define GQSPI_DEFAULT_NUM_CS	1	/* Default number of chip selects */

#define GQSPI_MAX_NUM_CS	2	/* Maximum number of chip selects */

#define GQSPI_USE_DATA_DLY		0x1
#define GQSPI_USE_DATA_DLY_SHIFT	31
#define GQSPI_DATA_DLY_ADJ_VALUE	0x2
#define GQSPI_DATA_DLY_ADJ_SHIFT	28
#define GQSPI_LPBK_DLY_ADJ_DLY_1	0x1
#define GQSPI_LPBK_DLY_ADJ_DLY_1_SHIFT	0x3
#define TAP_DLY_BYPASS_LQSPI_RX_VALUE	0x1
#define TAP_DLY_BYPASS_LQSPI_RX_SHIFT	0x2

/* set to differentiate versal from zynqmp, 1=versal, 0=zynqmp */
#define QSPI_QUIRK_HAS_TAPDELAY		BIT(0)

#define GQSPI_FREQ_37_5MHZ	37500000
#define GQSPI_FREQ_40MHZ	40000000
#define GQSPI_FREQ_100MHZ	100000000
#define GQSPI_FREQ_150MHZ	150000000

#define SPI_AUTOSUSPEND_TIMEOUT		3000
enum mode_type {GQSPI_MODE_IO, GQSPI_MODE_DMA};

/**
 * struct qspi_platform_data - zynqmp qspi platform data structure
 * @quirks:    Flags is used to identify the platform
 */
struct qspi_platform_data {
	u32 quirks;
};

/**
 * struct zynqmp_qspi - Defines qspi driver instance
 * @ctlr:		Pointer to the spi controller information
 * @regs:		Virtual address of the QSPI controller registers
 * @refclk:		Pointer to the peripheral clock
 * @pclk:		Pointer to the APB clock
 * @irq:		IRQ number
 * @dev:		Pointer to struct device
 * @txbuf:		Pointer to the TX buffer
 * @rxbuf:		Pointer to the RX buffer
 * @bytes_to_transfer:	Number of bytes left to transfer
 * @bytes_to_receive:	Number of bytes left to receive
 * @genfifocs:		Used for chip select
 * @genfifobus:		Used to select the upper or lower bus
 * @dma_rx_bytes:	Remaining bytes to receive by DMA mode
 * @dma_addr:		DMA address after mapping the kernel buffer
 * @genfifoentry:	Used for storing the genfifoentry instruction.
 * @mode:		Defines the mode in which QSPI is operating
 * @data_completion:	completion structure
 * @op_lock:		Operational lock
 * @speed_hz:          Current SPI bus clock speed in hz
 * @has_tapdelay:	Used for tapdelay register available in qspi
 */
struct zynqmp_qspi {
	struct spi_controller *ctlr;
	void __iomem *regs;
	struct clk *refclk;
	struct clk *pclk;
	int irq;
	struct device *dev;
	const void *txbuf;
	void *rxbuf;
	int bytes_to_transfer;
	int bytes_to_receive;
	u32 genfifocs;
	u32 genfifobus;
	u32 dma_rx_bytes;
	dma_addr_t dma_addr;
	u32 genfifoentry;
	enum mode_type mode;
	struct completion data_completion;
	struct mutex op_lock;
	u32 speed_hz;
	bool has_tapdelay;
};

/**
 * zynqmp_gqspi_read - For GQSPI controller read operation
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @offset:	Offset from where to read
 * Return:      Value at the offset
 */
static u32 zynqmp_gqspi_read(struct zynqmp_qspi *xqspi, u32 offset)
{
	return readl_relaxed(xqspi->regs + offset);
}

/**
 * zynqmp_gqspi_write - For GQSPI controller write operation
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @offset:	Offset where to write
 * @val:	Value to be written
 */
static inline void zynqmp_gqspi_write(struct zynqmp_qspi *xqspi, u32 offset,
				      u32 val)
{
	writel_relaxed(val, (xqspi->regs + offset));
}

/**
 * zynqmp_gqspi_selecttarget - For selection of target device
 * @instanceptr:	Pointer to the zynqmp_qspi structure
 * @targetcs:	For chip select
 * @targetbus:	To check which bus is selected- upper or lower
 */
static void zynqmp_gqspi_selecttarget(struct zynqmp_qspi *instanceptr,
				      u8 targetcs, u8 targetbus)
{
	/*
	 * Bus and CS lines selected here will be updated in the instance and
	 * used for subsequent GENFIFO entries during transfer.
	 */

	/* Choose target select line */
	switch (targetcs) {
	case GQSPI_SELECT_FLASH_CS_BOTH:
		instanceptr->genfifocs = GQSPI_GENFIFO_CS_LOWER |
			GQSPI_GENFIFO_CS_UPPER;
		break;
	case GQSPI_SELECT_FLASH_CS_UPPER:
		instanceptr->genfifocs = GQSPI_GENFIFO_CS_UPPER;
		break;
	case GQSPI_SELECT_FLASH_CS_LOWER:
		instanceptr->genfifocs = GQSPI_GENFIFO_CS_LOWER;
		break;
	default:
		dev_warn(instanceptr->dev, "Invalid target select\n");
	}

	/* Choose the bus */
	switch (targetbus) {
	case GQSPI_SELECT_FLASH_BUS_BOTH:
		instanceptr->genfifobus = GQSPI_GENFIFO_BUS_LOWER |
			GQSPI_GENFIFO_BUS_UPPER;
		break;
	case GQSPI_SELECT_FLASH_BUS_UPPER:
		instanceptr->genfifobus = GQSPI_GENFIFO_BUS_UPPER;
		break;
	case GQSPI_SELECT_FLASH_BUS_LOWER:
		instanceptr->genfifobus = GQSPI_GENFIFO_BUS_LOWER;
		break;
	default:
		dev_warn(instanceptr->dev, "Invalid target bus\n");
	}
}

/**
 * zynqmp_qspi_set_tapdelay:   To configure qspi tap delays
 * @xqspi:             Pointer to the zynqmp_qspi structure
 * @baudrateval:       Buadrate to configure
 */
static void zynqmp_qspi_set_tapdelay(struct zynqmp_qspi *xqspi, u32 baudrateval)
{
	u32 tapdlybypass = 0, lpbkdlyadj = 0, datadlyadj = 0, clk_rate;
	u32 reqhz = 0;

	clk_rate = clk_get_rate(xqspi->refclk);
	reqhz = (clk_rate / (GQSPI_BAUD_DIV_SHIFT << baudrateval));

	if (!xqspi->has_tapdelay) {
		if (reqhz <= GQSPI_FREQ_40MHZ) {
			zynqmp_pm_set_tapdelay_bypass(PM_TAPDELAY_QSPI,
						      PM_TAPDELAY_BYPASS_ENABLE);
		} else if (reqhz <= GQSPI_FREQ_100MHZ) {
			zynqmp_pm_set_tapdelay_bypass(PM_TAPDELAY_QSPI,
						      PM_TAPDELAY_BYPASS_ENABLE);
			lpbkdlyadj |= (GQSPI_LPBK_DLY_ADJ_USE_LPBK_MASK);
			datadlyadj |= ((GQSPI_USE_DATA_DLY <<
					GQSPI_USE_DATA_DLY_SHIFT)
					| (GQSPI_DATA_DLY_ADJ_VALUE <<
						GQSPI_DATA_DLY_ADJ_SHIFT));
		} else if (reqhz <= GQSPI_FREQ_150MHZ) {
			lpbkdlyadj |= GQSPI_LPBK_DLY_ADJ_USE_LPBK_MASK;
		}
	} else {
		if (reqhz <= GQSPI_FREQ_37_5MHZ) {
			tapdlybypass |= (TAP_DLY_BYPASS_LQSPI_RX_VALUE <<
					TAP_DLY_BYPASS_LQSPI_RX_SHIFT);
		} else if (reqhz <= GQSPI_FREQ_100MHZ) {
			tapdlybypass |= (TAP_DLY_BYPASS_LQSPI_RX_VALUE <<
					TAP_DLY_BYPASS_LQSPI_RX_SHIFT);
			lpbkdlyadj |= (GQSPI_LPBK_DLY_ADJ_USE_LPBK_MASK);
			datadlyadj |= (GQSPI_USE_DATA_DLY <<
					GQSPI_USE_DATA_DLY_SHIFT);
		} else if (reqhz <= GQSPI_FREQ_150MHZ) {
			lpbkdlyadj |= (GQSPI_LPBK_DLY_ADJ_USE_LPBK_MASK
				       | (GQSPI_LPBK_DLY_ADJ_DLY_1 <<
					       GQSPI_LPBK_DLY_ADJ_DLY_1_SHIFT));
		}
		zynqmp_gqspi_write(xqspi,
				   IOU_TAPDLY_BYPASS_OFST, tapdlybypass);
	}
	zynqmp_gqspi_write(xqspi, GQSPI_LPBK_DLY_ADJ_OFST, lpbkdlyadj);
	zynqmp_gqspi_write(xqspi, GQSPI_DATA_DLY_ADJ_OFST, datadlyadj);
}

/**
 * zynqmp_qspi_init_hw - Initialize the hardware
 * @xqspi:	Pointer to the zynqmp_qspi structure
 *
 * The default settings of the QSPI controller's configurable parameters on
 * reset are
 *	- Host mode
 *	- TX threshold set to 1
 *	- RX threshold set to 1
 *	- Flash memory interface mode enabled
 * This function performs the following actions
 *	- Disable and clear all the interrupts
 *	- Enable manual target select
 *	- Enable manual start
 *	- Deselect all the chip select lines
 *	- Set the little endian mode of TX FIFO
 *	- Set clock phase
 *	- Set clock polarity and
 *	- Enable the QSPI controller
 */
static void zynqmp_qspi_init_hw(struct zynqmp_qspi *xqspi)
{
	u32 config_reg, baud_rate_val = 0;
	ulong clk_rate;

	/* Select the GQSPI mode */
	zynqmp_gqspi_write(xqspi, GQSPI_SEL_OFST, GQSPI_SEL_MASK);
	/* Clear and disable interrupts */
	zynqmp_gqspi_write(xqspi, GQSPI_ISR_OFST,
			   zynqmp_gqspi_read(xqspi, GQSPI_ISR_OFST) |
			   GQSPI_ISR_WR_TO_CLR_MASK);
	/* Clear the DMA STS */
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_I_STS_OFST,
			   zynqmp_gqspi_read(xqspi,
					     GQSPI_QSPIDMA_DST_I_STS_OFST));
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_STS_OFST,
			   zynqmp_gqspi_read(xqspi,
					     GQSPI_QSPIDMA_DST_STS_OFST) |
					     GQSPI_QSPIDMA_DST_STS_WTC);
	zynqmp_gqspi_write(xqspi, GQSPI_IDR_OFST, GQSPI_IDR_ALL_MASK);
	zynqmp_gqspi_write(xqspi,
			   GQSPI_QSPIDMA_DST_I_DIS_OFST,
			   GQSPI_QSPIDMA_DST_INTR_ALL_MASK);
	/* Disable the GQSPI */
	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, 0x0);
	config_reg = zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST);
	config_reg &= ~GQSPI_CFG_MODE_EN_MASK;
	/* Manual start */
	config_reg |= GQSPI_CFG_GEN_FIFO_START_MODE_MASK;
	/* Little endian by default */
	config_reg &= ~GQSPI_CFG_ENDIAN_MASK;
	/* Disable poll time out */
	config_reg &= ~GQSPI_CFG_EN_POLL_TO_MASK;
	/* Set hold bit */
	config_reg |= GQSPI_CFG_WP_HOLD_MASK;
	/* Clear pre-scalar by default */
	config_reg &= ~GQSPI_CFG_BAUD_RATE_DIV_MASK;
	/* Set CPHA */
	if (xqspi->ctlr->mode_bits & SPI_CPHA)
		config_reg |= GQSPI_CFG_CLK_PHA_MASK;
	else
		config_reg &= ~GQSPI_CFG_CLK_PHA_MASK;
	/* Set CPOL */
	if (xqspi->ctlr->mode_bits & SPI_CPOL)
		config_reg |= GQSPI_CFG_CLK_POL_MASK;
	else
		config_reg &= ~GQSPI_CFG_CLK_POL_MASK;

	/* Set the clock frequency */
	clk_rate = clk_get_rate(xqspi->refclk);
	while ((baud_rate_val < GQSPI_BAUD_DIV_MAX) &&
	       (clk_rate /
		(GQSPI_BAUD_DIV_SHIFT << baud_rate_val)) > xqspi->speed_hz)
		baud_rate_val++;

	config_reg &= ~GQSPI_CFG_BAUD_RATE_DIV_MASK;
	config_reg |= (baud_rate_val << GQSPI_CFG_BAUD_RATE_DIV_SHIFT);

	zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST, config_reg);

	/* Set the tapdelay for clock frequency */
	zynqmp_qspi_set_tapdelay(xqspi, baud_rate_val);

	/* Clear the TX and RX FIFO */
	zynqmp_gqspi_write(xqspi, GQSPI_FIFO_CTRL_OFST,
			   GQSPI_FIFO_CTRL_RST_RX_FIFO_MASK |
			   GQSPI_FIFO_CTRL_RST_TX_FIFO_MASK |
			   GQSPI_FIFO_CTRL_RST_GEN_FIFO_MASK);
	/* Reset thresholds */
	zynqmp_gqspi_write(xqspi, GQSPI_TX_THRESHOLD_OFST,
			   GQSPI_TX_FIFO_THRESHOLD_RESET_VAL);
	zynqmp_gqspi_write(xqspi, GQSPI_RX_THRESHOLD_OFST,
			   GQSPI_RX_FIFO_THRESHOLD);
	zynqmp_gqspi_write(xqspi, GQSPI_GF_THRESHOLD_OFST,
			   GQSPI_GEN_FIFO_THRESHOLD_RESET_VAL);
	zynqmp_gqspi_selecttarget(xqspi,
				  GQSPI_SELECT_FLASH_CS_LOWER,
				  GQSPI_SELECT_FLASH_BUS_LOWER);
	/* Initialize DMA */
	zynqmp_gqspi_write(xqspi,
			   GQSPI_QSPIDMA_DST_CTRL_OFST,
			   GQSPI_QSPIDMA_DST_CTRL_RESET_VAL);

	/* Enable the GQSPI */
	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, GQSPI_EN_MASK);
}

/**
 * zynqmp_qspi_copy_read_data - Copy data to RX buffer
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @data:	The variable where data is stored
 * @size:	Number of bytes to be copied from data to RX buffer
 */
static void zynqmp_qspi_copy_read_data(struct zynqmp_qspi *xqspi,
				       ulong data, u8 size)
{
	memcpy(xqspi->rxbuf, &data, size);
	xqspi->rxbuf += size;
	xqspi->bytes_to_receive -= size;
}

/**
 * zynqmp_qspi_chipselect - Select or deselect the chip select line
 * @qspi:	Pointer to the spi_device structure
 * @is_high:	Select(0) or deselect (1) the chip select line
 */
static void zynqmp_qspi_chipselect(struct spi_device *qspi, bool is_high)
{
	struct zynqmp_qspi *xqspi = spi_controller_get_devdata(qspi->controller);
	ulong timeout;
	u32 genfifoentry = 0, statusreg;

	genfifoentry |= GQSPI_GENFIFO_MODE_SPI;

	if (!is_high) {
		if (!spi_get_chipselect(qspi, 0)) {
			xqspi->genfifobus = GQSPI_GENFIFO_BUS_LOWER;
			xqspi->genfifocs = GQSPI_GENFIFO_CS_LOWER;
		} else {
			xqspi->genfifobus = GQSPI_GENFIFO_BUS_UPPER;
			xqspi->genfifocs = GQSPI_GENFIFO_CS_UPPER;
		}
		genfifoentry |= xqspi->genfifobus;
		genfifoentry |= xqspi->genfifocs;
		genfifoentry |= GQSPI_GENFIFO_CS_SETUP;
	} else {
		genfifoentry |= GQSPI_GENFIFO_CS_HOLD;
	}

	zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST, genfifoentry);

	/* Manually start the generic FIFO command */
	zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST,
			   zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST) |
			   GQSPI_CFG_START_GEN_FIFO_MASK);

	timeout = jiffies + msecs_to_jiffies(1000);

	/* Wait until the generic FIFO command is empty */
	do {
		statusreg = zynqmp_gqspi_read(xqspi, GQSPI_ISR_OFST);

		if ((statusreg & GQSPI_ISR_GENFIFOEMPTY_MASK) &&
		    (statusreg & GQSPI_ISR_TXEMPTY_MASK))
			break;
		cpu_relax();
	} while (!time_after_eq(jiffies, timeout));

	if (time_after_eq(jiffies, timeout))
		dev_err(xqspi->dev, "Chip select timed out\n");
}

/**
 * zynqmp_qspi_selectspimode - Selects SPI mode - x1 or x2 or x4.
 * @xqspi:	xqspi is a pointer to the GQSPI instance
 * @spimode:	spimode - SPI or DUAL or QUAD.
 * Return:	Mask to set desired SPI mode in GENFIFO entry.
 */
static inline u32 zynqmp_qspi_selectspimode(struct zynqmp_qspi *xqspi,
					    u8 spimode)
{
	u32 mask = 0;

	switch (spimode) {
	case GQSPI_SELECT_MODE_DUALSPI:
		mask = GQSPI_GENFIFO_MODE_DUALSPI;
		break;
	case GQSPI_SELECT_MODE_QUADSPI:
		mask = GQSPI_GENFIFO_MODE_QUADSPI;
		break;
	case GQSPI_SELECT_MODE_SPI:
		mask = GQSPI_GENFIFO_MODE_SPI;
		break;
	default:
		dev_warn(xqspi->dev, "Invalid SPI mode\n");
	}

	return mask;
}

/**
 * zynqmp_qspi_config_op - Configure QSPI controller for specified
 *				transfer
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @op:		The memory operation to execute
 *
 * Sets the operational mode of QSPI controller for the next QSPI transfer and
 * sets the requested clock frequency.
 *
 * Return:	Always 0
 *
 * Note:
 *	If the requested frequency is not an exact match with what can be
 *	obtained using the pre-scalar value, the driver sets the clock
 *	frequency which is lower than the requested frequency (maximum lower)
 *	for the transfer.
 *
 *	If the requested frequency is higher or lower than that is supported
 *	by the QSPI controller the driver will set the highest or lowest
 *	frequency supported by controller.
 */
static int zynqmp_qspi_config_op(struct zynqmp_qspi *xqspi,
				 const struct spi_mem_op *op)
{
	ulong clk_rate;
	u32 config_reg, req_speed_hz, baud_rate_val = 0;

	req_speed_hz = op->max_freq;

	if (xqspi->speed_hz != req_speed_hz) {
		xqspi->speed_hz = req_speed_hz;

		/* Set the clock frequency */
		/* If req_speed_hz == 0, default to lowest speed */
		clk_rate = clk_get_rate(xqspi->refclk);

		while ((baud_rate_val < GQSPI_BAUD_DIV_MAX) &&
		       (clk_rate /
			(GQSPI_BAUD_DIV_SHIFT << baud_rate_val)) >
		       req_speed_hz)
			baud_rate_val++;

		config_reg = zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST);

		config_reg &= ~GQSPI_CFG_BAUD_RATE_DIV_MASK;
		config_reg |= (baud_rate_val << GQSPI_CFG_BAUD_RATE_DIV_SHIFT);
		zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST, config_reg);
		zynqmp_qspi_set_tapdelay(xqspi, baud_rate_val);
	}

	dev_dbg(xqspi->dev, "config speed %u\n", req_speed_hz);
	return 0;
}

/**
 * zynqmp_qspi_setup_op - Configure the QSPI controller
 * @qspi:	Pointer to the spi_device structure
 *
 * Sets the operational mode of QSPI controller for the next QSPI transfer,
 * baud rate and divisor value to setup the requested qspi clock.
 *
 * Return:	0 on success; error value otherwise.
 */
static int zynqmp_qspi_setup_op(struct spi_device *qspi)
{
	struct spi_controller *ctlr = qspi->controller;
	struct zynqmp_qspi *xqspi = spi_controller_get_devdata(ctlr);

	if (ctlr->busy)
		return -EBUSY;

	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, GQSPI_EN_MASK);

	return 0;
}

/**
 * zynqmp_qspi_filltxfifo - Fills the TX FIFO as long as there is room in
 *				the FIFO or the bytes required to be
 *				transmitted.
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @size:	Number of bytes to be copied from TX buffer to TX FIFO
 */
static void zynqmp_qspi_filltxfifo(struct zynqmp_qspi *xqspi, int size)
{
	u32 count = 0, intermediate;

	while ((xqspi->bytes_to_transfer > 0) && (count < size) && (xqspi->txbuf)) {
		if (xqspi->bytes_to_transfer >= 4) {
			memcpy(&intermediate, xqspi->txbuf, 4);
			xqspi->txbuf += 4;
			xqspi->bytes_to_transfer -= 4;
			count += 4;
		} else {
			memcpy(&intermediate, xqspi->txbuf,
			       xqspi->bytes_to_transfer);
			xqspi->txbuf += xqspi->bytes_to_transfer;
			xqspi->bytes_to_transfer = 0;
			count += xqspi->bytes_to_transfer;
		}
		zynqmp_gqspi_write(xqspi, GQSPI_TXD_OFST, intermediate);
	}
}

/**
 * zynqmp_qspi_readrxfifo - Fills the RX FIFO as long as there is room in
 *				the FIFO.
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @size:	Number of bytes to be copied from RX buffer to RX FIFO
 */
static void zynqmp_qspi_readrxfifo(struct zynqmp_qspi *xqspi, u32 size)
{
	ulong data;
	int count = 0;

	while ((count < size) && (xqspi->bytes_to_receive > 0)) {
		if (xqspi->bytes_to_receive >= 4) {
			(*(u32 *)xqspi->rxbuf) =
			zynqmp_gqspi_read(xqspi, GQSPI_RXD_OFST);
			xqspi->rxbuf += 4;
			xqspi->bytes_to_receive -= 4;
			count += 4;
		} else {
			data = zynqmp_gqspi_read(xqspi, GQSPI_RXD_OFST);
			count += xqspi->bytes_to_receive;
			zynqmp_qspi_copy_read_data(xqspi, data,
						   xqspi->bytes_to_receive);
			xqspi->bytes_to_receive = 0;
		}
	}
}

/**
 * zynqmp_qspi_fillgenfifo - Fills the GENFIFO.
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @nbits:	Transfer/Receive buswidth.
 * @genfifoentry:       Variable in which GENFIFO mask is saved
 */
static void zynqmp_qspi_fillgenfifo(struct zynqmp_qspi *xqspi, u8 nbits,
				    u32 genfifoentry)
{
	u32 transfer_len, tempcount, exponent;
	u8 imm_data;

	genfifoentry |= GQSPI_GENFIFO_DATA_XFER;
	if (xqspi->rxbuf) {
		genfifoentry |= GQSPI_GENFIFO_RX;
		if (xqspi->mode == GQSPI_MODE_DMA)
			transfer_len = xqspi->dma_rx_bytes;
		else
			transfer_len = xqspi->bytes_to_receive;
	} else {
		transfer_len = xqspi->bytes_to_transfer;
	}

	if (xqspi->txbuf)
		genfifoentry |= GQSPI_GENFIFO_TX;

	genfifoentry |= zynqmp_qspi_selectspimode(xqspi, nbits);
	xqspi->genfifoentry = genfifoentry;
	dev_dbg(xqspi->dev, "genfifo %05x transfer_len %u\n",
		genfifoentry, transfer_len);

	/* Exponent entries */
	imm_data = transfer_len;
	tempcount = transfer_len >> 8;
	exponent = 8;
	genfifoentry |= GQSPI_GENFIFO_EXP;
	while (tempcount) {
		if (tempcount & 1)
			zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST,
					   genfifoentry | exponent);
		tempcount >>= 1;
		exponent++;
	}

	/* Immediate entry */
	genfifoentry &= ~GQSPI_GENFIFO_EXP;
	if (imm_data)
		zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST,
				   genfifoentry | imm_data);

	/* Dummy generic FIFO entry */
	if (xqspi->mode == GQSPI_MODE_IO && xqspi->rxbuf)
		zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST, 0);
}

/**
 * zynqmp_qspi_disable_dma() - Disable DMA mode
 * @xqspi: GQSPI instance
 */
static void zynqmp_qspi_disable_dma(struct zynqmp_qspi *xqspi)
{
	u32 config_reg = zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST);

	config_reg &= ~GQSPI_CFG_MODE_EN_MASK;
	zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST, config_reg);
	xqspi->mode = GQSPI_MODE_IO;
}

/**
 * zynqmp_qspi_enable_dma() - Enable DMA mode
 * @xqspi: GQSPI instance
 */
static void zynqmp_qspi_enable_dma(struct zynqmp_qspi *xqspi)
{
	u32 config_reg = zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST);

	config_reg &= ~GQSPI_CFG_MODE_EN_MASK;
	config_reg |= GQSPI_CFG_MODE_EN_DMA_MASK;
	zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST, config_reg);
	xqspi->mode = GQSPI_MODE_DMA;
}

/**
 * zynqmp_process_dma_irq - Handler for DMA done interrupt of QSPI
 *				controller
 * @xqspi:	zynqmp_qspi instance pointer
 *
 * This function handles DMA interrupt only.
 */
static void zynqmp_process_dma_irq(struct zynqmp_qspi *xqspi)
{
	u32 genfifoentry;

	dma_unmap_single(xqspi->dev, xqspi->dma_addr,
			 xqspi->dma_rx_bytes, DMA_FROM_DEVICE);
	xqspi->rxbuf += xqspi->dma_rx_bytes;
	xqspi->bytes_to_receive -= xqspi->dma_rx_bytes;
	xqspi->dma_rx_bytes = 0;

	/* Disabling the DMA interrupts */
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_I_DIS_OFST,
			   GQSPI_QSPIDMA_DST_I_EN_DONE_MASK);

	if (xqspi->bytes_to_receive > 0) {
		/* Switch to IO mode,for remaining bytes to receive */
		zynqmp_qspi_disable_dma(xqspi);

		/* Initiate the transfer of remaining bytes */
		genfifoentry = xqspi->genfifoentry;
		genfifoentry |= xqspi->bytes_to_receive;
		zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST, genfifoentry);

		/* Dummy generic FIFO entry */
		zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST, 0x0);

		/* Manual start */
		zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST,
				   (zynqmp_gqspi_read(xqspi,
						      GQSPI_CONFIG_OFST) |
				   GQSPI_CFG_START_GEN_FIFO_MASK));

		/* Enable the RX interrupts for IO mode */
		zynqmp_gqspi_write(xqspi, GQSPI_IER_OFST,
				   GQSPI_IER_GENFIFOEMPTY_MASK |
				   GQSPI_IER_RXNEMPTY_MASK |
				   GQSPI_IER_RXEMPTY_MASK);
	}
}

/**
 * zynqmp_qspi_irq - Interrupt service routine of the QSPI controller
 * @irq:	IRQ number
 * @dev_id:	Pointer to the xqspi structure
 *
 * This function handles TX empty only.
 * On TX empty interrupt this function reads the received data from RX FIFO
 * and fills the TX FIFO if there is any data remaining to be transferred.
 *
 * Return:	IRQ_HANDLED when interrupt is handled
 *		IRQ_NONE otherwise.
 */
static irqreturn_t zynqmp_qspi_irq(int irq, void *dev_id)
{
	struct zynqmp_qspi *xqspi = (struct zynqmp_qspi *)dev_id;
	u32 status, mask, dma_status = 0;

	status = zynqmp_gqspi_read(xqspi, GQSPI_ISR_OFST);
	zynqmp_gqspi_write(xqspi, GQSPI_ISR_OFST, status);
	mask = (status & ~(zynqmp_gqspi_read(xqspi, GQSPI_IMASK_OFST)));

	/* Read and clear DMA status */
	if (xqspi->mode == GQSPI_MODE_DMA) {
		dma_status =
			zynqmp_gqspi_read(xqspi, GQSPI_QSPIDMA_DST_I_STS_OFST);
		zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_I_STS_OFST,
				   dma_status);
	}

	if (!mask && !dma_status)
		return IRQ_NONE;

	if (mask & GQSPI_ISR_TXNOT_FULL_MASK)
		zynqmp_qspi_filltxfifo(xqspi, GQSPI_TX_FIFO_FILL);

	if (dma_status & GQSPI_QSPIDMA_DST_I_STS_DONE_MASK)
		zynqmp_process_dma_irq(xqspi);
	else if (!(mask & GQSPI_IER_RXEMPTY_MASK) &&
			(mask & GQSPI_IER_GENFIFOEMPTY_MASK))
		zynqmp_qspi_readrxfifo(xqspi, GQSPI_RX_FIFO_FILL);

	if (xqspi->bytes_to_receive == 0 && xqspi->bytes_to_transfer == 0 &&
	    ((status & GQSPI_IRQ_MASK) == GQSPI_IRQ_MASK)) {
		zynqmp_gqspi_write(xqspi, GQSPI_IDR_OFST, GQSPI_ISR_IDR_MASK);
		complete(&xqspi->data_completion);
	}
	return IRQ_HANDLED;
}

/**
 * zynqmp_qspi_setuprxdma - This function sets up the RX DMA operation
 * @xqspi:	xqspi is a pointer to the GQSPI instance.
 *
 * Return:	0 on success; error value otherwise.
 */
static int zynqmp_qspi_setuprxdma(struct zynqmp_qspi *xqspi)
{
	u32 rx_bytes, rx_rem;
	dma_addr_t addr;
	u64 dma_align =  (u64)(uintptr_t)xqspi->rxbuf;

	if (xqspi->bytes_to_receive < 8 ||
	    ((dma_align & GQSPI_DMA_UNALIGN) != 0x0)) {
		/* Setting to IO mode */
		zynqmp_qspi_disable_dma(xqspi);
		xqspi->dma_rx_bytes = 0;
		return 0;
	}

	rx_rem = xqspi->bytes_to_receive % 4;
	rx_bytes = (xqspi->bytes_to_receive - rx_rem);

	addr = dma_map_single(xqspi->dev, (void *)xqspi->rxbuf,
			      rx_bytes, DMA_FROM_DEVICE);
	if (dma_mapping_error(xqspi->dev, addr)) {
		dev_err(xqspi->dev, "ERR:rxdma:memory not mapped\n");
		return -ENOMEM;
	}

	xqspi->dma_rx_bytes = rx_bytes;
	xqspi->dma_addr = addr;
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_ADDR_OFST,
			   (u32)(addr & 0xffffffff));
	addr = ((addr >> 16) >> 16);
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_ADDR_MSB_OFST,
			   ((u32)addr) & 0xfff);

	zynqmp_qspi_enable_dma(xqspi);

	/* Write the number of bytes to transfer */
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_SIZE_OFST, rx_bytes);

	return 0;
}

/**
 * zynqmp_qspi_write_op - This function sets up the GENFIFO entries,
 *			TX FIFO, and fills the TX FIFO with as many
 *			bytes as possible.
 * @xqspi:	Pointer to the GQSPI instance.
 * @tx_nbits:	Transfer buswidth.
 * @genfifoentry:	Variable in which GENFIFO mask is returned
 *			to calling function
 */
static void zynqmp_qspi_write_op(struct zynqmp_qspi *xqspi, u8 tx_nbits,
				 u32 genfifoentry)
{
	zynqmp_qspi_fillgenfifo(xqspi, tx_nbits, genfifoentry);
	zynqmp_qspi_filltxfifo(xqspi, GQSPI_TXD_DEPTH);
	if (xqspi->mode == GQSPI_MODE_DMA)
		zynqmp_qspi_disable_dma(xqspi);
}

/**
 * zynqmp_qspi_read_op - This function sets up the GENFIFO entries and
 *				RX DMA operation.
 * @xqspi:	xqspi is a pointer to the GQSPI instance.
 * @rx_nbits:	Receive buswidth.
 * @genfifoentry:	genfifoentry is pointer to the variable in which
 *			GENFIFO	mask is returned to calling function
 *
 * Return:	0 on success; error value otherwise.
 */
static int zynqmp_qspi_read_op(struct zynqmp_qspi *xqspi, u8 rx_nbits,
				u32 genfifoentry)
{
	int ret;

	ret = zynqmp_qspi_setuprxdma(xqspi);
	if (ret)
		return ret;
	zynqmp_qspi_fillgenfifo(xqspi, rx_nbits, genfifoentry);

	return 0;
}

/**
 * zynqmp_qspi_suspend - Suspend method for the QSPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function stops the QSPI driver queue and disables the QSPI controller
 *
 * Return:	Always 0
 */
static int __maybe_unused zynqmp_qspi_suspend(struct device *dev)
{
	struct zynqmp_qspi *xqspi = dev_get_drvdata(dev);
	struct spi_controller *ctlr = xqspi->ctlr;
	int ret;

	ret = spi_controller_suspend(ctlr);
	if (ret)
		return ret;

	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, 0x0);

	return 0;
}

/**
 * zynqmp_qspi_resume - Resume method for the QSPI driver
 * @dev:	Address of the platform_device structure
 *
 * The function starts the QSPI driver queue and initializes the QSPI
 * controller
 *
 * Return:	0 on success; error value otherwise
 */
static int __maybe_unused zynqmp_qspi_resume(struct device *dev)
{
	struct zynqmp_qspi *xqspi = dev_get_drvdata(dev);
	struct spi_controller *ctlr = xqspi->ctlr;

	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, GQSPI_EN_MASK);

	spi_controller_resume(ctlr);

	return 0;
}

/**
 * zynqmp_runtime_suspend - Runtime suspend method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function disables the clocks
 *
 * Return:	Always 0
 */
static int __maybe_unused zynqmp_runtime_suspend(struct device *dev)
{
	struct zynqmp_qspi *xqspi = dev_get_drvdata(dev);

	clk_disable_unprepare(xqspi->refclk);
	clk_disable_unprepare(xqspi->pclk);

	return 0;
}

/**
 * zynqmp_runtime_resume - Runtime resume method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function enables the clocks
 *
 * Return:	0 on success and error value on error
 */
static int __maybe_unused zynqmp_runtime_resume(struct device *dev)
{
	struct zynqmp_qspi *xqspi = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(xqspi->pclk);
	if (ret) {
		dev_err(dev, "Cannot enable APB clock.\n");
		return ret;
	}

	ret = clk_prepare_enable(xqspi->refclk);
	if (ret) {
		dev_err(dev, "Cannot enable device clock.\n");
		clk_disable_unprepare(xqspi->pclk);
		return ret;
	}

	return 0;
}

static unsigned long zynqmp_qspi_timeout(struct zynqmp_qspi *xqspi, u8 bits,
					 unsigned long bytes)
{
	unsigned long timeout;

	/* Assume we are at most 2x slower than the nominal bus speed */
	timeout = mult_frac(bytes, 2 * 8 * MSEC_PER_SEC,
			    bits * xqspi->speed_hz);
	/* And add 100 ms for scheduling delays */
	return msecs_to_jiffies(timeout + 100);
}

/**
 * zynqmp_qspi_exec_op() - Initiates the QSPI transfer
 * @mem: The SPI memory
 * @op: The memory operation to execute
 *
 * Executes a memory operation.
 *
 * This function first selects the chip and starts the memory operation.
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
static int zynqmp_qspi_exec_op(struct spi_mem *mem,
			       const struct spi_mem_op *op)
{
	struct zynqmp_qspi *xqspi =
		spi_controller_get_devdata(mem->spi->controller);
	unsigned long timeout;
	int err = 0, i;
	u32 genfifoentry = 0;
	u16 opcode = op->cmd.opcode;
	u64 opaddr;

	mutex_lock(&xqspi->op_lock);
	zynqmp_qspi_config_op(xqspi, op);
	zynqmp_qspi_chipselect(mem->spi, false);
	genfifoentry |= xqspi->genfifocs;
	genfifoentry |= xqspi->genfifobus;

	if (op->cmd.opcode) {
		reinit_completion(&xqspi->data_completion);
		xqspi->txbuf = &opcode;
		xqspi->rxbuf = NULL;
		xqspi->bytes_to_transfer = op->cmd.nbytes;
		xqspi->bytes_to_receive = 0;
		zynqmp_qspi_write_op(xqspi, op->cmd.buswidth, genfifoentry);
		zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST,
				   zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST) |
				   GQSPI_CFG_START_GEN_FIFO_MASK);
		zynqmp_gqspi_write(xqspi, GQSPI_IER_OFST,
				   GQSPI_IER_GENFIFOEMPTY_MASK |
				   GQSPI_IER_TXNOT_FULL_MASK);
		timeout = zynqmp_qspi_timeout(xqspi, op->cmd.buswidth,
					      op->cmd.nbytes);
		if (!wait_for_completion_timeout(&xqspi->data_completion,
						 timeout)) {
			err = -ETIMEDOUT;
			goto return_err;
		}
	}

	if (op->addr.nbytes) {
		xqspi->txbuf = &opaddr;
		for (i = 0; i < op->addr.nbytes; i++) {
			*(((u8 *)xqspi->txbuf) + i) = op->addr.val >>
					(8 * (op->addr.nbytes - i - 1));
		}

		reinit_completion(&xqspi->data_completion);
		xqspi->rxbuf = NULL;
		xqspi->bytes_to_transfer = op->addr.nbytes;
		xqspi->bytes_to_receive = 0;
		zynqmp_qspi_write_op(xqspi, op->addr.buswidth, genfifoentry);
		zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST,
				   zynqmp_gqspi_read(xqspi,
						     GQSPI_CONFIG_OFST) |
				   GQSPI_CFG_START_GEN_FIFO_MASK);
		zynqmp_gqspi_write(xqspi, GQSPI_IER_OFST,
				   GQSPI_IER_TXEMPTY_MASK |
				   GQSPI_IER_GENFIFOEMPTY_MASK |
				   GQSPI_IER_TXNOT_FULL_MASK);
		timeout = zynqmp_qspi_timeout(xqspi, op->addr.buswidth,
					      op->addr.nbytes);
		if (!wait_for_completion_timeout(&xqspi->data_completion,
						 timeout)) {
			err = -ETIMEDOUT;
			goto return_err;
		}
	}

	if (op->dummy.nbytes) {
		xqspi->txbuf = NULL;
		xqspi->rxbuf = NULL;
		/*
		 * xqspi->bytes_to_transfer here represents the dummy circles
		 * which need to be sent.
		 */
		xqspi->bytes_to_transfer = op->dummy.nbytes * 8 / op->dummy.buswidth;
		xqspi->bytes_to_receive = 0;
		/*
		 * Using op->data.buswidth instead of op->dummy.buswidth here because
		 * we need to use it to configure the correct SPI mode.
		 */
		zynqmp_qspi_write_op(xqspi, op->data.buswidth,
				     genfifoentry);
		zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST,
				   zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST) |
				   GQSPI_CFG_START_GEN_FIFO_MASK);
	}

	if (op->data.nbytes) {
		reinit_completion(&xqspi->data_completion);
		if (op->data.dir == SPI_MEM_DATA_OUT) {
			xqspi->txbuf = (u8 *)op->data.buf.out;
			xqspi->rxbuf = NULL;
			xqspi->bytes_to_transfer = op->data.nbytes;
			xqspi->bytes_to_receive = 0;
			zynqmp_qspi_write_op(xqspi, op->data.buswidth,
					     genfifoentry);
			zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST,
					   zynqmp_gqspi_read
					   (xqspi, GQSPI_CONFIG_OFST) |
					   GQSPI_CFG_START_GEN_FIFO_MASK);
			zynqmp_gqspi_write(xqspi, GQSPI_IER_OFST,
					   GQSPI_IER_TXEMPTY_MASK |
					   GQSPI_IER_GENFIFOEMPTY_MASK |
					   GQSPI_IER_TXNOT_FULL_MASK);
		} else {
			xqspi->txbuf = NULL;
			xqspi->rxbuf = (u8 *)op->data.buf.in;
			xqspi->bytes_to_receive = op->data.nbytes;
			xqspi->bytes_to_transfer = 0;
			err = zynqmp_qspi_read_op(xqspi, op->data.buswidth,
					    genfifoentry);
			if (err)
				goto return_err;

			zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST,
					   zynqmp_gqspi_read
					   (xqspi, GQSPI_CONFIG_OFST) |
					   GQSPI_CFG_START_GEN_FIFO_MASK);
			if (xqspi->mode == GQSPI_MODE_DMA) {
				zynqmp_gqspi_write
					(xqspi, GQSPI_QSPIDMA_DST_I_EN_OFST,
					 GQSPI_QSPIDMA_DST_I_EN_DONE_MASK);
			} else {
				zynqmp_gqspi_write(xqspi, GQSPI_IER_OFST,
						   GQSPI_IER_GENFIFOEMPTY_MASK |
						   GQSPI_IER_RXNEMPTY_MASK |
						   GQSPI_IER_RXEMPTY_MASK);
			}
		}
		timeout = zynqmp_qspi_timeout(xqspi, op->data.buswidth,
					      op->data.nbytes);
		if (!wait_for_completion_timeout(&xqspi->data_completion, timeout))
			err = -ETIMEDOUT;
	}

return_err:

	zynqmp_qspi_chipselect(mem->spi, true);
	mutex_unlock(&xqspi->op_lock);

	return err;
}

static const struct dev_pm_ops zynqmp_qspi_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(zynqmp_runtime_suspend,
			   zynqmp_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(zynqmp_qspi_suspend, zynqmp_qspi_resume)
};

static const struct qspi_platform_data versal_qspi_def = {
	.quirks = QSPI_QUIRK_HAS_TAPDELAY,
};

static const struct of_device_id zynqmp_qspi_of_match[] = {
	{ .compatible = "xlnx,zynqmp-qspi-1.0"},
	{ .compatible = "xlnx,versal-qspi-1.0", .data = &versal_qspi_def },
	{ /* End of table */ }
};

static const struct spi_controller_mem_ops zynqmp_qspi_mem_ops = {
	.exec_op = zynqmp_qspi_exec_op,
};

static const struct spi_controller_mem_caps zynqmp_qspi_mem_caps = {
	.per_op_freq = true,
};

/**
 * zynqmp_qspi_probe - Probe method for the QSPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function initializes the driver data structures and the hardware.
 *
 * Return:	0 on success; error value otherwise
 */
static int zynqmp_qspi_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct spi_controller *ctlr;
	struct zynqmp_qspi *xqspi;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	u32 num_cs;
	const struct qspi_platform_data *p_data;

	ctlr = devm_spi_alloc_host(&pdev->dev, sizeof(*xqspi));
	if (!ctlr)
		return -ENOMEM;

	xqspi = spi_controller_get_devdata(ctlr);
	xqspi->dev = dev;
	xqspi->ctlr = ctlr;
	platform_set_drvdata(pdev, xqspi);

	p_data = of_device_get_match_data(&pdev->dev);
	if (p_data && (p_data->quirks & QSPI_QUIRK_HAS_TAPDELAY))
		xqspi->has_tapdelay = true;

	xqspi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xqspi->regs))
		return PTR_ERR(xqspi->regs);

	xqspi->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(xqspi->pclk))
		return dev_err_probe(dev, PTR_ERR(xqspi->pclk),
				     "pclk clock not found.\n");

	xqspi->refclk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(xqspi->refclk))
		return dev_err_probe(dev, PTR_ERR(xqspi->refclk),
				     "ref_clk clock not found.\n");

	ret = clk_prepare_enable(xqspi->pclk);
	if (ret)
		return dev_err_probe(dev, ret, "Unable to enable APB clock.\n");

	ret = clk_prepare_enable(xqspi->refclk);
	if (ret) {
		dev_err(dev, "Unable to enable device clock.\n");
		goto clk_dis_pclk;
	}

	init_completion(&xqspi->data_completion);

	mutex_init(&xqspi->op_lock);

	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, SPI_AUTOSUSPEND_TIMEOUT);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to pm_runtime_get_sync: %d\n", ret);
		goto clk_dis_all;
	}

	ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_RX_DUAL | SPI_RX_QUAD |
		SPI_TX_DUAL | SPI_TX_QUAD;
	ctlr->max_speed_hz = clk_get_rate(xqspi->refclk) / 2;
	xqspi->speed_hz = ctlr->max_speed_hz;

	/* QSPI controller initializations */
	zynqmp_qspi_init_hw(xqspi);

	xqspi->irq = platform_get_irq(pdev, 0);
	if (xqspi->irq < 0) {
		ret = xqspi->irq;
		goto clk_dis_all;
	}
	ret = devm_request_irq(&pdev->dev, xqspi->irq, zynqmp_qspi_irq,
			       0, pdev->name, xqspi);
	if (ret != 0) {
		ret = -ENXIO;
		dev_err(dev, "request_irq failed\n");
		goto clk_dis_all;
	}

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(44));
	if (ret)
		goto clk_dis_all;

	ret = of_property_read_u32(np, "num-cs", &num_cs);
	if (ret < 0) {
		ctlr->num_chipselect = GQSPI_DEFAULT_NUM_CS;
	} else if (num_cs > GQSPI_MAX_NUM_CS) {
		ret = -EINVAL;
		dev_err(&pdev->dev, "only %d chip selects are available\n",
			GQSPI_MAX_NUM_CS);
		goto clk_dis_all;
	} else {
		ctlr->num_chipselect = num_cs;
	}

	ctlr->bits_per_word_mask = SPI_BPW_MASK(8);
	ctlr->mem_ops = &zynqmp_qspi_mem_ops;
	ctlr->mem_caps = &zynqmp_qspi_mem_caps;
	ctlr->setup = zynqmp_qspi_setup_op;
	ctlr->bits_per_word_mask = SPI_BPW_MASK(8);
	ctlr->dev.of_node = np;
	ctlr->auto_runtime_pm = true;

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret) {
		dev_err(&pdev->dev, "spi_register_controller failed\n");
		goto clk_dis_all;
	}

	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;

clk_dis_all:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	clk_disable_unprepare(xqspi->refclk);
clk_dis_pclk:
	clk_disable_unprepare(xqspi->pclk);

	return ret;
}

/**
 * zynqmp_qspi_remove - Remove method for the QSPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees all resources allocated to
 * the device.
 *
 * Return:	0 Always
 */
static void zynqmp_qspi_remove(struct platform_device *pdev)
{
	struct zynqmp_qspi *xqspi = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);

	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, 0x0);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	clk_disable_unprepare(xqspi->refclk);
	clk_disable_unprepare(xqspi->pclk);
}

MODULE_DEVICE_TABLE(of, zynqmp_qspi_of_match);

static struct platform_driver zynqmp_qspi_driver = {
	.probe = zynqmp_qspi_probe,
	.remove = zynqmp_qspi_remove,
	.driver = {
		.name = "zynqmp-qspi",
		.of_match_table = zynqmp_qspi_of_match,
		.pm = &zynqmp_qspi_dev_pm_ops,
	},
};

module_platform_driver(zynqmp_qspi_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx Zynqmp QSPI driver");
MODULE_LICENSE("GPL");
