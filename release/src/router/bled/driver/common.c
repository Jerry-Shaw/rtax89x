/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 * Copyright 2014, ASUSTeK Inc.
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND ASUS GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
#include <linux/atomic.h>
#endif
#include <asm/uaccess.h>
#include <linux/kmod.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include "../bled_defs.h"
#include "gpio_api.h"
#include "check.h"

extern int nr_irqs;

/*
 * File operation structure for blinkled devices
 */
static int bled_open(struct inode *, struct file *);
static int bled_release(struct inode *, struct file *);
static long bled_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static const struct file_operations bled_fops = {
	.owner		= THIS_MODULE,
	.open		= bled_open,
	.release	= bled_release,
	.unlocked_ioctl	= bled_ioctl,
	.llseek		= no_llseek,
};

static int bled_start = 0;
static int bled_major = 0;
static struct cdev *bled_cdev = NULL;
static struct class *bled_class = NULL;
static struct device *bled_device = NULL;
static struct proc_dir_entry *bled_pdentry = NULL;

static DEFINE_MUTEX(bled_list_lock);
static struct list_head bled_list;

/* Return value must greater than or equal to 0. */
static void (*bled_type_printer[BLED_TYPE_MAX])(struct bled_priv *bp, struct seq_file *m) = {
	[BLED_TYPE_NETDEV_BLED] = netdev_led_printer,
#if !defined(RTCONFIG_RALINK_MT7629)
	[BLED_TYPE_SWPORTS_BLED] = swports_led_printer,
#endif
#if !defined(RTCONFIG_RALINK_MT7622) && !defined(RTCONFIG_RALINK_MT7629) && !defined(RTCONFIG_RALINK_MT7621)
	[BLED_TYPE_USBBUS_BLED] = usbbus_led_printer,
#endif
	[BLED_TYPE_INTERRUPT_BLED] = interrupt_led_printer,
};

static unsigned long bled_check_interval_tbl[BLED_BHTYPE_MAX] = {
	[BLED_BHTYPE_TIMER] = BLED_TIMER_CHECK_INTERVAL,
	[BLED_BHTYPE_HYBRID] = BLED_HYBRID_CHECK_INTERVAL,
};

static void wproc_disable_bled(char *cmd_str);
static void wproc_enable_bled(char *cmd_str);
static void wproc_delete_bled(char *cmd_str);
static void wproc_debug_check(char *cmd_str);
static void wproc_set_udef_pattern(char *cmd_str);
static void wproc_set_mode(char *cmd_str);

static struct bled_wproc_handler_s {
	char *cmd;
	void (*func)(char *cmd_str);
} bled_wproc_handler[] = {
	{ "disable bled",	wproc_disable_bled },
	{ "enable bled",	wproc_enable_bled },
	{ "delete",		wproc_delete_bled },	/* delete ## [id] */
	{ "debug check",	wproc_debug_check },	/* debug check ## [0|1] */
	{ "set udef pattern",	wproc_set_udef_pattern },	/* set udef pattern ## interval v1 [v2 [v3 [...]]] */
	{ "set mode",		wproc_set_mode },	/* set mode ## [0|1] */

	{ NULL, NULL },
};

/**
 * Turn on/off LED.
 * @v:
 * 	0:	turn off LED
 * 	1:	turn on LED
 */
static inline void bled_ctrl(struct bled_priv *bp, int v)
{
	unsigned int i;

	for (i = 0; i < bp->gpio_count; i++) {
		if (bp->mode == BLED_UDEF_PATTERN_MODE && bp->gpio_count > 1) {
			if (bp->udef_pattern.trigger[i] == 0) {
				bp->gpio_set(bp->gpio_nr[i], !!(0) ^ bp->active_low[i]);
				continue;
			}
		}

		bp->gpio_set(bp->gpio_nr[i], !!v ^ bp->active_low[i]);
	}
	bp->value = v;
}

/**
 * Calculate number of ports in a mask.
 * @m:
 * @max:
 * @return:	number of ports in mask.
 */
static inline int __calc_nr(unsigned int m, unsigned int max)
{
	int c;

	for (c = 0; m && c < max; m >>= 1) {
		if (!(m & 1))
			continue;
		c++;
	}

	return c;
}

/**
 * Calculate number of switch ports in a mask.
 * @m:
 * @return:	number of switch ports in mask.
 */
static inline int calc_nr_ports(unsigned int m)
{
	return __calc_nr(m, BLED_MAX_NR_SWPORTS);
}

/**
 * Calculate number of usb bus in a mask.
 * @m:
 * @return:	number of usb bus in mask.
 */
static inline int calc_nr_bus(unsigned int m)
{
	return __calc_nr(m, BLED_MAX_NR_USBBUS);
}

/**
 * Suspend a bled specified by @bp.
 * @bp:	pointer to private data of bled.
 */
static void suspend_bled(struct bled_priv *bp)
{
	if (!bp)
		return;

	if (bp->bh_type == BLED_BHTYPE_HYBRID)
		cancel_delayed_work(&bp->bled_work);
#ifdef USE_WORKQUEUE_INSTEAD_OF_TIMER
	cancel_delayed_work(&bp->timer_work);
#else
	del_timer_sync(&bp->timer);
#endif
	dbg_bl_v("Suspend GPIO#%d.\n", bp->gpio_nr[0]);
}

/**
 * Resume (reschedule only) a bled specified by @bp.
 * @bp:	pointer to private data of bled.
 */
static void __resume_bled(struct bled_priv *bp)
{
	if (!bp)
		return;

	if (bp->bh_type == BLED_BHTYPE_HYBRID)
		schedule_delayed_work(&bp->bled_work, bp->next_check_interval);
#ifdef USE_WORKQUEUE_INSTEAD_OF_TIMER
	mod_delayed_work(bp->timer_wq, &bp->timer_work, bp->next_check_interval);
#else
	mod_timer(&bp->timer, jiffies + bp->next_check_interval);
#endif
	dbg_bl_v("Resume GPIO#%d.\n", bp->gpio_nr[0]);
}

/**
 * Resume a bled specified by @bp.
 * @bp:	pointer to private data of bled.
 */
static void resume_bled(struct bled_priv *bp)
{
	if (!bp)
		return;

	if (bp->reset_check)
		bp->reset_check(bp);
	bp->next_blink_ts = 0;
	bp->next_check_ts = jiffies + bp->next_check_interval;
	__resume_bled(bp);
}

/**
 * core bottom half handler of a bled.
 * @bp:
 */
static void __bled_bh_func(struct bled_priv *bp)
{
	int i;
	unsigned long t;
	struct udef_pattern_s *patt;

	if (unlikely(!bp))
		return;

	if (unlikely(!bled_start)) {
		bled_ctrl(bp, 1);
		return;
	}

	if (unlikely(bp->state == BLED_STATE_STOP)) {
		dbg_bl("GPIO#%d stopped!\n", bp->gpio_nr[0]);
		return;
	}

	switch (bp->mode) {
	case BLED_UDEF_PATTERN_MODE:
		patt = &bp->udef_pattern;
		if (unlikely(!bp->next_blink_ts))
			patt->curr = 0;
		if (!bp->next_blink_ts || time_after_eq(jiffies, bp->next_blink_ts)) {
			bled_ctrl(bp, patt->value[patt->curr]);
			bp->next_blink_ts = bp->next_check_ts = jiffies + patt->interval[patt->curr];
			if (++patt->curr >= patt->nr_pattern)
				patt->curr = 0;
		}
		break;
	default:	/* BLED_NORMAL_MODE */
		if (bp->next_blink_ts && time_after_eq(jiffies, bp->next_blink_ts)) {
			bled_ctrl(bp, bp->value ^ 1);
			bp->next_blink_ts = jiffies + bp->next_blink_interval;
		}

		if (unlikely(bp->bh_type == BLED_BHTYPE_HYBRID)) {
			/* Don't use bp->next_blilnk_ts here. We need to keep timer running. */
#ifdef USE_WORKQUEUE_INSTEAD_OF_TIMER
			mod_delayed_work(bp->timer_wq, &bp->timer_work, bp->next_blink_interval);
#else
			mod_timer(&bp->timer, jiffies + bp->next_blink_interval);
#endif
			return;
		}

		if (time_after_eq(jiffies, bp->next_check_ts)) {
			i = bp->check(bp);
			if (unlikely(bp->check2))
				i += bp->check2(bp);

			if (!bp->next_blink_ts && i) {
				bp->next_blink_interval = bp->interval;
				bp->next_blink_ts = jiffies + bp->next_blink_interval;
			} else if (!i) {
				bp->next_blink_ts = 0;
				bled_ctrl(bp, 1);
			}
			if (unlikely(i && bp->next_check_interval == BLED_WAIT_IF_INTERVAL))
				bp->next_check_interval = bled_check_interval_tbl[bp->bh_type];

			bp->next_check_ts = jiffies + bp->next_check_interval;
		}
	}

	t = bp->next_blink_ts;
	if (!bp->next_blink_ts || time_before(bp->next_check_ts, bp->next_blink_ts))
		t = bp->next_check_ts;

#ifdef USE_WORKQUEUE_INSTEAD_OF_TIMER
	mod_delayed_work(bp->timer_wq, &bp->timer_work, (t - jiffies));
#else
	mod_timer(&bp->timer, t);
#endif
}
/**
 * Timer handler of a bled.
 */
