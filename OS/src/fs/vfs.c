#include <stddef.h>
#include <stdint.h>
#include "fs/vfs.h"
#include "mm/heap.h"
#include "lib/str.h"
#include "drivers/ata.h"

#define VFS_LBA_BASE 2048u
#define VFS_MAGIC 0x50534631u
#define VFS_VERSION 2u

typedef enum { NODE_DIR = 1, NODE_FILE = 2 } node_type_t;

typedef struct vfs_node {
	node_type_t type;
	char name[32];
	uint8_t flags; // identify learned/executable spell
	struct vfs_node* parent;
	struct vfs_node* sibling_next;
	struct vfs_node* child_head;
	char* file_data;
	size_t file_size;
} vfs_node_t;

static int g_dirty = 0;
int vfs_is_dirty(void) { return g_dirty; }
static void vfs_mark_dirty(void) { g_dirty = 1; }
static void vfs_mark_clean(void) { g_dirty = 0; }

static vfs_node_t* g_root = 0;
static vfs_node_t* g_cwd = 0;

static int name_valid(const char* s) {
	if (!s || s[0] == '\0') return 0;

	for (size_t i = 0; s[i]; i++) {
		char c = s[i];
		if (c == '/' || c == '\\' || c == ':' || c == ' ') return 0;
	}
	return 1;
}

// check if file is .ms (magic spell/script file)
static int has_ms_extension(const char* name) {
	size_t n = kstrlen(name);
	if (n < 3) return 0;
	return name[n - 3] == '.' && name[n - 2] == 'm' && name[n - 1] == 's';
}

static vfs_node_t* node_alloc(node_type_t t, const char* name, vfs_node_t* parent) {
	vfs_node_t* n = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
	if (!n) return 0;
	kmemset(n, 0, sizeof(vfs_node_t));
	n->type = t;
	n->parent = parent;

	kmemset(n->name, 0, sizeof(n->name));
	if (name) {
		for (size_t i = 0; i + 1 < sizeof(n->name) && name[i]; i++) {
			n->name[i] = name[i];
		}
		n->name[sizeof(n->name) - 1] = '\0';
	}

	return n;
}

static void link_child(vfs_node_t* dir, vfs_node_t* child) {
	child->sibling_next = dir->child_head;
	dir->child_head = child;
}

static vfs_node_t* find_child(vfs_node_t* dir, const char* name) {
	for (vfs_node_t* c = dir->child_head; c; c = c->sibling_next) {
		if (streq(c->name, name)) return c;
	}
	return 0;
}

static vfs_node_t* find_base_dir(void) {
	if (!g_root) return 0;
	vfs_node_t* root = find_child(g_root, "root");
	if (!root) return 0;
	return find_child(root, "base");
}

static vfs_status_t remove_child(vfs_node_t* dir, vfs_node_t* target) {
	vfs_node_t* prev = 0;
	for (vfs_node_t* c = dir->child_head; c; c = c->sibling_next) {
		if (c == target) {
			if (prev) prev->sibling_next = c->sibling_next;
			else dir->child_head = c->sibling_next;
			return VFS_OK;
		}
		prev = c;
	}
	return VFS_ERR_NOT_FOUND;
}

static void free_subtree(vfs_node_t* n) {
	vfs_node_t* c = n->child_head;
	while (c) {
		vfs_node_t* next = c->sibling_next;
		free_subtree(c);
		c = next;
	}
	if (n->type == NODE_FILE && n->file_data) {
		kfree(n->file_data);
		n->file_data = 0;
		n->file_size = 0;
	}
	kfree(n);
}

void vfs_init(void) {
	// build -- /P/root/base
	g_root = node_alloc(NODE_DIR, "P", 0);
	vfs_node_t* root = node_alloc(NODE_DIR, "root", g_root);
	vfs_node_t* base = node_alloc(NODE_DIR, "base", root);
	if (!g_root || !root || !base) {
		return;
	}
	link_child(g_root, root);
	link_child(root, base);
	g_cwd = base;
}

static size_t append_path_piece(char* out, size_t cap, size_t pos, const char* s) {
	if (pos >= cap) return pos;
	for (size_t i = 0; s[i] && pos + 1 < cap; i++) out[pos++] = s[i];
	out[pos] = '\0';
	return pos;
}

