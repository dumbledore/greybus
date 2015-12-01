#ifndef _GB_AUDIO_MODULE_H_
#define _GB_AUDIO_MODULE_H_

#include <linux/kobject.h>
#include <linux/list.h>

#define GB_AUDIO_MODULE_NAME_LEN 64
#define GB_AUDIO_MODULE_NAME_LEN_SSCANF "63"


struct gb_audio_module_descriptor {
	char name[GB_AUDIO_MODULE_NAME_LEN];
	int slot;
	int vid;
	int pid;
	int cport;
	unsigned int devices;
};

struct gb_audio_module {
	struct kobject kobj;
	struct list_head list;
	int id;
	struct gb_audio_module_descriptor desc;
};

#endif /* _GB_AUDIO_MODULE_H_ */