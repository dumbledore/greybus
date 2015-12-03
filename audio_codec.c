/*
 * Greybus audio driver
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */
#include <linux/module.h>

#include "greybus.h"
#include "audio_codec.h"

#define GB_AUDIO_MGMT_DRIVER_NAME	"gb_audio_mgmt"

static int gbcodec_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	return 0;
}

static void gbcodec_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
}

static int gbcodec_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hwparams,
			     struct snd_soc_dai *dai)
{
	return 0;
}

static int gbcodec_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	return 0;
}

static int gbcodec_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	return 0;
}

static int gbcodec_digital_mute(struct snd_soc_dai *dai, int mute)
{
	return 0;
}

static struct snd_soc_dai_ops gbcodec_dai_ops = {
	.startup = gbcodec_startup,
	.shutdown = gbcodec_shutdown,
	.hw_params = gbcodec_hw_params,
	.prepare = gbcodec_prepare,
	.set_fmt = gbcodec_set_dai_fmt,
	.digital_mute = gbcodec_digital_mute,
};

static int gbcodec_probe(struct snd_soc_codec *codec)
{
	struct gbaudio_codec_info *gbcodec = snd_soc_codec_get_drvdata(codec);

	gbcodec->codec = codec;

	return 0;
}

static int gbcodec_remove(struct snd_soc_codec *codec)
{
	/* Empty function for now */
	return 0;
}

static int gbcodec_write(struct snd_soc_codec *codec, unsigned int reg,
			 unsigned int value)
{
	int ret = 0;
	struct gbaudio_codec_info *gbcodec = snd_soc_codec_get_drvdata(codec);
	u8 *gbcodec_reg = gbcodec->reg;

	if (reg == SND_SOC_NOPM)
		return 0;

	if (reg >= GBCODEC_REG_COUNT)
		return 0;

	gbcodec_reg[reg] = value;
	dev_dbg(codec->dev, "reg[%d] = 0x%x\n", reg, value);

	return ret;
}

static unsigned int gbcodec_read(struct snd_soc_codec *codec,
				 unsigned int reg)
{
	unsigned int val = 0;

	struct gbaudio_codec_info *gbcodec = snd_soc_codec_get_drvdata(codec);
	u8 *gbcodec_reg = gbcodec->reg;

	if (reg == SND_SOC_NOPM)
		return 0;

	if (reg >= GBCODEC_REG_COUNT)
		return 0;

	val = gbcodec_reg[reg];
	dev_dbg(codec->dev, "reg[%d] = 0x%x\n", reg, val);

	return val;
}

static struct snd_soc_codec_driver soc_codec_dev_gbcodec = {
	.probe = gbcodec_probe,
	.remove = gbcodec_remove,

	.read = gbcodec_read,
	.write = gbcodec_write,

	.reg_cache_size = GBCODEC_REG_COUNT,
	.reg_cache_default = gbcodec_reg_defaults,
	.reg_word_size = 1,

	.idle_bias_off = true,
};

struct device_driver gb_codec_driver = {
	.name = "1-8",
	.owner = THIS_MODULE,
};

/*
 * This is the basic hook get things initialized and registered w/ gb
 */

