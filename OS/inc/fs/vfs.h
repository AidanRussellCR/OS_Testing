#pragma once
#include <stddef.h>

typedef void (*vfs_list_cb_t)(const char* name, int is_dir, void* user);

typedef enum {
	VFS_OK = 0,
	VFS_ERR_NOT_FOUND,
	VFS_ERR_EXISTS,
	VFS_ERR_NOT_DIR,
	VFS_ERR_IS_DIR,
	VFS_ERR_NAME_INVALID,
	VFS_ERR_NO_MEM,
	VFS_ERR_BUSY
} vfs_status_t;

void vfs_init(void);

// Working directory
void vfs_pwd(char* out, size_t cap);
vfs_status_t vfs_cd(const char* name);

// Directories
vfs_status_t vfs_mkdir(const char* name);

// Files
vfs_status_t vfs_fab(const char* filename);
vfs_status_t vfs_insp(const char* filename, const char** out_text);
vfs_status_t vfs_carve(const char* filename, const char* text);

vfs_status_t vfs_burn(const char* filename);
vfs_status_t vfs_load(void);
vfs_status_t vfs_save(void);

vfs_status_t vfs_learn(const char* filename);
vfs_status_t vfs_is_learned(const char* filename, int* out_learned);

void vfs_shop(vfs_list_cb_t cb, void* user);
int vfs_is_dirty(void);

// Scripts
typedef void (*vfs_spell_cb_t)(const char* name, void* user);

void vfs_grimoire(vfs_spell_cb_t cb, void* user);
