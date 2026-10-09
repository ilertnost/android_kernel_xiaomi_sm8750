// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/vip.c — F2FS VIP inode set (CONFIG_F2FS_VIP_FILE)
 *
 * Ported from the HyperOS 4 kernel, where it backs the observed runtime state:
 *
 *   /sys/fs/f2fs/<dev>/vip_file_enable        = 1
 *   /sys/fs/f2fs/<dev>/gc_pin_file_thresh     = 2048
 *   /sys/fs/f2fs/<dev>/reserved_pin_section   = 512
 *
 * The on-disk format is NOT touched by this feature: f2fs_super_block is
 * byte-identical between the HyperOS 3 and HyperOS 4 kernels (3072 bytes each,
 * verified from the stock BTF), so all of this is in-memory bookkeeping.
 *
 * The container layout and the three recovery'd entry points below were taken
 * from the HyperOS 4 binary (f2fs_vip_bsearch / f2fs_vip_insert /
 * f2fs_vip_remove) and reproduce its behaviour:
 *
 *   f2fs_vip_bsearch(nm_i, ino, &found)
 *       - returns 0 with *found == false when count == 0
 *       - otherwise binary-searches sorted[], returning the entry index, or
 *         the insertion point with *found == false
 *   f2fs_vip_remove(sbi, ino)
 *       - EOPNOTSUPP when the containers are not allocated (stock returns -95)
 *       - ENOENT when the nid is absent (stock returns -2)
 *       - otherwise memmoves the tail of sorted[] down, decrements count and
 *         drops the entry from the unsorted ring
 *   f2fs_vip_insert(sbi, ino)
 *       - appends to the unsorted ring while it has room; once the ring is
 *         full its contents are merged into sorted[] (which grows by doubling)
 *         and the ring is reset
 *
 * Call sites recovered in the HyperOS 4 binary:
 *   f2fs_iget            -> f2fs_vip_bsearch   (stamp FI_VIP on the inode)
 *   __f2fs_ioctl         -> f2fs_vip_insert    (F2FS_IOC_SET_PIN_FILE)
 *   f2fs_unlink          -> f2fs_vip_remove
 */

#include <linux/f2fs_fs.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "f2fs.h"

/*
 * f2fs_vip_bsearch - binary search sorted[] by ino
 *
 * Callers must hold vip->lock, or otherwise exclude concurrent mutation.
 */
unsigned int f2fs_vip_bsearch(struct f2fs_nm_info *nm_i, nid_t ino, bool *found)
{
	struct f2fs_vip_manager *vip = &nm_i->vip;
	unsigned int lo = 0, hi, mid;

	*found = false;

	if (vip->count == 0)
		return 0;

	hi = vip->count - 1;

	while (lo <= hi) {
		int diff = (int)(hi - lo);

		if (diff < 0)
			diff++;
		mid = lo + ((unsigned int)diff >> 1);

		if (vip->sorted[mid].ino == ino) {
			*found = true;
			return mid;
		}

		if (vip->sorted[mid].ino < ino)
			lo = mid + 1;
		else
			hi = mid - 1;
	}

	return lo;
}

static int f2fs_vip_grow_sorted(struct f2fs_vip_manager *vip)
{
	unsigned int new_cap = vip->capacity * 2;
	struct vip_inode_sorted *ns;

	if (new_cap < F2FS_VIP_SORTED_MIN)
		new_cap = F2FS_VIP_SORTED_MIN;

	ns = kvmalloc_array(new_cap, sizeof(*ns), GFP_KERNEL);
	if (!ns)
		return -ENOMEM;

	if (vip->count)
		memcpy(ns, vip->sorted, vip->count * sizeof(*ns));

	kvfree(vip->sorted);
	vip->sorted = ns;
	vip->capacity = new_cap;

	return 0;
}

/* Merge every pending ring entry into sorted[]. vip->lock must be held. */
static void f2fs_vip_merge_ring(struct f2fs_nm_info *nm_i)
{
	struct f2fs_vip_manager *vip = &nm_i->vip;

	while (vip->unsorted_head != vip->unsorted_tail) {
		nid_t ino = vip->unsorted[vip->unsorted_head].ino;
		unsigned int idx;
		bool found;
		int ret;

		if (vip->count >= vip->capacity) {
			ret = f2fs_vip_grow_sorted(vip);
			if (ret)
				return;
		}

		idx = f2fs_vip_bsearch(nm_i, ino, &found);
		if (found) {
			vip->unsorted_head = (vip->unsorted_head + 1) %
					     vip->unsorted_buf_size;
			continue;
		}

		memmove(&vip->sorted[idx + 1], &vip->sorted[idx],
			(vip->count - idx) * sizeof(*vip->sorted));
		vip->sorted[idx].ino = ino;
		vip->sorted[idx].unsorted_idx = vip->unsorted_head;
		vip->count++;

		vip->unsorted_head = (vip->unsorted_head + 1) %
				     vip->unsorted_buf_size;
	}

	vip->unsorted_head = vip->unsorted_tail = 0;
}

/*
 * f2fs_vip_insert - add @ino to the VIP set.
 * Returns 0 on success or when the entry already exists, -ENOMEM when the
 * sorted array cannot grow, -EOPNOTSUPP when the set is not usable.
 */
