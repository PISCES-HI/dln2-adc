/*
 * Driver for the Diolan DLN-2 USB-ADC adapter
 *
 * Copyright (c) 2017 Jack Andersen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mfd/dln2.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define DLN2_ADC_MOD_NAME "dln2-adc"

#define DLN2_ADC_ID             0x06

#define DLN2_ADC_GET_CHANNEL_COUNT	DLN2_CMD(0x01, DLN2_ADC_ID)
#define DLN2_ADC_ENABLE			DLN2_CMD(0x02, DLN2_ADC_ID)
#define DLN2_ADC_DISABLE		DLN2_CMD(0x03, DLN2_ADC_ID)
#define DLN2_ADC_CHANNEL_ENABLE		DLN2_CMD(0x05, DLN2_ADC_ID)
#define DLN2_ADC_CHANNEL_DISABLE	DLN2_CMD(0x06, DLN2_ADC_ID)
#define DLN2_ADC_SET_RESOLUTION		DLN2_CMD(0x08, DLN2_ADC_ID)
#define DLN2_ADC_CHANNEL_GET_VAL	DLN2_CMD(0x0A, DLN2_ADC_ID)

#define DLN2_ADC_MAX_CHANNELS 8
#define DLN2_ADC_DATA_BITS 10

struct dln2_adc {
	struct platform_device *pdev;
	int port;
	/* Set once initialized */
	u8 chans_enabled;
};

struct dln2_adc_port_chan {
	u8 port;
	u8 chan;
};

static int dln2_adc_get_chan_count(struct dln2_adc *dln2)
{
	int ret;
	u8 port = dln2->port;
	int ilen = sizeof(port);
	u8 count;
	int olen = sizeof(count);

	ret = dln2_transfer(dln2->pdev, DLN2_ADC_GET_CHANNEL_COUNT,
				&port, ilen, &count, &olen);
	if (ret < 0)
		return ret;
	if (olen < sizeof(count))
		return -EPROTO;

	return count;
}

static int dln2_adc_set_port_resolution(struct dln2_adc *dln2)
{
	int ret;
	struct dln2_adc_port_chan port_chan = {
		.port = dln2->port,
		.chan = DLN2_ADC_DATA_BITS,
	};
	int ilen = sizeof(port_chan);

	ret = dln2_transfer_tx(dln2->pdev, DLN2_ADC_SET_RESOLUTION, &port_chan, ilen);
	if (ret < 0)
		return ret;

	return 0;
}

static int dln2_adc_set_chan_enabled(struct dln2_adc *dln2,
				int channel, bool enable)
{
	int ret;
	struct dln2_adc_port_chan port_chan = {
		.port = dln2->port,
		.chan = channel,
	};
	int ilen = sizeof(port_chan);

	ret = dln2_transfer_tx(dln2->pdev,
				enable ? DLN2_ADC_CHANNEL_ENABLE : DLN2_ADC_CHANNEL_DISABLE,
				&port_chan, ilen);
	if (ret < 0)
		return ret;

	return 0;
}

static int dln2_adc_set_port_enabled(struct dln2_adc *dln2, bool enable)
{
	int ret;
	ret = dln2_adc_get_chan_count(dln2);
	if (ret < 0)
		return ret;
	int chan_count = ret;

	ret = dln2_adc_set_port_resolution(dln2);
	if (ret < 0)
		return ret;

	int i;
	for (i = 0; i < chan_count; ++i) {
		ret = dln2_adc_set_chan_enabled(dln2, i, true);
		if (ret < 0)
			return ret;
	}

	u8 port = dln2->port;
	int ilen = sizeof(port);
	__le16 conflict;
	int olen = sizeof(conflict);

	ret = dln2_transfer(dln2->pdev, enable ? DLN2_ADC_ENABLE : DLN2_ADC_DISABLE,
				&port, ilen, &conflict, &olen);
	if (ret < 0)
		return ret;
	if (olen < sizeof(conflict))
		return -EPROTO;

	return 0;
}

static int dln2_adc_read(struct dln2_adc *dln2, unsigned channel)
{
	int ret;

	if (dln2->chans_enabled == 0) {
		ret = dln2_adc_set_port_enabled(dln2, true);
		if (ret < 0)
			return ret;
		dln2->chans_enabled = 1;
	}

	struct dln2_adc_port_chan port_chan = {
		.port = dln2->port,
		.chan = channel,
	};
	int ilen = sizeof(port_chan);
	__le16 value;
	int olen = sizeof(value);

	ret = dln2_transfer(dln2->pdev, DLN2_ADC_CHANNEL_GET_VAL,
				&port_chan, ilen, &value, &olen);
	if (ret < 0)
		return ret;
	if (olen < sizeof(value))
		return -EPROTO;

	return le16_to_cpu(value);
}

static int dln2_adc_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val,
				int *val2,
				long mask)
{
	struct dln2_adc *dln2 = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		int ret = dln2_adc_read(dln2, chan->channel);
		mutex_unlock(&indio_dev->mlock);

		if (ret < 0)
			break;
		*val = ret;

		return IIO_VAL_INT;
	}

	return -EINVAL;
}

#define DLN2_ADC_CHAN(idx) {				\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.channel = idx,					\
}

static const struct iio_chan_spec dln2_adc_iio_channels[] = {
	DLN2_ADC_CHAN(0),
	DLN2_ADC_CHAN(1),
	DLN2_ADC_CHAN(2),
	DLN2_ADC_CHAN(3),
	DLN2_ADC_CHAN(4),
	DLN2_ADC_CHAN(5),
	DLN2_ADC_CHAN(6),
	DLN2_ADC_CHAN(7),
};

static const struct iio_info dln2_adc_info = {
	.read_raw = &dln2_adc_read_raw,
	.driver_module = THIS_MODULE,
};

static int dln2_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dln2_adc *dln2;
	struct dln2_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct iio_dev *indio_dev = NULL;
	int ret = -ENODEV;

	indio_dev = devm_iio_device_alloc(dev, sizeof(struct dln2_adc));
	if (!indio_dev) {
		dev_err(dev, "failed allocating iio device\n");
		return -ENOMEM;
	}

	dln2 = iio_priv(indio_dev);
	dln2->pdev = pdev;
	dln2->port = pdata->port;

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->name = DLN2_ADC_MOD_NAME;
	indio_dev->dev.parent = dev;
	indio_dev->info = &dln2_adc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = dln2_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(dln2_adc_iio_channels);

	ret = iio_device_register(indio_dev);
	if (ret)
		return ret;

	dev_info(dev, "DLN2 ADC driver loaded\n");

	return 0;
}

static int dln2_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	iio_device_unregister(indio_dev);

	return 0;
}

static struct platform_driver dln2_adc_driver = {
	.driver.name	= DLN2_ADC_MOD_NAME,
	.probe		= dln2_adc_probe,
	.remove		= dln2_adc_remove,
};

module_platform_driver(dln2_adc_driver);

MODULE_AUTHOR("Jack Andersen <jackoalan@gmail.com");
MODULE_DESCRIPTION("Driver for the Diolan DLN2 ADC interface");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dln2-adc");
