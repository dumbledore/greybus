#include <linux/slab.h>

#include "gb_audio_module_private.h"
#include "gb_audio_module.h"
#include "gb_audio_manager.h"

#define to_gb_audio_module_attr(x) container_of(x, struct gb_audio_module_attribute, attr)
#define to_gb_audio_module(x) container_of(x, struct gb_audio_module, kobj)

struct gb_audio_module_attribute {
	struct attribute attr;
	ssize_t (*show)(struct gb_audio_module *module, struct gb_audio_module_attribute *attr, char *buf);
	ssize_t (*store)(struct gb_audio_module *module, struct gb_audio_module_attribute *attr, const char *buf, size_t count);
};

static ssize_t gb_audio_module_attr_show(
	struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct gb_audio_module_attribute *attribute;
	struct gb_audio_module *module;

	attribute = to_gb_audio_module_attr(attr);
	module = to_gb_audio_module(kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(module, attribute, buf);
}

static ssize_t gb_audio_module_attr_store(
	struct kobject *kobj, struct attribute *attr, const char *buf, size_t len)
{
	struct gb_audio_module_attribute *attribute;
	struct gb_audio_module *module;

	attribute = to_gb_audio_module_attr(attr);
	module = to_gb_audio_module(kobj);

	if (!attribute->store)
		return -EIO;

	return attribute->store(module, attribute, buf, len);
}

static const struct sysfs_ops gb_audio_module_sysfs_ops = {
	.show = gb_audio_module_attr_show,
	.store = gb_audio_module_attr_store,
};

static void gb_audio_module_release(struct kobject *kobj)
{
	struct gb_audio_module *module = to_gb_audio_module(kobj);
	printk(KERN_INFO "Destroying audio module #%d\n", module->id);
	/* TODO -> delete from list */
	kfree(module);
}

static ssize_t gb_audio_module_name_show(
	struct gb_audio_module *module,
	struct gb_audio_module_attribute *attr, char *buf)
{
	return sprintf(buf, "%s", module->desc.name);
}

static struct gb_audio_module_attribute gb_audio_module_name_attribute =
	__ATTR(name, 0664, gb_audio_module_name_show, NULL);

static ssize_t gb_audio_module_slot_show(
	struct gb_audio_module *module,
	struct gb_audio_module_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", module->desc.slot);
}

static struct gb_audio_module_attribute gb_audio_module_slot_attribute =
	__ATTR(slot, 0664, gb_audio_module_slot_show, NULL);

static ssize_t gb_audio_module_vid_show(
	struct gb_audio_module *module,
	struct gb_audio_module_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", module->desc.vid);
}

static struct gb_audio_module_attribute gb_audio_module_vid_attribute =
	__ATTR(vid, 0664, gb_audio_module_vid_show, NULL);

static ssize_t gb_audio_module_pid_show(
	struct gb_audio_module *module,
	struct gb_audio_module_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", module->desc.pid);
}

static struct gb_audio_module_attribute gb_audio_module_pid_attribute =
	__ATTR(pid, 0664, gb_audio_module_pid_show, NULL);

static ssize_t gb_audio_module_cport_show(
	struct gb_audio_module *module,
	struct gb_audio_module_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", module->desc.cport);
}

static struct gb_audio_module_attribute gb_audio_module_cport_attribute =
	__ATTR(cport, 0664, gb_audio_module_cport_show, NULL);

static ssize_t gb_audio_module_devices_show(
	struct gb_audio_module *module,
	struct gb_audio_module_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%X", module->desc.devices);
}

static struct gb_audio_module_attribute gb_audio_module_devices_attribute =
	__ATTR(devices, 0664, gb_audio_module_devices_show, NULL);

static struct attribute *gb_audio_module_default_attrs[] = {
	&gb_audio_module_name_attribute.attr,
	&gb_audio_module_slot_attribute.attr,
	&gb_audio_module_vid_attribute.attr,
	&gb_audio_module_pid_attribute.attr,
	&gb_audio_module_cport_attribute.attr,
	&gb_audio_module_devices_attribute.attr,
	NULL,   /* need to NULL terminate the list of attributes */
};

static struct kobj_type gb_audio_module_type = {
	.sysfs_ops = &gb_audio_module_sysfs_ops,
	.release = gb_audio_module_release,
	.default_attrs = gb_audio_module_default_attrs,
};

static void send_add_uevent(struct gb_audio_module *module)
{
	char name_string[128];
	char slot_string[64];
	char vid_string[64];
	char pid_string[64];
	char cport_string[64];
	char devices_string[64];

	char *envp[] = {
		name_string,
		slot_string,
		vid_string,
		pid_string,
		cport_string,
		devices_string,
		NULL
	};

	snprintf(name_string, 128, "NAME=%s", module->desc.name);
	snprintf(slot_string, 64, "SLOT=%d", module->desc.slot);
	snprintf(vid_string, 64, "VID=%d", module->desc.vid);
	snprintf(pid_string, 64, "PID=%d", module->desc.pid);
	snprintf(cport_string, 64, "CPORT=%d", module->desc.cport);
	snprintf(devices_string, 64, "DEVICES=0x%X", module->desc.devices);

	kobject_uevent_env(&module->kobj, KOBJ_ADD, envp);
}

int gb_audio_module_create(
	struct gb_audio_module **module,
	struct kset *manager_kset,
	int id, struct gb_audio_module_descriptor *desc)
{
	int err;
	struct gb_audio_module *m;

	m = kzalloc(sizeof(*m), GFP_ATOMIC);
	if (!m)
		return -ENOMEM;

	/* Initialize the node */
	INIT_LIST_HEAD(&m->list);

	/* Set the module id */
	m->id = id;

	/* Copy the provided descriptor */
	memcpy(&m->desc, desc, sizeof(*desc));

	/* set the kset */
	m->kobj.kset = manager_kset;

	/*
	 * Initialize and add the kobject to the kernel.  All the default files
	 * will be created here.  As we have already specified a kset for this
	 * kobject, we don't have to set a parent for the kobject, the kobject
	 * will be placed beneath that kset automatically.
	 */
	err = kobject_init_and_add(&m->kobj, &gb_audio_module_type, NULL, "%d", id);
	if (err) {
		printk(KERN_ERR "failed initializing kobject for audio module #%d\n", id);
		kobject_put(&m->kobj);
		return err;
	}

	/*
	 * Notify the object was created
	 */
	send_add_uevent(m);

	*module = m;
	printk(KERN_INFO "Created audio module #%d\n", id);
	return 0;
}

void gb_audio_module_dump(struct gb_audio_module *module)
{
	printk(KERN_INFO "audio module #%d name=%s slot=%d vid=%d pid=%d cport=%d devices=0x%X\n",
		module->id,
		module->desc.name,
		module->desc.slot,
		module->desc.vid,
		module->desc.pid,
		module->desc.cport,
		module->desc.devices);
}