#ifdef USE_WORKQUEUE_INSTEAD_OF_TIMER
static void bled_timer_func(struct work_struct *work)
{
	struct bled_priv *bp = container_of(work, typeof(*bp), timer_work.work);
	__bled_bh_func(bp);
}
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
static void bled_timer_func(unsigned long data)
{
	__bled_bh_func((struct bled_priv *) data);
}
#else
static void bled_timer_func(struct timer_list *t)
{
	struct bled_priv *bp = from_timer(bp, t, timer);
	__bled_bh_func(bp);
}
#endif
#endif

/**
 * workqueue of a bled.
 * Run check function of a bled only.
 * Workqueue would be disabled when bp->mode = BLED_UDEF_PATTERN_MODE.
 */
static void bled_wq_func(struct work_struct *work)
{
	int i;
	struct bled_priv *bp = container_of(work, struct bled_priv, bled_work.work);

	if (unlikely(!bp))
		return;

	if (unlikely(bp->state == BLED_STATE_STOP)) {
		dbg_bl("GPIO#%d stopped!\n", bp->gpio_nr[0]);
		return;
	}

	i = bp->check(bp);
	if (unlikely(bp->check2))
		i += bp->check2(bp);

	if (!bp->next_blink_ts && i) {
		bp->next_blink_interval = bp->interval;
		bp->next_blink_ts = jiffies + bp->next_blink_interval;
	} else if (!i) {
		bp->next_blink_ts = 0;
		bled_ctrl(bp, 1);
	}
	if (unlikely(i && bp->next_check_interval == BLED_WAIT_IF_INTERVAL))
		bp->next_check_interval = bled_check_interval_tbl[bp->bh_type];

	bp->next_check_ts = jiffies + bp->next_check_interval;

	schedule_delayed_work(&bp->bled_work, bp->next_check_interval);
}

/**
 * Looking for bled structure by GPIO#
 * NOTE:
 * 	Take bled_list_lock before calling this function
 */
static struct bled_priv *find_bled_priv_by_gpio(unsigned int gpio_nr)
{
	struct bled_priv *bp, *ret = NULL;

	list_for_each_entry(bp, &bled_list, list) {
		if (bp->gpio_nr[0] != gpio_nr)
			continue;

		ret = bp;
		break;
	}

	return ret;
}

/**
 * Allocate a bled structure which provides extra_size at tail.
 * @return:	pointer to struct bled_priv.
 */
static inline struct bled_priv *allocate_bled_priv(size_t extra_size)
{
	return kzalloc(ALIGN(sizeof(struct bled_priv) + extra_size, 4), GFP_KERNEL);
}

/**
 * Get pointer to extra private data of struct bled_priv.
 * @return:	pointer to extra private data.
 */
static inline void *to_check_priv(struct bled_priv *bp)
{
	return (char*)bp + ALIGN(sizeof(struct bled_priv), 4);
}

/**
 * Basic validation struct ndev_bled which is provided by user-space code.
 * @bc:		pointer to struct bled_common.
 * @type:
 * 	0:	don't check GPIO API
 *  otherwise:	check GPIO API
 * @return:
 * 	0:	OK
 *  otherwise:	fail
 */
static int validate_bled(struct bled_common *bc, int type)
{
	struct gpio_api_s *p;

	if (!bc)
		return -EINVAL;

	if (bc->gpio_nr >= 0xFF || bc->gpio2_nr >= 0xFF)
		return -EINVAL;
	if (type && bc->bh_type >= BLED_BHTYPE_MAX)
		return -EINVAL;
	if (type &&
	    (bc->gpio_api_id >= GPIO_API_MAX || bc->gpio_api_id < 0))
		return -EINVAL;
	p = &gpio_api_tbl[bc->gpio_api_id];
	if (type && (!p->gpio_set || !p->gpio_get)) {
		printk(KERN_WARNING "bled: %d-th GPIO API %p/%p is not defined!\n",
			bc->gpio_api_id, p->gpio_set, p->gpio_get);
		return -EINVAL;
	}
	if (bc->mode >= BLED_MODE_MAX)
		return -EINVAL;
	if (bc->nr_pattern &&
	    (bc->pattern_interval < BLED_UDEF_PATTERN_MIN_INTERVAL ||
	     bc->pattern_interval > BLED_UDEF_PATTERN_MAX_INTERVAL))
	{
	    printk(KERN_INFO "bled: invalid user defined pattern. (%u, %u)\n",
		    bc->nr_pattern, bc->pattern_interval);
	    return -EINVAL;
	}

	return 0;
}

/**
 * Validates struct ndev_bled which is provided by user-space code.
 * @return:
 * 	0:	OK
 *  otherwise:	fail
 */
static int validate_nd_bled(struct ndev_bled *nd)
{
	if (!nd)
		return -EINVAL;
	if (validate_bled(&nd->bled, 1))
		return -EINVAL;
	if (nd->ifname[0] == '\0')
		return -EINVAL;

	return 0;
}

/**
 * Validates struct swport_bled which is provided by user-space code.
 * @return:
 * 	0:	OK
 *  otherwise:	fail
 */
static int validate_sport_bled(struct swport_bled *sl)
{
	if (!sl)
		return -EINVAL;
	if (validate_bled(&sl->bled, 1))
		return -EINVAL;
	if ((sl->port_mask & ~((1U << BLED_MAX_NR_SWPORTS) - 1)) != 0)
		return -EINVAL;

	return 0;
}

/**
 * Validates struct usbbus_bled which is provided by user-space code.
 * @return:
 * 	0:	OK
 *  otherwise:	fail
 */
#if defined(RTCONFIG_USB)
static int validate_usbbus_bled(struct usbbus_bled *ul)
{
	if (!ul)
		return -EINVAL;
	if (validate_bled(&ul->bled, 1))
		return -EINVAL;

	return 0;
}
#endif

/**
 * Validates struct interrupt_bled which is provided by user-space code.
 * @return:
 * 	0:	OK
 *  otherwise:	fail
 */
static int validate_intr_bled(struct interrupt_bled *it)
{
	unsigned int i;

	if (!it)
		return -EINVAL;
	if (validate_bled(&it->bled, 1))
		return -EINVAL;
	if (it->nr_interrupt > BLED_MAX_NR_INTERRUPT)
		return -EINVAL;
	for (i = 0; i < it->nr_interrupt; ++i) {
		if (it->interrupt[i] >= nr_irqs)
			return -ENODEV;
	}

	return 0;
}

/**
 * Find out whether @id exist in @bp.
 * @bp:		pointer to private data of bled
 * @id:		string, to backward compatible, NULL is translated as "".
 * @return:
 *  0:		@id is not in bp->id[0 ~ bp->nr_users], invalid parameter.
 *  otherwise:	@id is in bp->id[0 ~ bp->nr_users].
 */