static int gbaudio_codec_probe(struct gb_connection *connection)
{
	int ret, i;
	struct gbaudio_codec_info *gbcodec;
	struct gb_audio_topology *topology;
	char name[NAME_SIZE], dai_name[NAME_SIZE];
	struct gbaudio_module_info *gbmod_info;
        struct device *dev = &connection->bundle->dev;
	int dev_id = connection->bundle->id;

	gbcodec = devm_kzalloc(dev, sizeof(struct gbaudio_codec_info),
			       GFP_KERNEL);
	if (!gbcodec)
		return -ENOMEM;
	dev_set_drvdata(dev, gbcodec);
	gbcodec->dev = dev;
	gbcodec->mgmt_connection = connection;

	strlcpy(name, dev_name(dev), NAME_SIZE);
	dev_err(dev, "codec name is %s\n", name);

	/* fetch topology data */
	ret = gb_audio_gb_get_topology(connection, &topology);
	if (ret) {
		dev_err(gbcodec->dev,
			"%d:Error while fetching topology\n", ret);
		return ret;
	}

	/* validate topology data */
	INIT_LIST_HEAD(&gbcodec->dai_list);
	INIT_LIST_HEAD(&gbcodec->widget_list);
	INIT_LIST_HEAD(&gbcodec->codec_ctl_list);
	INIT_LIST_HEAD(&gbcodec->widget_ctl_list);

	/* process topology data */
	ret = gbaudio_tplg_parse_data(gbcodec, topology);
	if (ret) {
		dev_err(dev, "%d:Error while parsing topology data\n",
			  ret);
		return ret;
	}

	/* update codec info */
	soc_codec_dev_gbcodec.controls = gbcodec->kctls;
	soc_codec_dev_gbcodec.num_controls = gbcodec->num_kcontrols;
	soc_codec_dev_gbcodec.dapm_widgets = gbcodec->widgets;
	soc_codec_dev_gbcodec.num_dapm_widgets = gbcodec->num_dapm_widgets;
	soc_codec_dev_gbcodec.dapm_routes = gbcodec->routes;
	soc_codec_dev_gbcodec.num_dapm_routes = gbcodec->num_dapm_routes;

	/* update DAI info */
	for (i = 0; i < gbcodec->num_dais; i++) {
		snprintf(dai_name, NAME_SIZE, "%s.%d", gbcodec->dais[i].name,
			 dev_id);
		gbcodec->dais[i].name = dai_name;
		gbcodec->dais[i].ops = &gbcodec_dai_ops;
	}

	/* register codec */
	dev->driver = &gb_codec_driver;
	ret = snd_soc_register_codec(dev, &soc_codec_dev_gbcodec,
				     gbcodec->dais, 1);
	if (ret) {
		dev_err(dev, "%d:Failed to register codec\n", ret);
		return ret;
	}

	/* update DAI links in response to this codec */
	gbmod_info = devm_kzalloc(dev, sizeof(struct gbaudio_module_info),
				  GFP_KERNEL);
	if (!gbmod_info) {
		ret = -ENOMEM;
		goto base_error;
	}

	gbmod_info->dev = dev;
	gbmod_info->dev_id = dev_id;
	strlcpy(gbmod_info->codec_name, name, NAME_SIZE);
	gbmod_info->num_dais = gbcodec->num_dais;
	for (i = 0; i < gbcodec->num_dais; i++)
		gbmod_info->dai_names[i] = gbcodec->dais[i].name;
	ret = gbaudio_register_module(gbmod_info);
	if (ret) {
		dev_err(dev, "Unable to update DAI links for %s codec inserted\n",
			name);
		ret = -EAGAIN;
		goto base_error;
	}
	gbcodec->modinfo = gbmod_info;

	return ret;

base_error:
	snd_soc_unregister_codec(dev);
	return ret;
}

static void gbaudio_codec_remove(struct gb_connection *connection)
{
        struct device *dev = &connection->bundle->dev;
	struct gbaudio_codec_info *gbcodec;
	struct gbaudio_module_info *gbmod_info;

	gbcodec = (struct gbaudio_codec_info *)dev_get_drvdata(dev);
	gbmod_info = gbcodec->modinfo;

	if (gbmod_info) {
		gbaudio_unregister_module(gbmod_info);
		devm_kfree(dev, gbmod_info);
	}
	snd_soc_unregister_codec(dev);
	dev->driver = NULL;

	return;
}

static int gbaudio_codec_report_event_recv(u8 type, struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;

	dev_warn(&connection->bundle->dev, "Audio Event received\n");

	return 0;
}

static struct gb_protocol gb_audio_mgmt_protocol = {
	.name			= GB_AUDIO_MGMT_DRIVER_NAME,
	.id			= GREYBUS_PROTOCOL_AUDIO_MGMT,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gbaudio_codec_probe,
	.connection_exit	= gbaudio_codec_remove,
	.request_recv		= gbaudio_codec_report_event_recv,
};
gb_protocol_driver(&gb_audio_mgmt_protocol)

MODULE_DESCRIPTION("Greybus Audio virtual codec driver");
MODULE_AUTHOR("Vaibhav Agarwal <vaibhav.agarwal@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gbaudio-codec");
