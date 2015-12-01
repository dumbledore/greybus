#ifndef _GB_AUDIO_MODULE_PRIVATE_H_
#define _GB_AUDIO_MODULE_PRIVATE_H_

#include "gb_audio_module.h"

int gb_audio_module_create(
	struct gb_audio_module **module,
	struct kset *manager_kset,
	int id, struct gb_audio_module_descriptor *desc);

/* destroyed via kobject_put */

void gb_audio_module_dump(struct gb_audio_module *module);

#endif /* _GB_AUDIO_MODULE_PRIVATE_H_ */