static int is_id_exist(struct bled_priv *bp, const char *id)
{
	int ret = 0;
	unsigned int i;

	if (!bp)
		return 0;

	if (!id)
		id = "";

	for (i = 0; i < bp->nr_users; ++i) {
		if (!strcmp(bp->id[i], id)) {
			ret = 1;
			break;
		}
	}

	return ret;
}

/**
 * Return index of @bp->id for @id.
 * @bp:		pointer to private data of bled
 * @id:		string, to backward compatible, NULL is translated as "".
 * @return:	index of @bp->id that equal to @id
 *  < 0:			invalid parameter or error.
 *  0 ~ @bp->nr_users - 1:	success
 *  otherwise:			shouldn't happen
 */
static int idx_of_id(struct bled_priv *bp, const char *id)
{
	int i, ret = -1;

	if (!bp)
		return -1;

	if (!id)
		id = "";

	for (i = 0; i < bp->nr_users; ++i) {
		if (!strcmp(bp->id[i], id)) {
			ret = i;
			break;
		}
	}

	return ret;
}

/**
 * Initialize bled private date base on struct bled_common.
 * @bp:		pointer to private data of bled
 * @bc:		pointer to struct bled_common which is provided by user-space and copied to kernel-space.
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int init_bled_priv(struct bled_priv *bp, struct bled_common *bc)
{
	unsigned int i, curr;
	struct gpio_api_s *p;
	struct udef_pattern_s *patt = &bp->udef_pattern;

	bp->gpio_nr[bp->gpio_count] = bc->gpio_nr;
	bp->active_low[bp->gpio_count] = !!bc->active_low;
	bp->state = BLED_STATE_STOP;
	bp->bh_type = bc->bh_type;
	bp->mode = BLED_NORMAL_MODE;
	bp->next_blink_ts = 0;
	bp->next_check_interval = bled_check_interval_tbl[bp->bh_type];
	bp->next_check_ts = jiffies + bp->next_check_interval;
	bp->threshold = (bc->min_blink_speed * 1024) / (HZ / bp->next_check_interval);	/* KB/s --> bytes per bp->next_check_interval */
	dbg_bl("GPIO#%d BLED: %u KB/s --> %8lu bytes / %3u ms\n",
		bc->gpio_nr, bc->min_blink_speed, bp->threshold,
		jiffies_to_msecs(bp->next_check_interval));
	bp->interval = bc->interval * HZ / 1000;		/* ms --> jiffies */
	if (bc->interval && bp->interval == 0) {
		printk(KERN_NOTICE "bled: GPIO#%d small interval %dms!\n",
			bc->gpio_nr, bc->interval);
		bc->interval = 1;
	}

	memset(&bp->udef_pattern, 0, sizeof(bp->udef_pattern));
	patt->value[0] = !!bc->pattern[0];
	for (i = 0, curr = 0; i < bc->nr_pattern; ++i) {
		if (patt->value[curr] != !!bc->pattern[i])
			curr++;

		patt->value[curr] = !!bc->pattern[i];
		patt->interval[curr] += msecs_to_jiffies(bc->pattern_interval);
	}
	if (curr)
		patt->nr_pattern = ++curr;

	dbg_bl("GPIO#%d BLED: %ums --> %ld jiffies\n",
		bc->gpio_nr, bc->interval, bp->interval);
	p = &gpio_api_tbl[bc->gpio_api_id];
	bp->gpio_set = p->gpio_set;
	bp->gpio_get = p->gpio_get;
	bp->value = !!p->gpio_get(bp->gpio_nr[0]) ^ bp->active_low[0];

#ifdef USE_WORKQUEUE_INSTEAD_OF_TIMER
	bp->timer_wq = create_singlethread_workqueue(THIS_MODULE->name);
        if (!bp->timer_wq) {
		return -ENOMEM;
	}
	INIT_DELAYED_WORK(&bp->timer_work, bled_timer_func);
	queue_delayed_work(bp->timer_wq, &bp->timer_work, bp->next_check_interval);
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
	init_timer(&bp->timer);
	bp->timer.expires = jiffies + bp->next_check_interval;
	bp->timer.data = (unsigned long) bp;
	bp->timer.function = bled_timer_func;
#else
	timer_setup(&bp->timer, bled_timer_func, 0);
	mod_timer(&bp->timer, (jiffies + bp->next_check_interval));
#endif
#endif

	INIT_DELAYED_WORK(&bp->bled_work, bled_wq_func);
	bp->gpio_count++;

	return 0;
}

/**
 * Add a basic bled.
 * @bc:		pointer to struct bled_common
 * @extra_size:	extra size that is required by private data of checker
 * @return:
 * 	NULL:	fail
 *  otherwise:	OK
 */
static void* add_bled(struct bled_common *bc, size_t extra_size, enum bled_type type)
{
	void *priv;
	struct bled_priv *bp;

	if ((bp = find_bled_priv_by_gpio(bc->gpio_nr)) != NULL) {
		if (bp->type == type) {
			if (is_id_exist(bp, bc->id) || bp->nr_users < MAX_NR_BLED_PER_GPIO) {
				/* Assume the GPIO PIN is shared by two or more bled. */
				return bp;
			}
			return ERR_PTR(-EACCES);
		} else {
			return ERR_PTR(-EEXIST);
		}
	}

	bp = allocate_bled_priv(extra_size);
	if (!bp)
		return ERR_PTR(-ENOMEM);
	if (init_bled_priv(bp, bc))
		return ERR_PTR(-EINVAL);

	priv = to_check_priv(bp);
	/* netdev_bled-specific private data */
	bp->check_priv = priv;

	list_add(&bp->list, &bled_list);

	return bp;
}

/**
 * Remove a bled structure from bled_list. Need to be executed multiple times if bp->nr_users > 1.
 * @bp:		pointer to private data of bled
 * @id:		string, to backward compatible, NULL is translated as "".
 * NOTE:
 * 	Take bled_list_lock before calling this function
 */
static void del_bled(struct bled_priv *bp, const char *id)
{
	int i, j;

	if (!bp)
		return;

	if (!id)
		id = "";

	suspend_bled(bp);
	for (i = 0; i < bp->nr_users; ++i) {
		if (strcmp(bp->id[i], id))
			continue;

		*bp->id[i] = '\0';
		for (j = i + 1; j < bp->nr_users; ++j) {
			strlcpy(bp->id[i], bp->id[j], sizeof(bp->id[i]));
			bp->user_states[i] = bp->user_states[j];
			*bp->id[j] = '\0';
		}
	}
	bp->nr_users--;
	if (bp->nr_users) {
		resume_bled(bp);
	} else {
		/* Always turn on LED */
		bled_ctrl(bp, 1);
		list_del(&bp->list);
#ifdef USE_WORKQUEUE_INSTEAD_OF_TIMER
		flush_workqueue(bp->timer_wq);
		destroy_workqueue(bp->timer_wq);
#endif
		dbg_bl("Remove GPIO#%d.\n", bp->gpio_nr[0]);
		kfree(bp);
	}
}

/**
 * Handler of /proc/bled write procedure.
 * Disable bled driver.
 * @cmd_str:	commmand string
 * 		disable bled
 */
static void wproc_disable_bled(char *cmd_str)
{
	struct bled_priv *bp;

	bled_start = 0;
	mutex_lock(&bled_list_lock);
	list_for_each_entry(bp, &bled_list, list) {
		suspend_bled(bp);
		/* Always turn on LED */
		bled_ctrl(bp, 1);
		bp->next_blink_interval = 0;
	}
	mutex_unlock(&bled_list_lock);
}

/**
 * Handler of /proc/bled write procedure
 * Enable bled driver.
 * @cmd_str:	commmand string
 * 		enable bled
 */