void vfs_pwd(char* out, size_t cap) {
	if (!out || cap == 0) return;
	out[0] = '\0';
	if (!g_cwd) {
		append_path_piece(out, cap, 0, "/");
		return;
	}

	vfs_node_t* chain[32];
	int n = 0;
	vfs_node_t* cur = g_cwd;
	while (cur && n < 32) {
		chain[n++] = cur;
		cur = cur->parent;
	}

	size_t pos = 0;
	pos = append_path_piece(out, cap, pos, "/");
	for (int i = n - 1; i >= 0; i--) {
		pos = append_path_piece(out, cap, pos, chain[i]->name);
		if (i != 0) pos = append_path_piece(out, cap, pos, "/");
	}
}

vfs_status_t vfs_cd(const char* name) {
	if (!g_cwd) return VFS_ERR_NOT_FOUND;
	if (!name || name[0] == '\0') return VFS_ERR_NAME_INVALID;
	if (streq(name, "/")) {
		g_cwd = g_root;
		return VFS_OK;
	}
	if (streq(name, "..")) {
		if (g_cwd->parent) g_cwd = g_cwd->parent;
		return VFS_OK;
	}

	vfs_node_t* c = find_child(g_cwd, name);
	if (!c) return VFS_ERR_NOT_FOUND;
	if (c->type != NODE_DIR) return VFS_ERR_NOT_DIR;
	g_cwd = c;
	return VFS_OK;
}

vfs_status_t vfs_mkdir(const char* name) {
	if (!g_cwd) return VFS_ERR_NOT_FOUND;
	if (!name_valid(name)) return VFS_ERR_NAME_INVALID;
	if (find_child(g_cwd, name)) return VFS_ERR_EXISTS;

	vfs_node_t* d = node_alloc(NODE_DIR, name, g_cwd);
	if (!d) return VFS_ERR_NO_MEM;
	link_child(g_cwd, d);
	vfs_mark_dirty();
	return VFS_OK;
}

vfs_status_t vfs_fab(const char* filename) {
	if (!g_cwd) return VFS_ERR_NOT_FOUND;
	if (!name_valid(filename)) return VFS_ERR_NAME_INVALID;
	if (find_child(g_cwd, filename)) return VFS_ERR_EXISTS;

	vfs_node_t* f = node_alloc(NODE_FILE, filename, g_cwd);
	if (!f) return VFS_ERR_NO_MEM;
	link_child(g_cwd, f);
	vfs_mark_dirty();
	return VFS_OK;
}

vfs_status_t vfs_insp(const char* filename, const char** out_text) {
	if (out_text) *out_text = 0;
	if (!g_cwd) return VFS_ERR_NOT_FOUND;
	vfs_node_t* f = find_child(g_cwd, filename);
	if (!f) return VFS_ERR_NOT_FOUND;
	if (f->type != NODE_FILE) return VFS_ERR_IS_DIR;
	if (out_text) *out_text = (f->file_data) ? f->file_data : "";
	return VFS_OK;
}

vfs_status_t vfs_carve(const char* filename, const char* text) {
	if (!g_cwd) return VFS_ERR_NOT_FOUND;
	vfs_node_t* f = find_child(g_cwd, filename);
	if (!f) return VFS_ERR_NOT_FOUND;
	if (f->type != NODE_FILE) return VFS_ERR_IS_DIR;

	const char* src = text ? text : "";
	size_t n = kstrlen(src);

	char* buf = (char*)kmalloc(n + 1);
	if (!buf) return VFS_ERR_NO_MEM;

	for (size_t i = 0; i < n; i++) buf[i] = src[i];
	buf[n] = '\0';

	if (f->file_data) {
		kfree(f->file_data);
		f->file_data = 0;
		f->file_size = 0;
	}

	f->file_data = buf;
	f->file_size = n;
	vfs_mark_dirty();
	return VFS_OK;
}

vfs_status_t vfs_burn(const char* filename) {
	if (!g_cwd) return VFS_ERR_NOT_FOUND;
	vfs_node_t* n = find_child(g_cwd, filename);
	if (!n) return VFS_ERR_NOT_FOUND;
	if (n->type == NODE_DIR) {
		if (n->child_head) return VFS_ERR_BUSY;
	}
	remove_child(g_cwd, n);
	n->parent = 0;
	n->sibling_next = 0;
	free_subtree(n);
	vfs_mark_dirty();
	return VFS_OK;
}

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint16_t version;
	uint16_t reserved;
	uint32_t node_count;
	uint32_t data_bytes;
	uint32_t root_index;
	uint32_t cwd_index;
	uint32_t total_sectors;
	uint32_t checksum;
} vfs_superblock_t;

typedef struct __attribute__((packed)) {
	uint8_t type;
	uint8_t flags;
	char name[32];
	int32_t parent;
	int32_t first_child;
	int32_t next_sibling;
	uint32_t file_off;
	uint32_t file_len;
} vfs_disk_node_t;

