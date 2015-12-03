#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rwlock.h>

#include "gb_audio_manager.h"
#include "gb_audio_manager_sysfs.h"
#include "gb_audio_module_private.h"

static struct kset *manager_kset;

static LIST_HEAD(modules_list);
static DEFINE_RWLOCK(modules_lock);

static int current_module_id = 0;

/* helpers */
static struct gb_audio_module* gb_audio_manager_get_locked(int id)
{
	struct gb_audio_module *module;

	if (id < 0)
		return NULL;

	list_for_each_entry(module, &modules_list, list) {
		if (module->id == id)
			return module;
	}

	return NULL;
}

/* public API */
int gb_audio_manager_add(struct gb_audio_module_descriptor *desc)
{
	struct gb_audio_module *module;
	unsigned long flags;
	int err;

	err = gb_audio_module_create(&module, manager_kset, current_module_id++, desc);
	if (err)
		return err;

	/* Add it to the list */
	write_lock_irqsave(&modules_lock, flags);
	list_add_tail(&module->list, &modules_list);
	write_unlock_irqrestore(&modules_lock, flags);

	return module->id;
}
EXPORT_SYMBOL_GPL(gb_audio_manager_add);

int gb_audio_manager_remove(int id)
{
	struct gb_audio_module *module;
	unsigned long flags;

	write_lock_irqsave(&modules_lock, flags);

	module = gb_audio_manager_get_locked(id);
	if (!module) {
		write_unlock_irqrestore(&modules_lock, flags);
		return -EINVAL;
	}

	list_del(&module->list);
	kobject_put(&module->kobj);
	write_unlock_irqrestore(&modules_lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(gb_audio_manager_remove);

void gb_audio_manager_remove_all(void)
{
	struct gb_audio_module *module, *next;
	int is_empty = 1;
	unsigned long flags;

	write_lock_irqsave(&modules_lock, flags);

	list_for_each_entry_safe(module, next, &modules_list, list) {
		list_del(&module->list);
		kobject_put(&module->kobj);
	}

	is_empty = list_empty(&modules_list);

	write_unlock_irqrestore(&modules_lock, flags);

	if (!is_empty) {
		printk(KERN_WARNING "Not all nodes were deleted\n");
	}
}
EXPORT_SYMBOL_GPL(gb_audio_manager_remove_all);

struct gb_audio_module* gb_audio_manager_get_module(int id)
{
	struct gb_audio_module *module;
	unsigned long flags;

	read_lock_irqsave(&modules_lock, flags);
	module = gb_audio_manager_get_locked(id);
	kobject_get(&module->kobj);
	read_unlock_irqrestore(&modules_lock, flags);
	return module;
}
EXPORT_SYMBOL_GPL(gb_audio_manager_get_module);

void gb_audio_manager_put_module(struct gb_audio_module *module)
{
	kobject_put(&module->kobj);
}
EXPORT_SYMBOL_GPL(gb_audio_manager_put_module);

int gb_audio_manager_dump_module(int id)
{
	struct gb_audio_module *module;
	unsigned long flags;

	read_lock_irqsave(&modules_lock, flags);
	module = gb_audio_manager_get_locked(id);
	read_unlock_irqrestore(&modules_lock, flags);

	if (!module)
		return -EINVAL;

	gb_audio_module_dump(module);
	return 0;
}
EXPORT_SYMBOL_GPL(gb_audio_manager_dump_module);

void gb_audio_manager_dump_all(void)
{
	struct gb_audio_module *module;
	int count = 0;
	unsigned long flags;

	read_lock_irqsave(&modules_lock, flags);
	list_for_each_entry(module, &modules_list, list) {
		gb_audio_module_dump(module);
		count++;
	}
	read_unlock_irqrestore(&modules_lock, flags);

	printk(KERN_INFO "Number of connected modules: %d\n", count);
}
EXPORT_SYMBOL_GPL(gb_audio_manager_dump_all);

/*
 * module init/deinit
 */
static int __init manager_init(void)
{
	manager_kset = kset_create_and_add(GB_AUDIO_MANAGER_NAME, NULL, kernel_kobj);
	if (!manager_kset)
		return -ENOMEM;

#ifdef GB_AUDIO_MANAGER_SYSFS
	gb_audio_manager_sysfs_init(&manager_kset->kobj);
#endif

	return 0;
}

static void __exit manager_exit(void)
{
	gb_audio_manager_remove_all();
	kset_unregister(manager_kset);
}

module_init(manager_init);
module_exit(manager_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Svetlin Ankov <ankov_svetlin@projectara.com>");