static void wproc_enable_bled(char *cmd_str)
{
	struct bled_priv *bp;

	bled_start = 1;
	mutex_lock(&bled_list_lock);
	list_for_each_entry(bp, &bled_list, list) {
		__resume_bled(bp);
	}
	mutex_unlock(&bled_list_lock);
}

/**
 * Handler of /proc/bled write procedure
 * Delete a bled.
 * @cmd_str:	commmand string
 * 		delete ##
 */
static void wproc_delete_bled(char *cmd_str)
{
	int r, gpio_nr;
	char id_buf[BLED_ID_LEN] =  "", *id = NULL;
	struct bled_priv *bp;

	/* cmd_str = "delete ##"
	 * ## means gpio_nr
	 */
	if (!cmd_str)
		return;

	if ((r = sscanf(cmd_str, "delete %d %s", &gpio_nr, id_buf)) <= 0) {
		dbg_bl("%s: invalid command [%s]\n", __func__, cmd_str);
		return;
	}

	if (r == 2)
		id = id_buf;

	mutex_lock(&bled_list_lock);
	if ((bp = find_bled_priv_by_gpio(gpio_nr))) {
		del_bled(bp, id);
	}
	mutex_unlock(&bled_list_lock);
}

/**
 * Handler of /proc/bled write procedure
 * Enable/Disable debug message of check function.
 * @cmd_str:	commmand string
 * 		debug check ## [0|1]
 */
static void wproc_debug_check(char *cmd_str)
{
	int r, gpio_nr, v;
	struct bled_priv *bp;

	/* cmd_str = "debug check ## [0|1]"
	 * ## means gpio_nr
	 */
	if (!cmd_str)
		return;

	if ((r = sscanf(cmd_str, "debug check %d %d", &gpio_nr, &v)) != 2) {
		dbg_bl("%s: invalid command [%s]\n", __func__, cmd_str);
		return;
	}

	if (gpio_nr < 0 || gpio_nr >= 0xFF || (v && v != 1))
		return;

	mutex_lock(&bled_list_lock);
	if ((bp = find_bled_priv_by_gpio(gpio_nr))) {
		if (!v) {
			bp->flags &= ~(BLED_FLAGS_DBG_CHECK_FUNC);
		} else {
			bp->flags |= BLED_FLAGS_DBG_CHECK_FUNC;
		}
	}
	mutex_unlock(&bled_list_lock);
}

/**
 * Handler of /proc/bled write procedure
 * Set user defined pattern of a bled.
 * @cmd_str:	commmand string
 * 		set udef pattern ## interval v1 [v2 [v3 [...]]]
 */
static void wproc_set_udef_pattern(char *cmd_str)
{
	int r, gpio_nr, v;
	char *p;
	unsigned int curr, interval;
	struct bled_priv *bp;
	struct udef_pattern_s *patt;

	/* cmd_str = "set udef pattern ## interval v1 [v2 [v3 [...]]]"
	 * ## means gpio_nr
	 */
	if (!cmd_str)
		return;

	if ((r = sscanf(cmd_str, "set udef pattern %d %d", &gpio_nr, &interval)) != 2) {
		dbg_bl("%s: invalid command [%s]\n", __func__, cmd_str);
		return;
	}

	if (gpio_nr < 0 || gpio_nr >= 0xFF ||
	    interval < BLED_UDEF_PATTERN_MIN_INTERVAL ||
	    interval > BLED_UDEF_PATTERN_MAX_INTERVAL) {
		dbg_bl("%s: invalid parameter. (gpio_nr %d interval %u)\n",
			__func__, gpio_nr, interval);
		return;
	}

	p = cmd_str + 16;		/* skip "set udef pattern" */
	while (*p && !isdigit(*p))
		p++;
	while (*p && isdigit(*p))	/* skip GPIO number */
		p++;
	while (*p && !isdigit(*p))
		p++;
	while (*p && isdigit(*p))	/* skip interval */
		p++;
	while (*p && !isdigit(*p))
		p++;

	if (!(patt = kzalloc(sizeof(struct udef_pattern_s), GFP_KERNEL))) {
		printk("%s: allocate %zu bytes failed!\n", __func__, sizeof(struct udef_pattern_s));
		return;
	}

	patt->value[0] = !!simple_strtoul(p, NULL, 10);
	for (curr = 0; *p != '\0' && curr < (BLED_MAX_NR_PATTERN - 1); ) {
		v = !!simple_strtoul(p, NULL, 10);
		if (patt->value[curr] != v)
			curr++;

		patt->value[curr] = v;
		patt->interval[curr] += msecs_to_jiffies(interval);

		while (*p && isdigit(*p))
			p++;
		while (*p && !isdigit(*p))
			p++;
	}
	if (!curr) {
		dbg_bl("Unknown user defined pattern.\n");
		return;
	}

	patt->nr_pattern = ++curr;
	mutex_lock(&bled_list_lock);
	if ((bp = find_bled_priv_by_gpio(gpio_nr))) {
		local_bh_disable();
		bp->udef_pattern = *patt;
		local_bh_enable();
	}
	mutex_unlock(&bled_list_lock);

	kfree(patt);
}

/**
 * Handler of /proc/bled write procedure
 * Set mode of a bled.
 * @cmd_str:	commmand string
 * 		set mode ## [0|1]
 */
static void wproc_set_mode(char *cmd_str)
{
	int r, gpio_nr, v;
	struct bled_priv *bp;
	struct udef_pattern_s *patt;

	/* cmd_str = "set mode ##
	 * ## means gpio_nr
	 */
	if (!cmd_str)
		return;

	if ((r = sscanf(cmd_str, "set mode %d %d", &gpio_nr, &v)) != 2) {
		dbg_bl("%s: invalid command [%s]\n", __func__, cmd_str);
		return;
	}

	if (gpio_nr < 0 || gpio_nr >= 0xFF || (v && v != 1))
		return;

	mutex_lock(&bled_list_lock);
	if ((bp = find_bled_priv_by_gpio(gpio_nr))) {
		switch (v) {
		case BLED_UDEF_PATTERN_MODE:
			patt = &bp->udef_pattern;
			if (!patt->nr_pattern) {
				dbg_bl("User defined pattern of GPIO#%d absent!\n", gpio_nr);
				break;
			}
			suspend_bled(bp);
			patt->curr = 0;
			bp->next_blink_ts = 0;
			bp->mode = v;
#ifdef USE_WORKQUEUE_INSTEAD_OF_TIMER
			mod_delayed_work(bp->timer_wq, &bp->timer_work, 0);
#else
			mod_timer(&bp->timer, jiffies);
#endif
			break;
		default:	/* BLED_NORMAL_MODE */
			if (bp->reset_check)
				bp->reset_check(bp);
			local_bh_disable();
			bp->mode = BLED_NORMAL_MODE;
			local_bh_enable();
			resume_bled(bp);
		}
	}
	mutex_unlock(&bled_list_lock);
}

static int bled_open(struct inode *inode, struct file *filp)
{
	int dev_num = 0;

	if ((dev_num = iminor(inode)) > 1)
		return -ENODEV;

	return 0;
}

static int bled_release(struct inode *inode, struct file *filp)
{
	int dev_num = 0;

	if ((dev_num = iminor(inode)) > 1)
		return -ENODEV;

	return 0;
}

/**
 * Get bottom-half type name of a bled
 * @bp:
 * @return:	pointer to "timer", "workqueue", etc
 */
static char *get_bhtype_str(struct bled_priv *bp)
{
	switch (bp->bh_type) {
	case BLED_BHTYPE_TIMER:
		return "t";
	case BLED_BHTYPE_HYBRID:
		return "h";
	default:
		return "?";
	}
	return "?";
}

