/*
 * Greybus audio driver
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <linux/delay.h>

#include "greybus.h"
#include "audio.h"

static LIST_HEAD(module_list);

#define GB_AUDIO_MGMT_DRIVER_NAME		"gb_audio_mgmt"

static void gbaudio_remove_dailink(struct gbaudio_module_info *info)
{
	int i;
	struct snd_soc_dai_link *dai_link = info->dai_link;

	for (i = 0; i < info->num_dai_links; i++) {
		snd_soc_remove_dai_link(info->card_name, dai_link->name);
		dai_link++;
	}
	info->num_dai_links = 0;
	info->dai_link = NULL;
}

int gbaudio_register_module(struct gbaudio_module_info *info)
{
	int ret, i;
	struct platform_device *pdev;
	struct gbaudio_codec_info *gbcodec;
	struct snd_soc_dai_link *dai_link = info->dai_link;
	char prefix_name[NAME_SIZE], codec_name[NAME_SIZE];

	/* register platform device for Module 0*/
	pdev = platform_device_register_simple(info->codec_name, info->index,
					       NULL, 0);
	if (!pdev)
		return -ENOMEM;

	info->pdev = pdev;
	udelay(1000);
	schedule();
	gbcodec = platform_get_drvdata(pdev);

	/* once codec is registered, add DAI link */
	if (!gbcodec || !gbcodec->registered) {
		dev_err(&pdev->dev, "%s: codec not yet registered\n",
			info->codec_name);
		ret = -EAGAIN;
		goto base_error;
	}

	snprintf(prefix_name, NAME_SIZE, "GB %d", info->mgmt_cport);
	snprintf(codec_name, NAME_SIZE, "%s.%d", info->codec_name,
		 info->index);
	ret = snd_soc_update_name_prefix(codec_name, prefix_name);
	if (ret) {
		dev_err(&pdev->dev, "Failed to set prefix name\n");
		goto base_error;
	}

	for (i = 0; i < info->num_dai_links; i++) {
		ret = snd_soc_add_dai_link(info->card_name, dai_link);
		if (ret) {
			dev_err(&pdev->dev,
				"%s: dai_link registration failed,%d\n",
				dai_link->name, ret);
			goto err_dai_link;
		}
		dai_link++;
	}

	/* add this module to module_list */
	list_add(&info->list, &module_list);

	return 0;

err_dai_link:
	dai_link = info->dai_link;
	for (; i > 0; i--) {
		snd_soc_remove_dai_link(info->card_name, dai_link->name);
		dai_link++;
	}
base_error:
	info->pdev = NULL;
	info->num_dai_links = 0;
	platform_device_unregister(pdev);

	return ret;
}

int gbaudio_unregister_module(struct gbaudio_module_info *gbmodule)
{
	int ret;
	char prefix_name[NAME_SIZE], codec_name[NAME_SIZE];
	struct platform_device *pdev = gbmodule->pdev;

	snprintf(prefix_name, NAME_SIZE, "GB %d", gbmodule->mgmt_cport);
	snprintf(codec_name, NAME_SIZE, "%s.%d", gbmodule->codec_name,
		 gbmodule->index);
	ret = snd_soc_update_name_prefix(codec_name, prefix_name);
	if (ret)
		dev_err(&pdev->dev, "Failed to set prefix name\n");

	/* remove dai_links if any */
	if (gbmodule->num_dai_links)
		gbaudio_remove_dailink(gbmodule);

	/* unregister platform device */
	if (pdev)
		platform_device_unregister(pdev);

	/* remove entry from the list */
	list_del(&gbmodule->list);

	return 0;
}

static struct snd_soc_dai_link gbaudio_dailink = {
	.name = "GB PRI_MI2S_RX",
	.stream_name = "Primary MI2S Playback",
	.platform_name = "qcom,msm-pcm-routing.41",
	.cpu_dai_name = "qcom,msm-dai-q6-mi2s-prim.204",
	.no_pcm = 1,
	.ignore_suspend = 1,
};

static int gb_audio_mgmt_connection_init(struct gb_connection *connection)
{
	int ret, index;
	struct gbaudio_module_info *gbmodule;
	struct snd_soc_dai_link *dai;
	char codec_name[NAME_SIZE];
	char codec_dai_name[NAME_SIZE];

	/* register module(s) */
	gbmodule = kzalloc(sizeof(struct gbaudio_module_info), GFP_KERNEL);
	if (!gbmodule)
		return -ENOMEM;

	/* assumption:
	 * each module can be used with single sound card at a time
	 */
	index = connection->bundle->id;
	snprintf(codec_name, NAME_SIZE, "%s.%d", "gbaudio-codec", index);
	snprintf(codec_dai_name, NAME_SIZE, "%s.%d", "gbcodec_pcm", index);

	strlcpy(gbmodule->codec_name, "gbaudio-codec", NAME_SIZE);
	strlcpy(gbmodule->card_name, "msm8994-tomtom-mtp-snd-card", NAME_SIZE);
	dai = &gbaudio_dailink;
	dai->codec_name = codec_name;
	dai->codec_dai_name = codec_dai_name;
	gbmodule->index = index;
	gbmodule->mgmt_cport = index;
	gbmodule->dai_link = dai;
	gbmodule->num_dai_links = 1;

	/* register module1 */
	ret = gbaudio_register_module(gbmodule);
	if (ret) {
		dev_err(&connection->bundle->dev,
			"Module initialization failed, %d\n", ret);
		return ret;
	}

	return 0;
}

static void gb_audio_mgmt_connection_exit(struct gb_connection *connection)
{
	struct gbaudio_module_info *module, *_module;

	/* stop active streams */

	/* unregister all modules */
	list_for_each_entry_safe(module, _module, &module_list, list) {
		if (module->mgmt_cport == connection->bundle->id) {
			gbaudio_unregister_module(module);
			kfree(module);
			break;
		}
	}
}

static int gb_audio_mgmt_report_event_recv(u8 type, struct gb_operation *op)
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
	.connection_init	= gb_audio_mgmt_connection_init,
	.connection_exit	= gb_audio_mgmt_connection_exit,
	.request_recv		= gb_audio_mgmt_report_event_recv,
};

/*
 * This is the basic hook get things initialized and registered w/ gb
 */

static int __init gb_audio_protocol_init(void)
{
	int ret;

	ret = gb_protocol_register(&gb_audio_mgmt_protocol);
	if (ret)
		pr_err("Can't register audio mgmt protocol driver: %d\n", -ret);

	return ret;
}

static void __exit gb_audio_protocol_exit(void)
{
	struct gbaudio_module_info *module, *_module;

	list_for_each_entry_safe(module, _module, &module_list, list) {
			gbaudio_unregister_module(module);
			kfree(module);
	}
	gb_protocol_deregister(&gb_audio_mgmt_protocol);
}

module_init(gb_audio_protocol_init);
module_exit(gb_audio_protocol_exit);

MODULE_DESCRIPTION("Greybus Audio protocol driver");
MODULE_AUTHOR("Vaibhav Agarwal <vaibhav.agarwal@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("greybus:audio-protocol");
