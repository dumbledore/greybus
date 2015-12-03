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
#include "audio_codec.h"
#include "gb_audio_manager.h"

static LIST_HEAD(module_list);

static struct snd_soc_dai_link gbaudio_dailink = {
	.name = "PRI_MI2S_RX",
	.stream_name = "Primary MI2S Playback",
	.platform_name = "msm-pcm-routing",
	.cpu_dai_name = "msm-dai-q6-mi2s.0",
	.no_pcm = 1,
	.ignore_suspend = 1,
};

static void gbaudio_remove_dailink(struct gbaudio_module_info *info)
{
	struct snd_soc_dai_link *dai_link = &gbaudio_dailink;

	snd_soc_remove_dai_link(info->card_name, dai_link->name);
	info->num_dai_links = 0;
}

int gbaudio_register_module(struct gbaudio_module_info *info)
{
	int ret, index, i;
	char prefix_name[NAME_SIZE];
	char dai_link_name[NAME_SIZE];
	struct snd_soc_dai_link *dai;
	struct snd_soc_card *card;
	struct device *cdev;
        struct device_node *np;
	struct device *dev = info->dev;
	struct gb_audio_manager_module_descriptor desc;

	snprintf(prefix_name, NAME_SIZE, "GB %d", info->dev_id);
	ret = snd_soc_update_name_prefix(info->codec_name, prefix_name);
	if (ret) {
		dev_err(dev, "Failed to set prefix name\n");
		return ret;
	}

	/* update DAI link structure */
	strlcpy(info->card_name, "msm8994-tomtom-mtp-snd-card", NAME_SIZE);
	dai = &gbaudio_dailink;
	card = snd_soc_get_card(info->card_name);
	if (!card) {
		dev_err(dev, "Unable to find %s soc card\n",
			info->card_name);
		return -ENODEV;
	}
	cdev = card->dev;

	/* populate cpu_of_node for snd card dai links */
	if (dai->cpu_dai_name && !dai->cpu_of_node) {
                index = of_property_match_string(cdev->of_node,
                                                 "asoc-cpu-names",
                                                 dai->cpu_dai_name);
		if (index < 0) {
			dev_err(dev, "No match found for cpu_dai name: %s\n",
				dai->cpu_dai_name);
                        return -ENODEV;
		}
		np = of_parse_phandle(cdev->of_node, "asoc-cpu",
				      index);
		if (!np) {
			dev_err(dev, "retrieving phandle for cpu dai %s failed\n",
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
			dev_err(dev, "No match found for platform name: %s\n",
				dai->platform_name);
                        return -ENODEV;
		}
		np = of_parse_phandle(cdev->of_node, "asoc-platform",
				      index);
		if (!np) {
			dev_err(dev,
				"retrieving phandle for platform %s failed\n",
				dai->platform_name);
                        return -ENODEV;
		}
		dai->platform_of_node = np;
		dai->platform_name = NULL;
	}
	dai->codec_name = info->codec_name;
	info->num_dai_links = info->num_dais;

	for (i = 0; i < info->num_dai_links; i++) {
		snprintf(dai_link_name, NAME_SIZE, "GB %d.%d PRI_MI2S_RX",
			 info->dev_id, i);
		dai->name = dai_link_name;
		dai->codec_dai_name = info->dai_names[i];

		/* register DAI link */
		ret = snd_soc_add_dai_link(info->card_name, dai);
		if (ret) {
			dev_err(dev, "%s: dai registration failed,%d\n",
				dai->name, ret);
			goto err_dai_link;
		}
	}

	/* add this module to module_list */
	list_add(&info->list, &module_list);

	/* prepare for the audio manager */
	strncpy(desc.name, info->codec_name,
		GB_AUDIO_MANAGER_MODULE_NAME_LEN); /* todo */
	desc.slot = 1; /* todo */
	desc.vid = 2; /* todo */
	desc.pid = 3; /* todo */
	desc.cport = info->dev_id;
	desc.devices = 0x2; /* todo */
	info->manager_id = gb_audio_manager_add(&desc);

	return 0;

err_dai_link:
	for (; i > 0; i--) {
		snprintf(dai_link_name, NAME_SIZE, "GB %d.%d PRI_MI2S_RX",
			 info->dev_id, i-1);
		snd_soc_remove_dai_link(info->card_name, dai_link_name);
	}
	info->num_dai_links = 0;
	return ret;
}

int gbaudio_unregister_module(struct gbaudio_module_info *gbmodule)
{
	int ret;
	char prefix_name[NAME_SIZE];
	struct device *dev = gbmodule->dev;

	snprintf(prefix_name, NAME_SIZE, "GB %d", gbmodule->dev_id);
	ret = snd_soc_update_name_prefix(gbmodule->codec_name, prefix_name);
	if (ret)
		dev_err(dev, "Failed to set prefix name\n");

	/* remove dai_links if any */
	if (gbmodule->num_dai_links)
		gbaudio_remove_dailink(gbmodule);

	/* notify the audio manager */
	gb_audio_manager_remove(gbmodule->manager_id);

	/* remove entry from the list */
	list_del(&gbmodule->list);

	return 0;
}