static int proc_bled_show(struct seq_file *m, void *v)
{
	unsigned i;
	struct bled_priv *bp;
	struct udef_pattern_s *patt;
	const char *sep = "___________________________________________________________________________________";

	mutex_lock(&bled_list_lock);
	seq_printf(m, TFMT "%d\n", "bled start", bled_start);
	local_bh_disable();
	list_for_each_entry(bp, &bled_list, list) {
		seq_printf(m, "%s\n" TFMT, sep, "LED GPIO# / Id");
		for (i = 0; i < bp->gpio_count; ++i) {
			seq_printf(m, "%s%d (%s active)", i? "," : "",
				bp->gpio_nr[i], (bp->active_low[i])? "low":"high");
		}
		seq_printf(m, " / [");
		for (i = 0; i < bp->nr_users; ++i)
			seq_printf(m, "%s%s", i? "," : "", bp->id[i]);
		seq_printf(m, "]\n");
		seq_printf(m, TFMT "%d (%s) / %d;", "Type / State / Flags / LED",
			bp->type, get_bhtype_str(bp), bp->state);
		seq_printf(m, "%d(", bp->nr_users);
		for (i = 0; i < bp->nr_users; ++i)
			seq_printf(m, "%s%d", i? "," : "", bp->user_states[i]);
		seq_printf(m, ") / %x / %s\n", bp->flags, (bp->value)? "ON":"OFF");
		seq_printf(m, TFMT "%u (", "Mode / User defined pattern (ms)", bp->mode);
		patt = &bp->udef_pattern;
		if (!patt->nr_pattern) {
			seq_printf(m, "N/A\n");
		} else {
			for (i = 0; i < patt->nr_pattern; ++i) {
				seq_printf(m, "%d,%4u%s", !!patt->value[i],
					jiffies_to_msecs(patt->interval[i]),
					(i == patt->nr_pattern - 1)? ")\n" : "; ");
			}
		}

		if (bp->flags & BLED_FLAGS_DBG_CHECK_FUNC) {
			seq_printf(m, TFMT "%8lu bytes/%3u ms\n", "Blink threshold",
				bp->threshold, jiffies_to_msecs(bp->next_check_interval));
			seq_printf(m, TFMT "%4u/%4u ms (%3lu/%3lu jiffies)\n", "Blink / Check interval",
				jiffies_to_msecs(bp->next_blink_interval), jiffies_to_msecs(bp->next_check_interval),
			bp->next_blink_interval, bp->next_check_interval);
			seq_printf(m, TFMT "%10lu/%10lu/%10lu\n", "Curr / Blink / Next ts.",
				jiffies, bp->next_blink_ts, bp->next_check_ts);
#if 0
			seq_printf(m, TFMT "%p/%p\n", "GPIO API", bp->gpio_set, bp->gpio_get);
			seq_printf(m, TFMT "%p\n", "Check traffic", bp->check);
#endif
		}

		if (bp->type >= 0 && bp->type < BLED_TYPE_MAX && bled_type_printer[bp->type])
			bled_type_printer[bp->type](bp, m);
		if (bp->type == BLED_TYPE_SWPORTS_BLED && bled_type_printer[BLED_TYPE_NETDEV_BLED])
			bled_type_printer[BLED_TYPE_NETDEV_BLED](bp, m);
	}
	seq_printf(m, "%s\n\n", sep);
	local_bh_enable();
	mutex_unlock(&bled_list_lock);

	return 0;
}

static int proc_bled_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_bled_show, NULL);
}

/**
 * Main write procedure of /proc/bled
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
static int proc_bled_write(struct file* file, const char __user* buffer, size_t count, loff_t *ppos)
#else
static ssize_t proc_bled_write(struct file* file, const char __user* buffer, size_t count, loff_t *ppos)
#endif
{
	char *p, *buf = kmalloc(count, GFP_KERNEL);
	struct bled_wproc_handler_s *wh;

	if (!buf) {
		dbg_bl_v("allocate %zu bytes fail!\n", count);
		return count;
	}

	if(copy_from_user(buf, buffer, count)) {
		kfree(buf);
		return -EFAULT;
	}
	*(buf + count) = '\0';

	/* terminates at first non-printable character. */
	for (p = buf; *p; ++p) {
		if (isprint(*p))
			continue;

		*p = '\0';
		break;
	}

	/* Looking for suitable write procedure handler. */
	for (wh = &bled_wproc_handler[0]; wh->cmd && wh->func; ++wh) {
		if (strncmp(buf, wh->cmd, strlen(wh->cmd)))
			continue;

		wh->func(buf);
		break;
	}

	kfree(buf);

	return count;
}

static const struct file_operations proc_bled_operations = {
	.open		= proc_bled_open,
	.read		= seq_read,
	.write		= proc_bled_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * Handle BLED_CTL_CHG_STATE ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to  struct bled_common)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_chg_state(unsigned long arg)
{
	int i, idx, new_state = BLED_STATE_MAX;
	struct bled_common bled, *bc = &bled;
	struct bled_priv *bp;

	if (copy_from_user(bc, (void __user *) arg, sizeof(struct bled_common)))
		return -EFAULT;

	if (validate_bled(bc, 0))
		return -EINVAL;
	if (!(bp = find_bled_priv_by_gpio(bc->gpio_nr)))
		return -ENODEV;

	dbg_bl("GPIO#%d LED %3s state [%d] (%d-%d,%d), id [%s] next state [%d]\n", bp->gpio_nr[0], (bp->value)? "ON":"OFF", bp->state, bp->nr_users, bp->user_states[0], bp->user_states[1], bc->id, bc->state);
	if (bp->nr_users > 1) {
		idx = idx_of_id(bp, bc->id);
		if (idx < 0 || idx >= bp->nr_users) {
			dbg_bl("Invalid idx %d of GPIO#%d. (id = %s)\n", idx, bp->gpio_nr[0], bc->id);
			return -ENODEV;
		}
		bp->user_states[idx] = bc->state;
		if (bp->state == bc->state)
			return 0;

		/* Recalculate state based on states of each bled of the GPIO PIN. */
		if (bp->state == BLED_STATE_STOP && bc->state == BLED_STATE_RUN) {
			new_state = BLED_STATE_RUN;
		} else if (bp->state == BLED_STATE_RUN && bc->state == BLED_STATE_STOP) {
			/* Switch to STOP if all user is STOP. */
			new_state = BLED_STATE_STOP;
			for (i = 0; i < bp->nr_users; ++i) {
				if (bp->user_states[i] == BLED_STATE_RUN) {
					new_state = BLED_STATE_RUN;
					break;
				}
			}
		}

		if (new_state < 0 || new_state >= BLED_STATE_MAX || new_state == bp->state)
			return 0;

		dbg_bl("GPIO#%d: state %d -> %d (%d:%d)\n", bc->gpio_nr, bp->state, new_state, idx, bc->state);
	} else {
		if (bp->state == bc->state)
			return 0;

		new_state = bc->state;
		dbg_bl("GPIO#%d: state %d -> %d\n", bc->gpio_nr, bp->state, bc->state);
	}

	switch (new_state) {
	case BLED_STATE_STOP:
		/* RUN -> STOP: always turn on LED in STOP state */
		suspend_bled(bp);
		bled_ctrl(bp, 1);
		break;
	case BLED_STATE_RUN:
		/* STOP -> RUN */
		resume_bled(bp);
		break;
	default:
		return -EINVAL;
	}
	bp->state = new_state;

	return 0;
}

/**
 * Handle BLED_CTL_ADD_NETDEV_BLED ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to  struct ndev_bled)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_add_netdev_bled(unsigned long arg)
{
	struct ndev_bled nl;
	struct bled_common *bc = &nl.bled;
	struct ndev_bled_priv *np;
	struct bled_priv *bp;
	struct ndev_bled_ifstat *ifs;
	int i;

	if (copy_from_user(&nl, (void __user *) arg, sizeof(nl)))
		return -EFAULT;
	if (validate_nd_bled(&nl))
		return -EINVAL;
	bp = add_bled(bc, sizeof(struct ndev_bled_priv), BLED_TYPE_NETDEV_BLED);
	if (IS_ERR(bp))
		return PTR_ERR(bp);

	np = to_check_priv(bp);
	for (i = 0, ifs = &np->ifstat[0]; i < np->nr_if; ++i, ++ifs) {
		if (!strcmp(ifs->ifname, nl.ifname))
			return -EEXIST;
	}
	if (np->nr_if >= ARRAY_SIZE(np->ifstat))
		return -ENOSPC;

	ifs = &np->ifstat[np->nr_if];
	/* netdev_bled-specific private data */
	bp->type = BLED_TYPE_NETDEV_BLED;
	bp->check = ndev_check_traffic;
	bp->reset_check = ndev_reset_check_traffic;
	strlcpy(ifs->ifname, nl.ifname, sizeof(ifs->ifname));
	if (!is_id_exist(bp, bc->id)) {
		strlcpy(bp->id[bp->nr_users], bc->id, sizeof(bp->id[bp->nr_users]));
		bp->nr_users++;
	}
	np->nr_if++;

	bp->reset_check(bp);
	__resume_bled(bp);
	printk("bled: GPIO#%d: netdev %s nr_users %d.\n", bp->gpio_nr[0], ifs->ifname, bp->nr_users);

	return 0;
}