static int build_index_table(vfs_node_t* n, vfs_node_t** out, int cap, int* count) {
	if (!n) return 0;
	if (*count >= cap) return 0;
	out[*count] = n;
	(*count)++;
	for (vfs_node_t* ch = n->child_head; ch; ch = ch->sibling_next) {
		if (!build_index_table(ch, out, cap, count)) return 0;
	}
	return 1;
}

static int find_index(vfs_node_t** arr, int n, vfs_node_t* target) {
	for (int i = 0; i < n; i++) if (arr[i] == target) return i;
	return -1;
}

static uint32_t bytes_to_sectors(uint32_t bytes) {
	return (bytes + ATA_SECTOR_SIZE - 1u) / ATA_SECTOR_SIZE;
}

vfs_status_t vfs_save(void) {
	if (!g_root) return VFS_ERR_NOT_FOUND;

	enum { MAX_NODES_SNAPSHOT = 1024 };
	vfs_node_t** nodes = (vfs_node_t**)kmalloc(sizeof(vfs_node_t*) * MAX_NODES_SNAPSHOT);
	if (!nodes) return VFS_ERR_NO_MEM;
	
	int node_count = 0;
	if (!build_index_table(g_root, nodes, MAX_NODES_SNAPSHOT, &node_count)) {
		return VFS_ERR_NO_MEM;
	}

	uint32_t data_bytes = 0;
	for (int i = 0; i < node_count; i++) {
		if (nodes[i]->type == NODE_FILE && nodes[i]->file_data && nodes[i]->file_size) {
			data_bytes += (uint32_t)nodes[i]->file_size;
		}
	}

	uint32_t node_table_bytes = (uint32_t)node_count * (uint32_t)sizeof(vfs_disk_node_t);
	uint32_t node_table_sectors = bytes_to_sectors(node_table_bytes);
	uint32_t data_sectors = bytes_to_sectors(data_bytes);
	uint32_t total_sectors = 1u + node_table_sectors + data_sectors;

	int root_index = find_index(nodes, node_count, g_root);
	int cwd_index  = find_index(nodes, node_count, g_cwd);

	if (root_index < 0) root_index = 0;
	if (cwd_index < 0) cwd_index = 0;

	vfs_superblock_t sb;
	kmemset(&sb, 0, sizeof(sb));
	sb.magic = VFS_MAGIC;
	sb.version = (uint16_t)VFS_VERSION;
	sb.node_count = (uint32_t)node_count;
	sb.data_bytes = data_bytes;
	sb.root_index = (uint32_t)root_index;
	sb.cwd_index = (uint32_t)cwd_index;
	sb.total_sectors = total_sectors;

	uint8_t sector[ATA_SECTOR_SIZE];
	kmemset(sector, 0, ATA_SECTOR_SIZE);

	for (size_t i = 0; i < sizeof(sb); i++) sector[i] = ((const uint8_t*)&sb)[i];

	if (ata_pio_write28(VFS_LBA_BASE, sector) != 0) return VFS_ERR_BUSY;

	uint32_t data_cursor = 0;
	uint32_t lba = VFS_LBA_BASE + 1u;

	uint32_t written_nodes = 0;
	for (uint32_t s = 0; s < node_table_sectors; s++) {
		kmemset(sector, 0, ATA_SECTOR_SIZE);
		uint32_t off = s * ATA_SECTOR_SIZE;
		for (uint32_t j = 0; j < ATA_SECTOR_SIZE; j++) {
			uint32_t pos = off + j;
			if (pos >= node_table_bytes) break;

			uint32_t ni = pos / sizeof(vfs_disk_node_t);
			uint32_t bi = pos % sizeof(vfs_disk_node_t);

			if (ni >= (uint32_t)node_count) break;

			vfs_node_t* mn = nodes[ni];
			vfs_disk_node_t dn;
			kmemset(&dn, 0, sizeof(dn));
			dn.type = (uint8_t)mn->type;
			dn.flags = mn->flags;

			kmemset(dn.name, 0, sizeof(dn.name));
			for (int k = 0; k < 31; k++) dn.name[k] = mn->name[k];
			dn.name[31] = '\0';

			dn.parent = (mn->parent) ? find_index(nodes, node_count, mn->parent) : -1;
			dn.first_child = (mn->child_head) ? find_index(nodes, node_count, mn->child_head) : -1;
			dn.next_sibling = (mn->sibling_next) ? find_index(nodes, node_count, mn->sibling_next) : -1;

			if (mn->type == NODE_FILE && mn->file_data && mn->file_size) {
				dn.file_off = data_cursor;
				dn.file_len = (uint32_t)mn->file_size;
			} else {
				dn.file_off = 0;
				dn.file_len = 0;
			}

			sector[j] = ((uint8_t*)&dn)[bi];

			if (bi == sizeof(vfs_disk_node_t) - 1) {
				if (mn->type == NODE_FILE && mn->file_data && mn->file_size) {
					data_cursor += (uint32_t)mn->file_size;
				}
				written_nodes++;
			}
		}

		if (ata_pio_write28(lba + s, sector) != 0) return VFS_ERR_BUSY;
	}

	uint32_t data_lba = VFS_LBA_BASE + 1u + node_table_sectors;
	uint32_t blob_written = 0;

	for (uint32_t s = 0; s < data_sectors; s++) {
		kmemset(sector, 0, ATA_SECTOR_SIZE);
		uint32_t fill = 0;

		while (fill < ATA_SECTOR_SIZE && blob_written < data_bytes) {
			uint32_t acc = 0;
			vfs_node_t* src = 0;
			uint32_t src_off = 0;

			for (int i = 0; i < node_count; i++) {
				if (nodes[i]->type != NODE_FILE || !nodes[i]->file_data || !nodes[i]->file_size) continue;
				uint32_t len = (uint32_t)nodes[i]->file_size;
				if (blob_written >= acc && blob_written < acc + len) {
					src = nodes[i];
					src_off = blob_written - acc;
					break;
				}
				acc += len;
			}

			if (!src) break;

			uint32_t remaining = (uint32_t)src->file_size - src_off;
			uint32_t to_copy = ATA_SECTOR_SIZE - fill;
			if (to_copy > remaining) to_copy = remaining;

			for (uint32_t k = 0; k < to_copy; k++) {
				sector[fill + k] = (uint8_t)src->file_data[src_off + k];
			}

			fill += to_copy;
			blob_written += to_copy;
		}

		if (ata_pio_write28(data_lba + s, sector) != 0) return VFS_ERR_BUSY;
	}

	kfree(nodes);
	vfs_mark_clean();
	return VFS_OK;
}

