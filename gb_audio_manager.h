#ifndef _GB_AUDIO_MANAGER_H_
#define _GB_AUDIO_MANAGER_H_

#include "gb_audio_module.h"

#define GB_AUDIO_MANAGER_NAME "gb_audio_manager"

/*
 * Creates a new gb_audio_module, using the specified descriptor.
 *
 * Returns a negative result on error, or the id of the newly created module.
 *
 */
int gb_audio_manager_add(struct gb_audio_module_descriptor *desc);

/*
 * Removes a connected gb_audio_module for the specified ID.
 *
 * Returns zero on success, or a negative value on error.
 */
int gb_audio_manager_remove(int id);

/*
 * Removes all connected gb_audio_modules
 *
 * Returns zero on success, or a negative value on error.
 */
void gb_audio_manager_remove_all(void);

/*
 * Retrieves a gb_audio_module for the specified id.
 * Returns the gb_audio_module structure,
 * or NULL if there is no module with the specified ID.
 */
struct gb_audio_module* gb_audio_manager_get_module(int id);

/*
 * Decreases the refcount of the module, obtained by the get function.
 * Modules are removed via gb_audio_manager_remove
 */
void gb_audio_manager_put_module(struct gb_audio_module *module);

/*
 * Dumps the module for the specified id
 * Return 0 on success
 */
int gb_audio_manager_dump_module(int id);

/*
 * Dumps all connected modules
 */
void gb_audio_manager_dump_all(void);

#endif /* _GB_AUDIO_MANAGER_H_ */