/**
 * Handle BLED_CTL_DEL_BLED ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to  struct bled_common)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_del_bled(unsigned long arg)
{
	struct bled_common bled, *bc = &bled;
	struct bled_priv *bp;

	if (copy_from_user(bc, (void __user *) arg, sizeof(struct bled_common)))
		return -EFAULT;

	if (validate_bled(bc, 0))
		return -EINVAL;
	if (!(bp = find_bled_priv_by_gpio(bc->gpio_nr)))
		return -ENODEV;

	del_bled(bp, bc->id);

	return 0;
}

/**
 * Handle BLED_CTL_ADD_NETDEV_IF ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to  struct bled_common)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_add_netdev_if(unsigned long arg)
{
	int i;
	struct ndev_bled nl;
	struct bled_common *bc = &nl.bled;
	struct bled_priv *bp;
	struct ndev_bled_priv *np;
	struct swport_bled_priv *sp;
	struct ndev_bled_ifstat *ifs;

	if (copy_from_user(&nl, (void __user *) arg, sizeof(nl)))
		return -EFAULT;

	if (validate_bled(bc, 0))
		return -EINVAL;
	if (!(bp = find_bled_priv_by_gpio(bc->gpio_nr)))
		return -ENODEV;

	switch (bp->type) {
	case BLED_TYPE_NETDEV_BLED:
		np = to_check_priv(bp);
		break;
	case BLED_TYPE_SWPORTS_BLED:
		sp = to_check_priv(bp);
		np = &sp->ndev_priv;
		break;
	default:
		dbg_bl("%s: bled type %d is not supported!\n", __func__, bp->type);
		return -EINVAL;
	}

	for (i = 0, ifs = &np->ifstat[0]; i < np->nr_if; ++i, ++ifs) {
		if (!strcmp(ifs->ifname, nl.ifname))
			return -EEXIST;
	}
	if (np->nr_if >= ARRAY_SIZE(np->ifstat))
		return -ENOSPC;

	suspend_bled(bp);
	strlcpy(np->ifstat[np->nr_if].ifname, nl.ifname, sizeof(np->ifstat[np->nr_if].ifname));
	np->nr_if++;
	switch (bp->type) {
	case BLED_TYPE_NETDEV_BLED:
		if (bp->reset_check)
			bp->reset_check(bp);
		break;
	case BLED_TYPE_SWPORTS_BLED:
		if (bp->reset_check2)
			bp->reset_check2(bp);
		break;
	default:
		/* nothing */
		break;
	}
	resume_bled(bp);

	return 0;
}

/**
 * Handle BLED_CTL_DEL_NETDEV_IF ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to  struct bled_common)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_del_netdev_if(unsigned long arg)
{
	int i, j, ret = -ENODEV;
	struct ndev_bled nl;
	struct bled_common *bc = &nl.bled;
	struct bled_priv *bp;
	struct ndev_bled_priv *np;
	struct swport_bled_priv *sp;
	struct ndev_bled_ifstat *ifs, *next;

	if (copy_from_user(&nl, (void __user *) arg, sizeof(nl)))
		return -EFAULT;

	if (validate_bled(bc, 0))
		return -EINVAL;
	if (!(bp = find_bled_priv_by_gpio(bc->gpio_nr)))
		return -ENODEV;

	switch (bp->type) {
	case BLED_TYPE_NETDEV_BLED:
		np = to_check_priv(bp);
		break;
	case BLED_TYPE_SWPORTS_BLED:
		sp = to_check_priv(bp);
		np = &sp->ndev_priv;
		break;
	default:
		dbg_bl("%s: bled type %d is not supported!\n", __func__, bp->type);
		return -EINVAL;
	}

	for (i = 0, ifs = &np->ifstat[i]; i < np->nr_if; ++i, ++ifs) {
		if (strcmp(ifs->ifname, nl.ifname))
			continue;

		/* First interface of BLED_TYPE_NETDEV_BLED can't be removed. */
		if (i == 0 && bp->type == BLED_TYPE_NETDEV_BLED)
			return -EPERM;

		suspend_bled(bp);
		for (j = i + 1, next = &np->ifstat[j]; j < np->nr_if; ++j, ++ifs, ++next) {
			*ifs = *next;
		}
		memset(next, 0, sizeof(struct ndev_bled_ifstat));
		np->nr_if--;

		switch (bp->type) {
		case BLED_TYPE_NETDEV_BLED:
			if (bp->reset_check)
				bp->reset_check(bp);
			break;
		case BLED_TYPE_SWPORTS_BLED:
			if (bp->reset_check2)
				bp->reset_check2(bp);
			break;
		default:
			/* nothing */
			break;
		}
		resume_bled(bp);

		ret = 0;
		break;
	}

	return ret;
}

/**
 * Handle BLED_CTL_ADD_SWPORTS_BLED ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to  struct bled_common)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_add_swports_bled(unsigned long arg)
{
	struct swport_bled sl;
	struct bled_common *bc = &sl.bled;
	struct ndev_bled_priv *np;
	struct swport_bled_priv *sp;
	struct bled_priv *bp;

	if (copy_from_user(&sl, (void __user *) arg, sizeof(sl)))
		return -EFAULT;
	if (validate_sport_bled(&sl))
		return -EINVAL;
	bp = add_bled(bc, sizeof(struct swport_bled_priv), BLED_TYPE_SWPORTS_BLED);
	if (IS_ERR(bp))
		return PTR_ERR(bp);

	sp = to_check_priv(bp);
	/* netdev_bled-specific private data */
	bp->type = BLED_TYPE_SWPORTS_BLED;
	bp->check = swports_check_traffic;
	bp->reset_check = swports_reset_check_traffic;
	if (!is_id_exist(bp, bc->id)) {
		strlcpy(bp->id[bp->nr_users], bc->id, sizeof(bp->id[bp->nr_users]));
		bp->nr_users++;
	}
	sp->port_mask = sl.port_mask;
	sp->nr_port = calc_nr_ports(sp->port_mask);

	np = &sp->ndev_priv;
	bp->check2 = ndev_check_traffic;
	bp->reset_check2 = ndev_reset_check_traffic;
	bp->check2_priv = np;

	bp->reset_check(bp);
	bp->reset_check2(bp);
	__resume_bled(bp);
	printk("bled: GPIO#%d: switch ports mask %8x.\n", bp->gpio_nr[0], sp->port_mask);

	return 0;
}