vfs_status_t vfs_load(void) {
	uint8_t sector[ATA_SECTOR_SIZE];
	if (ata_pio_read28(VFS_LBA_BASE, sector) != 0) return VFS_ERR_NOT_FOUND;

	vfs_superblock_t sb;
	kmemset(&sb, 0, sizeof(sb));
	for (size_t i = 0; i < sizeof(sb); i++) ((uint8_t*)&sb)[i] = sector[i];

	if (sb.magic != VFS_MAGIC || sb.version != VFS_VERSION) return VFS_ERR_NOT_FOUND;
	if (sb.node_count == 0 || sb.node_count > 1024) return VFS_ERR_NOT_FOUND;

	uint32_t node_table_bytes = sb.node_count * (uint32_t)sizeof(vfs_disk_node_t);
	uint32_t node_table_sectors = bytes_to_sectors(node_table_bytes);
	uint32_t data_sectors = bytes_to_sectors(sb.data_bytes);

	uint8_t* nodebuf = (uint8_t*)kmalloc(node_table_sectors * ATA_SECTOR_SIZE);
	if (!nodebuf) return VFS_ERR_NO_MEM;

	for (uint32_t s = 0; s < node_table_sectors; s++) {
		if (ata_pio_read28(VFS_LBA_BASE + 1u + s, sector) != 0) return VFS_ERR_BUSY;
		for (uint32_t j = 0; j < ATA_SECTOR_SIZE; j++) nodebuf[s * ATA_SECTOR_SIZE + j] = sector[j];
	}

	uint8_t* databuf = 0;
	if (sb.data_bytes > 0) {
		databuf = (uint8_t*)kmalloc(data_sectors * ATA_SECTOR_SIZE);
		if (!databuf) return VFS_ERR_NO_MEM;

		uint32_t data_lba = VFS_LBA_BASE + 1u + node_table_sectors;
		for (uint32_t s = 0; s < data_sectors; s++) {
			if (ata_pio_read28(data_lba + s, sector) != 0) return VFS_ERR_BUSY;
			for (uint32_t j = 0; j < ATA_SECTOR_SIZE; j++) databuf[s * ATA_SECTOR_SIZE + j] = sector[j];
		}
	}

	vfs_node_t** nodes = (vfs_node_t**)kmalloc(sizeof(vfs_node_t*) * sb.node_count);
	if (!nodes) return VFS_ERR_NO_MEM;
	for (uint32_t i = 0; i < sb.node_count; i++) nodes[i] = 0;

	for (uint32_t i = 0; i < sb.node_count; i++) {
		vfs_disk_node_t dn;
		kmemset(&dn, 0, sizeof(dn));
		uint32_t base = i * sizeof(vfs_disk_node_t);
		for (uint32_t b = 0; b < sizeof(vfs_disk_node_t); b++) {
			((uint8_t*)&dn)[b] = nodebuf[base + b];
		}

		vfs_node_t* n = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
		if (!n) return VFS_ERR_NO_MEM;
		kmemset(n, 0, sizeof(vfs_node_t));

		n->type = (node_type_t)dn.type;
		n->flags = dn.flags;
		
		if (n->type != NODE_DIR && n->type != NODE_FILE) {
			// bad image
			return VFS_ERR_NOT_FOUND;
		}
		
		for (int k = 0; k < 32; k++) n->name[k] = dn.name[k];
		n->name[31] = '\0';

		if (n->type == NODE_FILE && dn.file_len > 0) {
			n->file_data = (char*)kmalloc(dn.file_len + 1u);
			if (!n->file_data) return VFS_ERR_NO_MEM;
			for (uint32_t k = 0; k < dn.file_len; k++) {
				n->file_data[k] = (char)databuf[dn.file_off + k];
			}
			n->file_data[dn.file_len] = '\0';
			n->file_size = dn.file_len;
		}

		nodes[i] = n;
	}

	for (uint32_t i = 0; i < sb.node_count; i++) {
		vfs_disk_node_t dn;
		kmemset(&dn, 0, sizeof(dn));
		uint32_t base = i * sizeof(vfs_disk_node_t);
		for (uint32_t b = 0; b < sizeof(vfs_disk_node_t); b++) {
			((uint8_t*)&dn)[b] = nodebuf[base + b];
		}

		vfs_node_t* n = nodes[i];
		if (dn.parent >= 0) n->parent = nodes[(uint32_t)dn.parent];
		if (dn.first_child >= 0) n->child_head = nodes[(uint32_t)dn.first_child];
		if (dn.next_sibling >= 0) n->sibling_next = nodes[(uint32_t)dn.next_sibling];
	}

	g_root = nodes[sb.root_index < sb.node_count ? sb.root_index : 0];
	g_cwd = find_base_dir();
	if (!g_cwd) g_cwd = g_root;

	kfree(nodes);
	vfs_mark_clean();
	return VFS_OK;
}