int f2fs_vip_insert(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct f2fs_nm_info *nm_i = sbi->nm_info;
	struct f2fs_vip_manager *vip;
	unsigned long flags;
	bool found;
	int ret = 0;

	if (!nm_i)
		return -EOPNOTSUPP;
	vip = &nm_i->vip;

	spin_lock_irqsave(&vip->lock, flags);

	if (!vip->sorted || !vip->unsorted) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	f2fs_vip_bsearch(nm_i, ino, &found);
	if (found)
		goto out;

	if (f2fs_vip_unsorted_count(vip) + 1 < vip->unsorted_buf_size) {
		vip->unsorted[vip->unsorted_tail].ino = ino;
		vip->unsorted_tail = (vip->unsorted_tail + 1) %
				     vip->unsorted_buf_size;
		goto out;
	}

	/* Ring is full: flush it and push this nid through the same path. */
	if (vip->count >= vip->capacity) {
		ret = f2fs_vip_grow_sorted(vip);
		if (ret)
			goto out;
	}

	f2fs_vip_merge_ring(nm_i);

	f2fs_vip_bsearch(nm_i, ino, &found);
	if (found)
		goto out;

	if (vip->count >= vip->capacity) {
		ret = f2fs_vip_grow_sorted(vip);
		if (ret)
			goto out;
	}

	{
		unsigned int idx = f2fs_vip_bsearch(nm_i, ino, &found);

		memmove(&vip->sorted[idx + 1], &vip->sorted[idx],
			(vip->count - idx) * sizeof(*vip->sorted));
		vip->sorted[idx].ino = ino;
		vip->sorted[idx].unsorted_idx = vip->unsorted_head;
		vip->count++;
	}
out:
	spin_unlock_irqrestore(&vip->lock, flags);
	return ret;
}

/*
 * f2fs_vip_remove - drop @ino from the VIP set.
 * Returns 0, -EOPNOTSUPP when the set is not allocated, -ENOENT when absent.
 */
void f2fs_vip_remove(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct f2fs_nm_info *nm_i = sbi->nm_info;
	struct f2fs_vip_manager *vip;
	unsigned long flags;
	unsigned int idx;
	bool found;

	if (!nm_i)
		return;
	vip = &nm_i->vip;

	spin_lock_irqsave(&vip->lock, flags);

	if (!vip->sorted || !vip->unsorted)
		goto out;

	idx = f2fs_vip_bsearch(nm_i, ino, &found);
	if (!found)
		goto out;

	if (vip->sorted[idx].unsorted_idx < vip->unsorted_tail) {
		memmove(&vip->unsorted[vip->sorted[idx].unsorted_idx],
			&vip->unsorted[vip->sorted[idx].unsorted_idx + 1],
			(vip->unsorted_tail - vip->sorted[idx].unsorted_idx - 1) *
			sizeof(*vip->unsorted));
		vip->unsorted_tail--;
	}

	memmove(&vip->sorted[idx], &vip->sorted[idx + 1],
		(vip->count - idx - 1) * sizeof(*vip->sorted));
	vip->count--;

	/* The remaining entries keep pointing one slot too low after the shift. */
	for (idx = 0; idx < vip->count; idx++)
		if (vip->sorted[idx].unsorted_idx > 0)
			vip->sorted[idx].unsorted_idx--;
out:
	spin_unlock_irqrestore(&vip->lock, flags);
}

/*
 * Returns whether @ino is in the VIP set.
 *
 * Called from f2fs_iget(), which the mount path reaches for F2FS_META_INO and
 * the quota inodes *before* f2fs_build_node_manager() has run, so sbi->nm_info
 * is still NULL there. Guard for it, not only for safety: an unguarded
 * sbi->nm_info->vip dereference panics during mount.
 */
bool f2fs_is_vip_inode(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct f2fs_vip_manager *vip;
	unsigned long flags;
	bool found;

	if (!sbi->nm_info || !sbi->nm_info->vip.sorted)
		return false;

	vip = &sbi->nm_info->vip;

	spin_lock_irqsave(&vip->lock, flags);
	f2fs_vip_bsearch(sbi->nm_info, ino, &found);
	spin_unlock_irqrestore(&vip->lock, flags);

	return found;
}

int f2fs_vip_init(struct f2fs_nm_info *nm_i)
{
	struct f2fs_vip_manager *vip = &nm_i->vip;

	vip->capacity = F2FS_VIP_SORTED_MIN;
	vip->unsorted_buf_size = F2FS_VIP_UNSORTED_MIN;
	vip->sorted = kvmalloc_array(vip->capacity, sizeof(*vip->sorted),
				     GFP_KERNEL);
	vip->unsorted = kvmalloc_array(vip->unsorted_buf_size,
				       sizeof(*vip->unsorted), GFP_KERNEL);
	if (!vip->sorted || !vip->unsorted) {
		kvfree(vip->sorted);
		kvfree(vip->unsorted);
		vip->sorted = NULL;
		vip->unsorted = NULL;
		return -ENOMEM;
	}

	vip->count = 0;
	vip->unsorted_head = 0;
	vip->unsorted_tail = 0;
	spin_lock_init(&vip->lock);
	vip->enabled = 1;	/* observed state on stock: vip_file_enable = 1 */

	return 0;
}

void f2fs_vip_destroy(struct f2fs_nm_info *nm_i)
{
	struct f2fs_vip_manager *vip = &nm_i->vip;

	kvfree(vip->sorted);
	kvfree(vip->unsorted);
	vip->sorted = NULL;
	vip->unsorted = NULL;
	vip->count = 0;
	vip->capacity = 0;
	vip->unsorted_buf_size = 0;
	vip->unsorted_head = vip->unsorted_tail = 0;
	vip->enabled = 0;
}