/**
 * Handle BLED_CTL_UPD_SWPORTS_MASK ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to  struct bled_common)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_upd_swports_mask(unsigned long arg)
{
	struct swport_bled sl;
	struct bled_common *bc = &sl.bled;
	struct bled_priv *bp;
	struct swport_bled_priv *sp;

	if (copy_from_user(&sl, (void __user *) arg, sizeof(sl)))
		return -EFAULT;

	if (validate_bled(bc, 0))
		return -EINVAL;
	if (!(bp = find_bled_priv_by_gpio(bc->gpio_nr)))
		return -ENODEV;

	if (bp->type != BLED_TYPE_SWPORTS_BLED)
		return -EINVAL;

	sl.port_mask &= (1U << BLED_MAX_NR_SWPORTS) - 1;
	sp = to_check_priv(bp);

	suspend_bled(bp);
	sp->port_mask = sl.port_mask;
	sp->nr_port = calc_nr_ports(sp->port_mask);
	resume_bled(bp);

	return 0;
}

/**
 * Handle BLED_CTL_ADD_USBBUS_BLED ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to  struct usbbus_bled)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
#if defined(RTCONFIG_USB) && !defined(RTCONFIG_RALINK_MT7622) && !defined(RTCONFIG_RALINK_MT7629) && !defined(RTCONFIG_RALINK_MT7621)
static int handle_add_usbbus_bled(unsigned long arg)
{
	struct usbbus_bled ul;
	struct bled_common *bc = &ul.bled;
	struct usbbus_bled_priv *up;
	struct bled_priv *bp;
	struct usbbus_bled_stat *ifs;

	if (copy_from_user(&ul, (void __user *) arg, sizeof(ul)))
		return -EFAULT;
	if (validate_usbbus_bled(&ul))
		return -EINVAL;
	bp = add_bled(bc, sizeof(struct usbbus_bled_priv), BLED_TYPE_USBBUS_BLED);
	if (IS_ERR(bp))
		return PTR_ERR(bp);

	up = to_check_priv(bp);
	ifs = &up->busstat[0];
	/* netdev_bled-specific private data */
	bp->type = BLED_TYPE_USBBUS_BLED;
	bp->check = usbbus_check_traffic;
	bp->reset_check = usbbus_reset_check_traffic;
	if (!is_id_exist(bp, bc->id)) {
		strlcpy(bp->id[bp->nr_users], bc->id, sizeof(bp->id[bp->nr_users]));
		bp->nr_users++;
	}
	up->bus_mask = ul.bus_mask;
	up->nr_bus = calc_nr_bus(up->bus_mask);

	bp->reset_check(bp);
	__resume_bled(bp);
	printk("bled: GPIO#%d: USB BUS mask %8x.\n", bp->gpio_nr[0], up->bus_mask);

	return 0;
}
#endif

/**
 * Handle BLED_CTL_GET_BLED_TYPE ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to integer)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_get_bled_type(unsigned long arg)
{
	int gpio_nr;
	struct bled_priv *bp;

	if (get_user(gpio_nr, (int __user *) arg))
		return -EFAULT;

	if (!(bp = find_bled_priv_by_gpio(gpio_nr)))
		return -ENODEV;

	if (put_user(bp->type, (int __user *) arg))
		return -EFAULT;

	return 0;
}

/**
 * Handle BLED_CTL_SET_UDEF_PATTERN ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to integer)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_set_udef_pattern(unsigned long arg)
{
	unsigned int i, curr;
	struct bled_common bled, *bc = &bled;
	struct bled_priv *bp;
	struct udef_pattern_s *patt;

	if (copy_from_user(bc, (void __user *) arg, sizeof(struct bled_common)))
		return -EFAULT;

	if (bc->gpio_nr >= 0xFF)
		return -EINVAL;
	if (!bc->nr_pattern ||
	    bc->pattern_interval < BLED_UDEF_PATTERN_MIN_INTERVAL ||
	    bc->pattern_interval > BLED_UDEF_PATTERN_MAX_INTERVAL)
		return -EINVAL;
	if (!(bp = find_bled_priv_by_gpio(bc->gpio_nr)))
		return -ENODEV;

	if (!(patt = kzalloc(sizeof(struct udef_pattern_s), GFP_KERNEL))) {
		printk("%s: Allocate %zu bytes failed!\n", __func__, sizeof(struct udef_pattern_s));
		return -ENOMEM;
	}

	dbg_bl("GPIO#%d: set user defined pattern. (nr: %u, interval: %ums)\n",
		bc->gpio_nr, bc->nr_pattern, bc->pattern_interval);
	patt->value[0] = !!bc->pattern[0];
	for (i = 0, curr = 0; i < bc->nr_pattern && curr < (BLED_MAX_NR_PATTERN - 1); ++i) {
		if (patt->value[curr] != !!bc->pattern[i])
			curr++;

		patt->value[curr] = !!bc->pattern[i];
		patt->interval[curr] += msecs_to_jiffies(bc->pattern_interval);
	}
	patt->nr_pattern = ++curr;
	local_bh_disable();
	bp->udef_pattern = *patt;
	local_bh_enable();

	kfree(patt);

	return 0;
}

/**
 * Handle BLED_CTL_SET_UDEF_TRIGGER ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to integer)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_set_udef_trigger(unsigned long arg)
{
	unsigned int i;
	struct bled_common bled, *bc = &bled;
	struct bled_priv *bp;

	if (copy_from_user(bc, (void __user *) arg, sizeof(struct bled_common)))
		return -EFAULT;

	if (bc->gpio_nr >= 0xFF)
		return -EINVAL;
	if (!(bp = find_bled_priv_by_gpio(bc->gpio_nr)))
		return -ENODEV;

	local_bh_disable();
	for (i = 0; i < bp->gpio_count; i++)
		bp->udef_pattern.trigger[i] = !!bc->trigger[i];
	local_bh_enable();

	return 0;
}

/**
 * Handle BLED_CTL_SET_MODE ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to integer)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_set_mode(unsigned long arg)
{
	struct bled_common bled, *bc = &bled;
	struct bled_priv *bp;
	struct udef_pattern_s *patt;

	if (copy_from_user(bc, (void __user *) arg, sizeof(struct bled_common)))
		return -EFAULT;

	if (bc->gpio_nr >= 0xFF)
		return -EINVAL;
	if (!(bp = find_bled_priv_by_gpio(bc->gpio_nr)))
		return -ENODEV;

	if (bp->mode == bc->mode)
		return 0;

	dbg_bl("GPIO#%d: mode %d -> %d\n", bc->gpio_nr, bp->mode, bc->mode);
	switch (bc->mode) {
	case BLED_UDEF_PATTERN_MODE:
		patt = &bp->udef_pattern;
		if (!patt->nr_pattern) {
			dbg_bl("User defined pattern of GPIO#%d absent!\n", bc->gpio_nr);
			return -EINVAL;
		}
		suspend_bled(bp);
		patt->curr = 0;
		bp->next_blink_ts = 0;
		bp->mode = bc->mode;
#ifdef USE_WORKQUEUE_INSTEAD_OF_TIMER
		mod_delayed_work(bp->timer_wq, &bp->timer_work, 0);
#else
		mod_timer(&bp->timer, jiffies);
#endif
		break;
	case BLED_NORMAL_MODE:
		suspend_bled(bp);
		bp->mode = bc->mode;
		resume_bled(bp);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * Handle BLED_CTL_ADD_INTERRUPT_BLED ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to struct interrupt_bled)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_add_interrupt_bled(unsigned long arg)
{
	unsigned int i;
	struct interrupt_bled it;
	struct bled_common *bc = &it.bled;
	struct interrupt_bled_priv *ip;
	struct bled_priv *bp;
	struct interrupt_bled_stat *intrs;

	if (copy_from_user(&it, (void __user *) arg, sizeof(it)))
		return -EFAULT;
	if (validate_intr_bled(&it))
		return -EINVAL;
	bp = add_bled(bc, sizeof(struct interrupt_bled_priv), BLED_TYPE_INTERRUPT_BLED);
	if (IS_ERR(bp))
		return PTR_ERR(bp);

	ip = to_check_priv(bp);
	intrs = &ip->interrupt_stat[0];
	/* interrupt_bled-specific private data */
	bp->type = BLED_TYPE_INTERRUPT_BLED;
	bp->check = interrupt_check_traffic;
	bp->reset_check = interrupt_reset_check_traffic;
	if (!is_id_exist(bp, bc->id)) {
		strlcpy(bp->id[bp->nr_users], bc->id, sizeof(bp->id[bp->nr_users]));
		bp->nr_users++;
	}
	for (i = 0; i < it.nr_interrupt; ++i, ++intrs) {
		intrs->interrupt = it.interrupt[i];
	}
	ip->nr_interrupt = it.nr_interrupt;

	bp->reset_check(bp);
	__resume_bled(bp);
	printk("bled: GPIO#%d: interrupt %u.\n", bp->gpio_nr[0], intrs->interrupt);

	return 0;
}

