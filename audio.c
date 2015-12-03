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
#include "gb_audio_manager.h"

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
	struct gb_audio_topology *topology;
	struct gb_audio_manager_module_descriptor desc;

	/* register platform device for Module 0*/
	pdev = platform_device_register_simple(info->codec_name, info->index,
					       NULL, 0);
	if (!pdev)
		return -ENOMEM;

	ret = gb_audio_gb_get_topology(info->mgmt_connection, &topology);
	if (ret)
		return ret;

#if 1 /* TODO: Remove when no longer useful */
	dev_info(&pdev->dev, "num_dais: %hhu\n", topology->num_dais);
	dev_info(&pdev->dev, "num_controls: %hhu\n", topology->num_controls);
	dev_info(&pdev->dev, "num_widgets: %hhu\n", topology->num_widgets);
	dev_info(&pdev->dev, "num_routes: %hhu\n", topology->num_routes);

	/* TODO: Remember to 'kfree(topology);' when done with it */
#endif

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

	/* prepare for the audio manager */
	strncpy(desc.name, codec_name, GB_AUDIO_MANAGER_MODULE_NAME_LEN); /* todo */
	desc.slot = 1; /* todo */
	desc.vid = 2; /* todo */
	desc.pid = 3; /* todo */
	desc.cport = info->mgmt_cport;
	desc.devices = 0x2; /* todo */
	info->manager_id = gb_audio_manager_add(&desc);

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

	/* notify the audio manager */
	gb_audio_manager_remove(gbmodule->manager_id);

	/* remove entry from the list */
	list_del(&gbmodule->list);

	return 0;
}

static struct snd_soc_dai_link gbaudio_dailink = {
	.name = "GB PRI_MI2S_RX",
	.stream_name = "Primary MI2S Playback",
	.platform_name = "msm-pcm-routing",
	.cpu_dai_name = "msm-dai-q6-mi2s.0",
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
	char card_name[NAME_SIZE] = "msm8994-tomtom-mtp-snd-card";
        struct device_node *np;
	struct snd_soc_card *card;
        struct device *pdev = &connection->bundle->dev;
        struct device *cdev;

	/* register module(s) */
	gbmodule = kzalloc(sizeof(struct gbaudio_module_info), GFP_KERNEL);
	if (!gbmodule)
		return -ENOMEM;

	dai = &gbaudio_dailink;
	card = snd_soc_get_card(card_name);
	if (!card) {
		dev_err(pdev, "Unable to find %s soc card\n",
			card_name);
		return -ENODEV;
	}
	cdev = card->dev;

	/* populate cpu_of_node for snd card dai links */
	if (dai->cpu_dai_name && !dai->cpu_of_node) {
                index = of_property_match_string(cdev->of_node,
                                                 "asoc-cpu-names",
                                                 dai->cpu_dai_name);
		if (index < 0) {
			dev_err(pdev, "No match found for cpu_dai name: %s\n",
				dai->cpu_dai_name);
                        return -ENODEV;
		}
		np = of_parse_phandle(cdev->of_node, "asoc-cpu",
				      index);
		if (!np) {
			dev_err(pdev, "retrieving phandle for cpu dai %s failed\n",
				dai->cpu_dai_name);
			return -ENODEV;
		}
		dai->cpu_of_node = np;
		dai->cpu_dai_name = NULL;
	}

	/* populate platform_of_node for snd card dai links */
	if (dai->platform_name && !dai->platform_of_node) {
		index = of_property_match_string(cdev->of_node,
						 "asoc-platform-names",
						 dai->platform_name);
		if (index < 0) {
			dev_err(pdev, "No match found for platform name: %s\n",
				dai->platform_name);
                        return -ENODEV;
		}
		np = of_parse_phandle(cdev->of_node, "asoc-platform",
				      index);
		if (!np) {
			dev_err(pdev,
				"retrieving phandle for platform %s failed\n",
				dai->platform_name);
                        return -ENODEV;
		}
		dai->platform_of_node = np;
		dai->platform_name = NULL;
	}

	/* assumption:
	 * each module can be used with single sound card at a time
	 */
	index = connection->bundle->id;
	snprintf(codec_name, NAME_SIZE, "%s.%d", "gbaudio-codec", index);
	snprintf(codec_dai_name, NAME_SIZE, "%s.%d", "gbcodec_pcm", index);

	strlcpy(gbmodule->codec_name, "gbaudio-codec", NAME_SIZE);
	strlcpy(gbmodule->card_name, card_name, NAME_SIZE);
	dai->codec_name = codec_name;
	dai->codec_dai_name = codec_dai_name;
	gbmodule->index = index;
	gbmodule->mgmt_cport = connection->hd_cport_id;
	gbmodule->mgmt_connection = connection;
	gbmodule->dai_link = dai;
	gbmodule->num_dai_links = 1;

	/* register module1 */
	ret = gbaudio_register_module(gbmodule);
	if (ret) {
		dev_err(pdev, "Module initialization failed, %d\n", ret);
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
		if (module->mgmt_cport == connection->hd_cport_id) {
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