void vfs_shop(vfs_list_cb_t cb, void* user) {
	if (!cb || !g_cwd) return;

	int guard = 0;
	for (vfs_node_t* n = g_cwd->child_head; n; n = n->sibling_next) {
		if (++guard > 1024) break;

		n->name[31] = '\0';
		int is_dir = (n->type == NODE_DIR) ? 1 : 0;
		cb(n->name, is_dir, user);
	}
}

void vfs_grimoire(vfs_spell_cb_t cb, void* user) {
	if (!cb || !g_cwd) return;

	int guard = 0;
	for (vfs_node_t* n = g_cwd->child_head; n; n = n->sibling_next) {
		if (++guard > 1024) break;

		if (n->type != NODE_FILE) continue;
		n->name[31] = '\0';

		if (!has_ms_extension(n->name)) continue;
		if ((n->flags & 0x01) == 0) continue;

		cb(n->name, user);
	}
}

vfs_status_t vfs_learn(const char* filename) {
	if (!g_cwd) return VFS_ERR_NOT_FOUND;
	vfs_node_t* f = find_child(g_cwd, filename);
	if (!f) return VFS_ERR_NOT_FOUND;
	if (f->type != NODE_FILE) return VFS_ERR_IS_DIR;
	if (!has_ms_extension(filename)) return VFS_ERR_NAME_INVALID;

	f->flags |= 0x01;
	vfs_mark_dirty();
	return VFS_OK;
}

vfs_status_t vfs_is_learned(const char* filename, int* out_learned) {
	if (out_learned) *out_learned = 0;
	if (!g_cwd) return VFS_ERR_NOT_FOUND;
	vfs_node_t* f = find_child(g_cwd, filename);
	if (!f) return VFS_ERR_NOT_FOUND;
	if (f->type != NODE_FILE) return VFS_ERR_IS_DIR;
	if (!has_ms_extension(filename)) return VFS_ERR_NAME_INVALID;

	if (out_learned) *out_learned = (f->flags & 0x01) ? 1 : 0;
	return VFS_OK;
}