/**
 * Handle BLED_CTL_ADD_GPIO ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to  struct bled_common)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_add_gpio(unsigned long arg)
{
	unsigned int i;
	struct bled_common bled, *bc = &bled;
	struct bled_priv *bp;

	if (copy_from_user(&bled, (void __user *) arg, sizeof(bled)))
		return -EFAULT;

	if (validate_bled(bc, 0))
		return -EINVAL;
	if (!(bp = find_bled_priv_by_gpio(bc->gpio_nr)))
		return -ENODEV;

	if ((bp->gpio_count + 1) > BLED_MAX_NR_GPIO_PER_BLED)
		return -ENOSPC;

	for (i = 0; i < bp->gpio_count; i++) {
		if (bp->gpio_nr[i] == bc->gpio2_nr)
			return -EPERM;
	}

	bp->gpio_nr[bp->gpio_count] = bc->gpio2_nr;
	bp->active_low[bp->gpio_count] = bc->active_low;
	bp->gpio_count++;

	return 0;
}

/**
 * Handle BLED_CTL_GET_SWPORTS_SETTINGS ioctl command.
 * @arg:	pointer to parameter in user-space
 * 		(pointer to  struct bled_common)
 * @return:
 * 	0:	success
 *  otherwise:	fail
 */
static int handle_get_swports_settings(unsigned long arg)
{
	int i;
	struct swport_bled sl;
	struct bled_common *bc = &sl.bled;
	struct ndev_bled_priv *np;
	struct bled_priv *bp;
	struct swport_bled_priv *sp;

	if (copy_from_user(&sl, (void __user *) arg, sizeof(sl)))
		return -EFAULT;

	if (validate_bled(bc, 0))
		return -EINVAL;
	if (!(bp = find_bled_priv_by_gpio(bc->gpio_nr)))
		return -ENODEV;

	if (bp->type != BLED_TYPE_SWPORTS_BLED)
		return -EINVAL;

	sp = to_check_priv(bp);
	np = &sp->ndev_priv;
	memset(&sl, 0, sizeof(sl));
	sl.port_mask = sp->port_mask;
	for (i = 0; i < np->nr_if; ++i) {
		strlcpy(sl.ifname[i], np->ifstat[i].ifname, IFNAMSIZ);
	}
	sl.nr_if = np->nr_if;

	return 0;
}

static long bled_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	mutex_lock(&bled_list_lock);

	switch (cmd) {
	case BLED_CTL_CHG_STATE:
		ret = handle_chg_state(arg);
		break;

	case BLED_CTL_ADD_NETDEV_BLED:
		ret = handle_add_netdev_bled(arg);
		break;

	case BLED_CTL_DEL_BLED:
		ret = handle_del_bled(arg);
		break;

	case BLED_CTL_ADD_NETDEV_IF:
		ret = handle_add_netdev_if(arg);
		break;

	case BLED_CTL_DEL_NETDEV_IF:
		ret = handle_del_netdev_if(arg);
		break;

#if !defined(RTCONFIG_RALINK_MT7629)
	case BLED_CTL_ADD_SWPORTS_BLED:
		ret = handle_add_swports_bled(arg);
		break;
#endif

	case BLED_CTL_UPD_SWPORTS_MASK:
		ret = handle_upd_swports_mask(arg);
		break;
#if !defined(RTCONFIG_RALINK_MT7622) && !defined(RTCONFIG_RALINK_MT7629) && !defined(RTCONFIG_RALINK_MT7621)
	case BLED_CTL_ADD_USBBUS_BLED:
		ret = handle_add_usbbus_bled(arg);
		break;
#endif
	case BLED_CTL_GET_BLED_TYPE:
		ret = handle_get_bled_type(arg);
		break;

	case BLED_CTL_SET_UDEF_PATTERN:
		ret = handle_set_udef_pattern(arg);
		break;

	case BLED_CTL_SET_UDEF_TRIGGER:
		ret = handle_set_udef_trigger(arg);
		break;

	case BLED_CTL_SET_MODE:
		ret = handle_set_mode(arg);
		break;

	case BLED_CTL_ADD_INTERRUPT_BLED:
		ret = handle_add_interrupt_bled(arg);
		break;

	case BLED_CTL_GET_SWPORTS_SETTINGS:
		ret = handle_get_swports_settings(arg);
		break;

	case BLED_CTL_ADD_GPIO:
		ret = handle_add_gpio(arg);
		break;
	}

	mutex_unlock(&bled_list_lock);

	return ret;
}

static void bled_cleanup(void)
{
	int i, c;
	struct bled_priv *bp, *next;

	mutex_lock(&bled_list_lock);
	list_for_each_entry_safe(bp, next, &bled_list, list) {
		c = bp->nr_users;
		for (i = 0; i < c; ++i) {
			del_bled(bp, bp->id[0]);
		}
	}

	if (bled_pdentry != NULL)	{
		remove_proc_entry(BLED_NAME, NULL);
	}
	if (bled_cdev) {
		cdev_del(bled_cdev);
		bled_cdev = NULL;
	}
	if (bled_class) {
		class_destroy(bled_class);
		bled_class = NULL;
	}
	if (bled_major) {
		unregister_chrdev_region(MKDEV(bled_major, 0), 1);
		bled_major = 0;
	}
	mutex_unlock(&bled_list_lock);
}

static int bled_register_cdev(dev_t dev)
{
	int rc = 0;

	if (bled_device) {
		device_destroy(bled_class, MKDEV(bled_major, 0));
		bled_device = NULL;
	}
	if (!(bled_cdev = cdev_alloc()))
		return -ENOMEM;

	bled_cdev->owner = THIS_MODULE;
	bled_cdev->ops = &bled_fops;
	bled_cdev->dev = dev;
	if (!(rc = cdev_add(bled_cdev, bled_cdev->dev, 1)))
		return 0;

	// register cdev fail, cleanup.
	kobject_put(&bled_cdev->kobj);
	cdev_del(bled_cdev);
	bled_cdev = NULL;

	return rc;
}

static int __init bled_init(void)
{
	int rc;
	dev_t dev;
	struct device *tmp_device;

	if ((rc = alloc_chrdev_region(&dev, 0, 1, BLED_NAME)) != 0)
		return rc;
	bled_major = MAJOR(dev);

	bled_class = class_create(THIS_MODULE, BLED_NAME);
	if (IS_ERR(bled_class)) {
		rc = PTR_ERR(bled_class);
		bled_class = NULL;
		goto cleanup;
	}

	INIT_LIST_HEAD(&bled_list);
	if ((rc = bled_register_cdev(dev)) != 0)
		goto cleanup;

	tmp_device = device_create(bled_class, NULL, MKDEV(bled_major, 0), "%s", BLED_NAME);
	if (IS_ERR(tmp_device)) {
		dbg_bl("Create device fail. ret %ld!\n", PTR_ERR(tmp_device));
		rc = PTR_ERR(tmp_device);
		goto cleanup;
	}
	bled_device = tmp_device;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,14)
	bled_pdentry = proc_create(BLED_NAME, 0444, NULL, &proc_bled_operations);
#else
	bled_pdentry = create_proc_entry(BLED_NAME, 0444, NULL);
#endif
	if (!bled_pdentry) {
		dbg_bl("Create %s fail!\n", BLED_NAME);
		rc  = -ENOMEM;
		goto cleanup;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,14)
	bled_pdentry->proc_fops = &proc_bled_operations;
#endif

	bled_start = 1;

	return 0;

cleanup:
	bled_cleanup();
	return rc;
}

static void __exit bled_exit(void)
{
	bled_cleanup();
	return;
}

module_init(bled_init);
module_exit(bled_exit);
MODULE_LICENSE("GPL");
