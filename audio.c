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

#define NUM_MODULES	1

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

static struct snd_soc_dai_link gbaudio_dailink[] = {
	{
		.name = "GB PRI_MI2S_RX",
		.stream_name = "Primary MI2S Playback",
		.platform_name = "qcom,msm-pcm-routing.41",
		.cpu_dai_name = "qcom,msm-dai-q6-mi2s-prim.205",
		.codec_name = "gbaudio-codec.0",
		.codec_dai_name = "gbcodec_pcm.0",
		.no_pcm = 1,
		.ignore_suspend = 1,
	},
};

static int gbaudio_probe(struct platform_device *pdev)
{
	int ret, i;
	struct gbaudio_priv *gbaudio;
	struct gbaudio_module_info *gbmodule[NUM_MODULES];
	/*
	struct device_node *np;
	struct device *cdev = &pdev->dev;
	*/

	gbaudio = devm_kzalloc(&pdev->dev, sizeof(struct gbaudio_priv),
			      GFP_KERNEL);
	if (!gbaudio)
		return -ENOMEM;

	platform_set_drvdata(pdev, gbaudio);

	/* register module(s) */

	for (i = 0; i < NUM_MODULES; i++) {
		gbmodule[i] = devm_kzalloc(&pdev->dev,
					   sizeof(struct gbaudio_module_info),
					   GFP_KERNEL);
	if (!gbmodule[i])
		return -ENOMEM;

	/* assumption:
	 * each module can be used with single sound card at a time
	 */
	strlcpy(gbmodule[i]->codec_name, "gbaudio-codec", NAME_SIZE);
	strlcpy(gbmodule[i]->card_name, "msm8994-tomtom-mtp-snd-card",
		NAME_SIZE);
	gbmodule[i]->index = i;
	gbmodule[i]->mgmt_cport = i;
	gbmodule[i]->dai_link = &gbaudio_dailink[i];
	gbmodule[i]->num_dai_links = 1;

	/* register module1 */
	ret = gbaudio_register_module(gbmodule[i]);
	if (ret) {
		dev_err(&pdev->dev, "Module initialization failed, %d\n", ret);
		return ret;
	}
	gbaudio->module_count++;
	}

	return 0;
}

static int gbaudio_remove(struct platform_device *pdev)
{
	struct gbaudio_module_info *module, *_module;
	struct gbaudio_priv *gbaudio = platform_get_drvdata(pdev);

	/* stop active streams */

	/* unregister all modules */
	list_for_each_entry_safe(module, _module, &module_list, list) {
		gbaudio_unregister_module(module);
		gbaudio->module_count--;
	}

	return 0;
}

static const struct of_device_id gbaudio_of_match[] = {
	{ .compatible = "greybus,audio", },
	{},
};

static struct platform_driver gbaudio_driver = {
	.driver		= {
		.name		= "greybus-audio",
		.owner		= THIS_MODULE,
		.of_match_table = gbaudio_of_match,
	},
	.probe		= gbaudio_probe,
	.remove		= gbaudio_remove,
};
module_platform_driver(gbaudio_driver);

MODULE_DESCRIPTION("Greybus Audio protocol driver");
MODULE_AUTHOR("Vaibhav Agarwal <vaibhav.agarwal@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("greybus:audio-protocol");
