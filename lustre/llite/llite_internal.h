/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2017, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef LLITE_INTERNAL_H
#define LLITE_INTERNAL_H
#include <lustre_debug.h>
#include <lustre_disk.h>  /* for s2sbi */
#include <lustre_eacl.h>
#include <lustre_linkea.h>

/* for struct cl_lock_descr and struct cl_io */
#include <cl_object.h>
#include <lustre_lmv.h>
#include <lustre_mdc.h>
#include <lustre_intent.h>
#include <linux/compat.h>
#include <linux/aio.h>
#include <lustre_compat.h>

#include "vvp_internal.h"
#include "range_lock.h"
#include "pcc.h"

#ifndef FMODE_EXEC
#define FMODE_EXEC 0
#endif

#ifndef VM_FAULT_RETRY
#define VM_FAULT_RETRY 0
#endif

/* Kernel 3.1 kills LOOKUP_CONTINUE, LOOKUP_PARENT is equivalent to it.
 * seem kernel commit 49084c3bb2055c401f3493c13edae14d49128ca0 */
#ifndef LOOKUP_CONTINUE
#define LOOKUP_CONTINUE LOOKUP_PARENT
#endif

/** Only used on client-side for indicating the tail of dir hash/offset. */
#define LL_DIR_END_OFF          0x7fffffffffffffffULL
#define LL_DIR_END_OFF_32BIT    0x7fffffffUL

/* 4UL * 1024 * 1024 */
#define LL_MAX_BLKSIZE_BITS 22

#define LL_IT2STR(it) ((it) ? ldlm_it2str((it)->it_op) : "0")
#define LUSTRE_FPRIVATE(file) ((file)->private_data)

struct ll_dentry_data {
	struct lookup_intent		*lld_it;
	unsigned int			lld_sa_generation;
	unsigned int			lld_invalid:1;
	unsigned int			lld_nfs_dentry:1;
	struct rcu_head			lld_rcu_head;
};

#define ll_d2d(de) ((struct ll_dentry_data*)((de)->d_fsdata))

#define LLI_INODE_MAGIC                 0x111d0de5
#define LLI_INODE_DEAD                  0xdeadd00d

struct ll_getname_data {
#ifdef HAVE_DIR_CONTEXT
	struct dir_context	ctx;
#endif
	char		*lgd_name;	/* points to a buffer with NAME_MAX+1 size */
	struct lu_fid	lgd_fid;	/* target fid we are looking for */
	int		lgd_found;	/* inode matched? */
};

struct ll_grouplock {
	struct lu_env	*lg_env;
	struct cl_io	*lg_io;
	struct cl_lock	*lg_lock;
	unsigned long	 lg_gid;
};

struct ll_inode_info {
	__u32				lli_inode_magic;
	spinlock_t			lli_lock;

	volatile unsigned long		lli_flags;
	struct posix_acl		*lli_posix_acl;

	/* identifying fields for both metadata and data stacks. */
	struct lu_fid			lli_fid;
	/* master inode fid for stripe directory */
	struct lu_fid			lli_pfid;

	/* We need all three because every inode may be opened in different
	 * modes */
	struct obd_client_handle       *lli_mds_read_och;
	struct obd_client_handle       *lli_mds_write_och;
	struct obd_client_handle       *lli_mds_exec_och;
	__u64				lli_open_fd_read_count;
	__u64				lli_open_fd_write_count;
	__u64				lli_open_fd_exec_count;
	/* Protects access to och pointers and their usage counters */
	struct mutex			lli_och_mutex;

	struct inode			lli_vfs_inode;

	/* the most recent timestamps obtained from mds */
	s64				lli_atime;
	s64				lli_mtime;
	s64				lli_ctime;
	spinlock_t			lli_agl_lock;

	/* update atime from MDS no matter if it's older than
	 * local inode atime. */
	unsigned int	lli_update_atime:1;

	/* Try to make the d::member and f::member are aligned. Before using
	 * these members, make clear whether it is directory or not. */
	union {
		/* for directory */
		struct {
			/* serialize normal readdir and statahead-readdir. */
			struct mutex			lli_readdir_mutex;

			/* metadata statahead */
			/* since parent-child threads can share the same @file
			 * struct, "opendir_key" is the token when dir close for
			 * case of parent exit before child -- it is me should
			 * cleanup the dir readahead. */
			void			       *lli_opendir_key;
			struct ll_statahead_info       *lli_sai;
			/* protect statahead stuff. */
			spinlock_t			lli_sa_lock;
			/* "opendir_pid" is the token when lookup/revalid
			 * -- I am the owner of dir statahead. */
			pid_t				lli_opendir_pid;
			/* stat will try to access statahead entries or start
			 * statahead if this flag is set, and this flag will be
			 * set upon dir open, and cleared when dir is closed,
			 * statahead hit ratio is too low, or start statahead
			 * thread failed. */
			unsigned int			lli_sa_enabled:1;
			/* generation for statahead */
			unsigned int			lli_sa_generation;
			/* rw lock protects lli_lsm_md */
			struct rw_semaphore		lli_lsm_sem;
			/* directory stripe information */
			struct lmv_stripe_md		*lli_lsm_md;
			/* directory default LMV */
			struct lmv_stripe_md		*lli_default_lsm_md;
		};

		/* for non-directory */
		struct {
			struct mutex		lli_size_mutex;
			char		       *lli_symlink_name;
			/*
			 * struct rw_semaphore {
			 *    signed long	count;     // align d.d_def_acl
			 *    spinlock_t	wait_lock; // align d.d_sa_lock
			 *    struct list_head wait_list;
			 * }
			 */
			struct rw_semaphore	lli_trunc_sem;
			struct range_lock_tree	lli_write_tree;

			struct rw_semaphore	lli_glimpse_sem;
			ktime_t			lli_glimpse_time;
			struct list_head	lli_agl_list;
			__u64			lli_agl_index;

			/* for writepage() only to communicate to fsync */
			int			lli_async_rc;

			/* protect the file heat fields */
			spinlock_t			lli_heat_lock;
			__u32				lli_heat_flags;
			struct obd_heat_instance	lli_heat_instances[OBD_HEAT_COUNT];

			/*
			 * Whenever a process try to read/write the file, the
			 * jobid of the process will be saved here, and it'll
			 * be packed into the write PRC when flush later.
			 *
			 * So the read/write statistics for jobid will not be
			 * accurate if the file is shared by different jobs.
			 */
			char                    lli_jobid[LUSTRE_JOBID_SIZE];

			struct mutex		 lli_pcc_lock;
			enum lu_pcc_state_flags	 lli_pcc_state;
			struct pcc_inode	*lli_pcc_inode;
		};
	};

        /* XXX: For following frequent used members, although they maybe special
         *      used for non-directory object, it is some time-wasting to check
         *      whether the object is directory or not before using them. On the
         *      other hand, currently, sizeof(f) > sizeof(d), it cannot reduce
         *      the "ll_inode_info" size even if moving those members into u.f.
         *      So keep them out side.
         *
         *      In the future, if more members are added only for directory,
         *      some of the following members can be moved into u.f.
         */
	struct cl_object		*lli_clob;

	/* mutex to request for layout lock exclusively. */
	struct mutex			lli_layout_mutex;
	/* Layout version, protected by lli_layout_lock */
	__u32				lli_layout_gen;
	spinlock_t			lli_layout_lock;

	__u32				lli_projid;   /* project id */

	struct rw_semaphore		lli_xattrs_list_rwsem;
	struct mutex			lli_xattrs_enq_lock;
	struct list_head		lli_xattrs; /* ll_xattr_entry->xe_list */
};

static inline __u32 ll_layout_version_get(struct ll_inode_info *lli)
{
	__u32 gen;

	spin_lock(&lli->lli_layout_lock);
	gen = lli->lli_layout_gen;
	spin_unlock(&lli->lli_layout_lock);

	return gen;
}

static inline void ll_layout_version_set(struct ll_inode_info *lli, __u32 gen)
{
	spin_lock(&lli->lli_layout_lock);
	lli->lli_layout_gen = gen;
	spin_unlock(&lli->lli_layout_lock);
}

enum ll_file_flags {
	/* File data is modified. */
	LLIF_DATA_MODIFIED      = 0,
	/* File is being restored */
	LLIF_FILE_RESTORING	= 1,
	/* Xattr cache is attached to the file */
	LLIF_XATTR_CACHE	= 2,
	/* Project inherit */
	LLIF_PROJECT_INHERIT	= 3,
};

static inline void ll_file_set_flag(struct ll_inode_info *lli,
				    enum ll_file_flags flag)
{
	set_bit(flag, &lli->lli_flags);
}

static inline void ll_file_clear_flag(struct ll_inode_info *lli,
				      enum ll_file_flags flag)
{
	clear_bit(flag, &lli->lli_flags);
}

static inline bool ll_file_test_flag(struct ll_inode_info *lli,
				     enum ll_file_flags flag)
{
	return test_bit(flag, &lli->lli_flags);
}

static inline bool ll_file_test_and_clear_flag(struct ll_inode_info *lli,
					       enum ll_file_flags flag)
{
	return test_and_clear_bit(flag, &lli->lli_flags);
}

int ll_xattr_cache_destroy(struct inode *inode);

int ll_xattr_cache_get(struct inode *inode,
			const char *name,
			char *buffer,
			size_t size,
			__u64 valid);

static inline bool obd_connect_has_secctx(struct obd_connect_data *data)
{
#if defined(HAVE_SECURITY_DENTRY_INIT_SECURITY) && defined(CONFIG_SECURITY)
	return data->ocd_connect_flags & OBD_CONNECT_FLAGS2 &&
	       data->ocd_connect_flags2 & OBD_CONNECT2_FILE_SECCTX;
#else
	return false;
#endif /* HAVE_SECURITY_DENTRY_INIT_SECURITY */
}

static inline void obd_connect_set_secctx(struct obd_connect_data *data)
{
#if defined(HAVE_SECURITY_DENTRY_INIT_SECURITY) && defined(CONFIG_SECURITY)
	data->ocd_connect_flags2 |= OBD_CONNECT2_FILE_SECCTX;
#endif /* HAVE_SECURITY_DENTRY_INIT_SECURITY */
}

int ll_dentry_init_security(struct dentry *dentry, int mode, struct qstr *name,
			    const char **secctx_name, void **secctx,
			    __u32 *secctx_size);
int ll_inode_init_security(struct dentry *dentry, struct inode *inode,
			   struct inode *dir);

int ll_listsecurity(struct inode *inode, char *secctx_name,
		    size_t secctx_name_size);

/*
 * Locking to guarantee consistency of non-atomic updates to long long i_size,
 * consistency between file size and KMS.
 *
 * Implemented by ->lli_size_mutex and ->lsm_lock, nested in that order.
 */

void ll_inode_size_lock(struct inode *inode);
void ll_inode_size_unlock(struct inode *inode);

static inline struct ll_inode_info *ll_i2info(struct inode *inode)
{
	return container_of(inode, struct ll_inode_info, lli_vfs_inode);
}

static inline struct pcc_inode *ll_i2pcci(struct inode *inode)
{
	return ll_i2info(inode)->lli_pcc_inode;
}

/* default to use at least 16M for fast read if possible */
#define RA_REMAIN_WINDOW_MIN			MiB_TO_PAGES(16UL)

/* default to about 64M of readahead on a given system. */
#define SBI_DEFAULT_READAHEAD_MAX		MiB_TO_PAGES(64UL)

/* default to read-ahead full files smaller than 2MB on the second read */
#define SBI_DEFAULT_READAHEAD_WHOLE_MAX		MiB_TO_PAGES(2UL)

enum ra_stat {
        RA_STAT_HIT = 0,
        RA_STAT_MISS,
        RA_STAT_DISTANT_READPAGE,
        RA_STAT_MISS_IN_WINDOW,
        RA_STAT_FAILED_GRAB_PAGE,
        RA_STAT_FAILED_MATCH,
        RA_STAT_DISCARDED,
        RA_STAT_ZERO_LEN,
        RA_STAT_ZERO_WINDOW,
        RA_STAT_EOF,
        RA_STAT_MAX_IN_FLIGHT,
        RA_STAT_WRONG_GRAB_PAGE,
	RA_STAT_FAILED_REACH_END,
	RA_STAT_ASYNC,
	RA_STAT_FAILED_FAST_READ,
	_NR_RA_STAT,
};

struct ll_ra_info {
	atomic_t	ra_cur_pages;
	unsigned long	ra_max_pages;
	unsigned long	ra_max_pages_per_file;
	unsigned long	ra_max_read_ahead_whole_pages;
	struct workqueue_struct  *ll_readahead_wq;
	/*
	 * Max number of active works for readahead workqueue,
	 * default is 0 which make workqueue init number itself,
	 * unless there is a specific need for throttling the
	 * number of active work items, specifying '0' is recommended.
	 */
	unsigned int ra_async_max_active;
	/* Threshold to control when to trigger async readahead */
	unsigned long ra_async_pages_per_file_threshold;
};

/* ra_io_arg will be filled in the beginning of ll_readahead with
 * ras_lock, then the following ll_read_ahead_pages will read RA
 * pages according to this arg, all the items in this structure are
 * counted by page index.
 */
struct ra_io_arg {
	unsigned long ria_start;  /* start offset of read-ahead*/
	unsigned long ria_end;    /* end offset of read-ahead*/
	unsigned long ria_reserved; /* reserved pages for read-ahead */
	unsigned long ria_end_min;  /* minimum end to cover current read */
	bool          ria_eof;    /* reach end of file */
	/* If stride read pattern is detected, ria_stoff means where
	 * stride read is started. Note: for normal read-ahead, the
	 * value here is meaningless, and also it will not be accessed*/
	pgoff_t ria_stoff;
	/* ria_length and ria_pages are the length and pages length in the
	 * stride I/O mode. And they will also be used to check whether
	 * it is stride I/O read-ahead in the read-ahead pages*/
	unsigned long ria_length;
	unsigned long ria_pages;
};

/* LL_HIST_MAX=32 causes an overflow */
#define LL_HIST_MAX 28
#define LL_HIST_START 12 /* buckets start at 2^12 = 4k */
#define LL_PROCESS_HIST_MAX 10
struct per_process_info {
        pid_t pid;
        struct obd_histogram pp_r_hist;
        struct obd_histogram pp_w_hist;
};

/* pp_extents[LL_PROCESS_HIST_MAX] will hold the combined process info */
struct ll_rw_extents_info {
        struct per_process_info pp_extents[LL_PROCESS_HIST_MAX + 1];
};

#define LL_OFFSET_HIST_MAX 100
struct ll_rw_process_info {
        pid_t                     rw_pid;
        int                       rw_op;
        loff_t                    rw_range_start;
        loff_t                    rw_range_end;
        loff_t                    rw_last_file_pos;
        loff_t                    rw_offset;
        size_t                    rw_smallest_extent;
        size_t                    rw_largest_extent;
        struct ll_file_data      *rw_last_file;
};

enum stats_track_type {
        STATS_TRACK_ALL = 0,  /* track all processes */
        STATS_TRACK_PID,      /* track process with this pid */
        STATS_TRACK_PPID,     /* track processes with this ppid */
        STATS_TRACK_GID,      /* track processes with this gid */
        STATS_TRACK_LAST,
};

/* flags for sbi->ll_flags */
#define LL_SBI_NOLCK             0x01 /* DLM locking disabled (directio-only) */
#define LL_SBI_CHECKSUM          0x02 /* checksum each page as it's written */
#define LL_SBI_FLOCK             0x04
#define LL_SBI_USER_XATTR        0x08 /* support user xattr */
#define LL_SBI_ACL               0x10 /* support ACL */
/*	LL_SBI_RMT_CLIENT        0x40    remote client */
#define LL_SBI_MDS_CAPA          0x80 /* support mds capa, obsolete */
#define LL_SBI_OSS_CAPA         0x100 /* support oss capa, obsolete */
#define LL_SBI_LOCALFLOCK       0x200 /* Local flocks support by kernel */
#define LL_SBI_LRU_RESIZE       0x400 /* lru resize support */
#define LL_SBI_LAZYSTATFS       0x800 /* lazystatfs mount option */
/*	LL_SBI_SOM_PREVIEW     0x1000    SOM preview mount option, obsolete */
#define LL_SBI_32BIT_API       0x2000 /* generate 32 bit inodes. */
#define LL_SBI_64BIT_HASH      0x4000 /* support 64-bits dir hash/offset */
#define LL_SBI_AGL_ENABLED     0x8000 /* enable agl */
#define LL_SBI_VERBOSE        0x10000 /* verbose mount/umount */
#define LL_SBI_LAYOUT_LOCK    0x20000 /* layout lock support */
#define LL_SBI_USER_FID2PATH  0x40000 /* allow fid2path by unprivileged users */
#define LL_SBI_XATTR_CACHE    0x80000 /* support for xattr cache */
#define LL_SBI_NOROOTSQUASH  0x100000 /* do not apply root squash */
#define LL_SBI_ALWAYS_PING   0x200000 /* always ping even if server
				       * suppress_pings */
#define LL_SBI_FAST_READ     0x400000 /* fast read support */
#define LL_SBI_FILE_SECCTX   0x800000 /* set file security context at create */
/*	LL_SBI_PIO	    0x1000000    parallel IO support, introduced in
					 2.10, abandoned */
#define LL_SBI_TINY_WRITE   0x2000000 /* tiny write support */
#define LL_SBI_FILE_HEAT    0x4000000 /* file heat support */
#define LL_SBI_FLAGS { 	\
	"nolck",	\
	"checksum",	\
	"flock",	\
	"user_xattr",	\
	"acl",		\
	"???",		\
	"???",		\
	"mds_capa",	\
	"oss_capa",	\
	"flock",	\
	"lru_resize",	\
	"lazy_statfs",	\
	"som",		\
	"32bit_api",	\
	"64bit_hash",	\
	"agl",		\
	"verbose",	\
	"layout",	\
	"user_fid2path",\
	"xattr_cache",	\
	"norootsquash",	\
	"always_ping",	\
	"fast_read",	\
	"file_secctx",	\
	"pio",		\
	"tiny_write",	\
	"file_heat",	\
}

/* This is embedded into llite super-blocks to keep track of connect
 * flags (capabilities) supported by all imports given mount is
 * connected to. */
struct lustre_client_ocd {
	/* This is conjunction of connect_flags across all imports
	 * (LOVs) this mount is connected to. This field is updated by
	 * cl_ocd_update() under ->lco_lock. */
	__u64			 lco_flags;
	struct mutex		 lco_lock;
	struct obd_export	*lco_md_exp;
	struct obd_export	*lco_dt_exp;
};

struct ll_sb_info {
	/* this protects pglist and ra_info.  It isn't safe to
	 * grab from interrupt contexts */
	spinlock_t		 ll_lock;
	spinlock_t		 ll_pp_extent_lock; /* pp_extent entry*/
	spinlock_t		 ll_process_lock; /* ll_rw_process_info */
	struct obd_uuid		 ll_sb_uuid;
	struct obd_export	*ll_md_exp;
	struct obd_export	*ll_dt_exp;
	struct obd_device	*ll_md_obd;
	struct obd_device	*ll_dt_obd;
	struct dentry		*ll_debugfs_entry;
	struct lu_fid		 ll_root_fid; /* root object fid */

        int                       ll_flags;
	unsigned int		  ll_umounting:1,
				  ll_xattr_cache_enabled:1,
				  ll_xattr_cache_set:1, /* already set to 0/1 */
				  ll_client_common_fill_super_succeeded:1,
				  ll_checksum_set:1;

        struct lustre_client_ocd  ll_lco;

        struct lprocfs_stats     *ll_stats; /* lprocfs stats counter */

	/* Used to track "unstable" pages on a client, and maintain a
	 * LRU list of clean pages. An "unstable" page is defined as
	 * any page which is sent to a server as part of a bulk request,
	 * but is uncommitted to stable storage. */
	struct cl_client_cache	 *ll_cache;

        struct lprocfs_stats     *ll_ra_stats;

        struct ll_ra_info         ll_ra_info;
        unsigned int              ll_namelen;
        struct file_operations   *ll_fop;

        struct lu_site           *ll_site;
        struct cl_device         *ll_cl;
        /* Statistics */
        struct ll_rw_extents_info ll_rw_extents_info;
        int                       ll_extent_process_count;
        struct ll_rw_process_info ll_rw_process_info[LL_PROCESS_HIST_MAX];
        unsigned int              ll_offset_process_count;
        struct ll_rw_process_info ll_rw_offset_info[LL_OFFSET_HIST_MAX];
        unsigned int              ll_rw_offset_entry_count;
        int                       ll_stats_track_id;
        enum stats_track_type     ll_stats_track_type;
        int                       ll_rw_stats_on;

	/* metadata stat-ahead */
	unsigned int		  ll_sa_running_max;/* max concurrent
						     * statahead instances */
	unsigned int		  ll_sa_max;     /* max statahead RPCs */
	atomic_t		  ll_sa_total;   /* statahead thread started
						  * count */
	atomic_t		  ll_sa_wrong;   /* statahead thread stopped for
						  * low hit ratio */
	atomic_t		  ll_sa_running; /* running statahead thread
						  * count */
	atomic_t		  ll_agl_total;  /* AGL thread started count */

	dev_t			  ll_sdev_orig; /* save s_dev before assign for
						 * clustred nfs */
	/* root squash */
	struct root_squash_info	  ll_squash;
	struct path		  ll_mnt;

	/* st_blksize returned by stat(2), when non-zero */
	unsigned int		  ll_stat_blksize;

	/* maximum relative age of cached statfs results */
	unsigned int		  ll_statfs_max_age;

	struct kset		  ll_kset;	/* sysfs object */
	struct completion	  ll_kobj_unregister;

	/* File heat */
	unsigned int		  ll_heat_decay_weight;
	unsigned int		  ll_heat_period_second;

	/* filesystem fsname */
	char			  ll_fsname[LUSTRE_MAXFSNAME + 1];

	/* Persistent Client Cache */
	struct pcc_super	  ll_pcc_super;
};

#define SBI_DEFAULT_HEAT_DECAY_WEIGHT	((80 * 256 + 50) / 100)
#define SBI_DEFAULT_HEAT_PERIOD_SECOND	(60)
/*
 * per file-descriptor read-ahead data.
 */
struct ll_readahead_state {
	spinlock_t  ras_lock;
        /*
         * index of the last page that read(2) needed and that wasn't in the
         * cache. Used by ras_update() to detect seeks.
         *
         * XXX nikita: if access seeks into cached region, Lustre doesn't see
         * this.
         */
        unsigned long   ras_last_readpage;
        /*
         * number of pages read after last read-ahead window reset. As window
         * is reset on each seek, this is effectively a number of consecutive
         * accesses. Maybe ->ras_accessed_in_window is better name.
         *
         * XXX nikita: window is also reset (by ras_update()) when Lustre
         * believes that memory pressure evicts read-ahead pages. In that
         * case, it probably doesn't make sense to expand window to
         * PTLRPC_MAX_BRW_PAGES on the third access.
         */
        unsigned long   ras_consecutive_pages;
        /*
         * number of read requests after the last read-ahead window reset
         * As window is reset on each seek, this is effectively the number
         * on consecutive read request and is used to trigger read-ahead.
         */
        unsigned long   ras_consecutive_requests;
        /*
         * Parameters of current read-ahead window. Handled by
         * ras_update(). On the initial access to the file or after a seek,
         * window is reset to 0. After 3 consecutive accesses, window is
         * expanded to PTLRPC_MAX_BRW_PAGES. Afterwards, window is enlarged by
         * PTLRPC_MAX_BRW_PAGES chunks up to ->ra_max_pages.
         */
        unsigned long   ras_window_start, ras_window_len;
	/*
	 * Optimal RPC size. It decides how many pages will be sent
	 * for each read-ahead.
	 */
	unsigned long	ras_rpc_size;
        /*
         * Where next read-ahead should start at. This lies within read-ahead
         * window. Read-ahead window is read in pieces rather than at once
         * because: 1. lustre limits total number of pages under read-ahead by
         * ->ra_max_pages (see ll_ra_count_get()), 2. client cannot read pages
         * not covered by DLM lock.
         */
        unsigned long   ras_next_readahead;
        /*
         * Total number of ll_file_read requests issued, reads originating
         * due to mmap are not counted in this total.  This value is used to
         * trigger full file read-ahead after multiple reads to a small file.
         */
        unsigned long   ras_requests;
        /*
         * Page index with respect to the current request, these value
         * will not be accurate when dealing with reads issued via mmap.
         */
        unsigned long   ras_request_index;
        /*
         * The following 3 items are used for detecting the stride I/O
         * mode.
         * In stride I/O mode,
         * ...............|-----data-----|****gap*****|--------|******|....
         *    offset      |-stride_pages-|-stride_gap-|
         * ras_stride_offset = offset;
         * ras_stride_length = stride_pages + stride_gap;
         * ras_stride_pages = stride_pages;
         * Note: all these three items are counted by pages.
         */
        unsigned long   ras_stride_length;
        unsigned long   ras_stride_pages;
        pgoff_t         ras_stride_offset;
        /*
         * number of consecutive stride request count, and it is similar as
         * ras_consecutive_requests, but used for stride I/O mode.
         * Note: only more than 2 consecutive stride request are detected,
         * stride read-ahead will be enable
         */
        unsigned long   ras_consecutive_stride_requests;
	/* index of the last page that async readahead starts */
	unsigned long	ras_async_last_readpage;
};

struct ll_readahead_work {
	/** File to readahead */
	struct file			*lrw_file;
	/** Start bytes */
	unsigned long			 lrw_start;
	/** End bytes */
	unsigned long			 lrw_end;

	/* async worker to handler read */
	struct work_struct		 lrw_readahead_work;
};

extern struct kmem_cache *ll_file_data_slab;
struct lustre_handle;
struct ll_file_data {
	struct ll_readahead_state fd_ras;
	struct ll_grouplock fd_grouplock;
	__u64 lfd_pos;
	__u32 fd_flags;
	fmode_t fd_omode;
	/* openhandle if lease exists for this file.
	 * Borrow lli->lli_och_mutex to protect assignment */
	struct obd_client_handle *fd_lease_och;
	struct obd_client_handle *fd_och;
	struct file *fd_file;
	/* Indicate whether need to report failure when close.
	 * true: failure is known, not report again.
	 * false: unknown failure, should report. */
	bool fd_write_failed;
	bool ll_lock_no_expand;
	rwlock_t fd_lock; /* protect lcc list */
	struct list_head fd_lccs; /* list of ll_cl_context */
	/* Used by mirrored file to lead IOs to a specific mirror, usually
	 * for mirror resync. 0 means default. */
	__u32 fd_designated_mirror;
	/* The layout version when resync starts. Resync I/O should carry this
	 * layout version for verification to OST objects */
	__u32 fd_layout_version;
	struct pcc_file fd_pcc_file;
};

void llite_tunables_unregister(void);
int llite_tunables_register(void);

static inline struct inode *ll_info2i(struct ll_inode_info *lli)
{
        return &lli->lli_vfs_inode;
}

__u32 ll_i2suppgid(struct inode *i);
void ll_i2gids(__u32 *suppgids, struct inode *i1,struct inode *i2);

static inline int ll_need_32bit_api(struct ll_sb_info *sbi)
{
#if BITS_PER_LONG == 32
	return 1;
#elif defined(CONFIG_COMPAT)
	if (unlikely(sbi->ll_flags & LL_SBI_32BIT_API))
		return true;

# ifdef CONFIG_X86_X32
	/* in_compat_syscall() returns true when called from a kthread
	 * and CONFIG_X86_X32 is enabled, which is wrong. So check
	 * whether the caller comes from a syscall (ie. not a kthread)
	 * before calling in_compat_syscall(). */
	if (current->flags & PF_KTHREAD)
		return false;
# endif

	return unlikely(in_compat_syscall());
#else
	return unlikely(sbi->ll_flags & LL_SBI_32BIT_API);
#endif
}

static inline bool ll_sbi_has_fast_read(struct ll_sb_info *sbi)
{
	return !!(sbi->ll_flags & LL_SBI_FAST_READ);
}

static inline bool ll_sbi_has_tiny_write(struct ll_sb_info *sbi)
{
	return !!(sbi->ll_flags & LL_SBI_TINY_WRITE);
}

static inline bool ll_sbi_has_file_heat(struct ll_sb_info *sbi)
{
	return !!(sbi->ll_flags & LL_SBI_FILE_HEAT);
}

void ll_ras_enter(struct file *f);

/* llite/lcommon_misc.c */
int cl_ocd_update(struct obd_device *host, struct obd_device *watched,
		  enum obd_notify_event ev, void *owner);
int cl_get_grouplock(struct cl_object *obj, unsigned long gid, int nonblock,
		     struct ll_grouplock *lg);
void cl_put_grouplock(struct ll_grouplock *lg);

/* llite/lproc_llite.c */
int ll_debugfs_register_super(struct super_block *sb, const char *name);
void ll_debugfs_unregister_super(struct super_block *sb);
void ll_stats_ops_tally(struct ll_sb_info *sbi, int op, int count);

enum {
	LPROC_LL_READ_BYTES,
	LPROC_LL_WRITE_BYTES,
	LPROC_LL_IOCTL,
	LPROC_LL_OPEN,
	LPROC_LL_RELEASE,
	LPROC_LL_MAP,
	LPROC_LL_FAULT,
	LPROC_LL_MKWRITE,
	LPROC_LL_LLSEEK,
	LPROC_LL_FSYNC,
	LPROC_LL_READDIR,
	LPROC_LL_SETATTR,
	LPROC_LL_TRUNC,
	LPROC_LL_FLOCK,
	LPROC_LL_GETATTR,
	LPROC_LL_CREATE,
	LPROC_LL_LINK,
	LPROC_LL_UNLINK,
	LPROC_LL_SYMLINK,
	LPROC_LL_MKDIR,
	LPROC_LL_RMDIR,
	LPROC_LL_MKNOD,
	LPROC_LL_RENAME,
	LPROC_LL_STATFS,
	LPROC_LL_ALLOC_INODE,
	LPROC_LL_SETXATTR,
	LPROC_LL_GETXATTR,
	LPROC_LL_GETXATTR_HITS,
	LPROC_LL_LISTXATTR,
	LPROC_LL_REMOVEXATTR,
	LPROC_LL_INODE_PERM,
	LPROC_LL_FILE_OPCODES
};

/* llite/dir.c */
struct ll_dir_chain {
};

static inline void ll_dir_chain_init(struct ll_dir_chain *chain)
{
}

static inline void ll_dir_chain_fini(struct ll_dir_chain *chain)
{
}

extern const struct file_operations ll_dir_operations;
extern const struct inode_operations ll_dir_inode_operations;
#ifdef HAVE_DIR_CONTEXT
int ll_dir_read(struct inode *inode, __u64 *pos, struct md_op_data *op_data,
		struct dir_context *ctx);
#else
int ll_dir_read(struct inode *inode, __u64 *pos, struct md_op_data *op_data,
		void *cookie, filldir_t filldir);
#endif
int ll_get_mdt_idx(struct inode *inode);
int ll_get_mdt_idx_by_fid(struct ll_sb_info *sbi, const struct lu_fid *fid);
struct page *ll_get_dir_page(struct inode *dir, struct md_op_data *op_data,
			     __u64 offset, struct ll_dir_chain *chain);
void ll_release_page(struct inode *inode, struct page *page, bool remove);

/* llite/namei.c */
extern const struct inode_operations ll_special_inode_operations;

struct inode *ll_iget(struct super_block *sb, ino_t hash,
                      struct lustre_md *lic);
int ll_test_inode_by_fid(struct inode *inode, void *opaque);
int ll_md_blocking_ast(struct ldlm_lock *, struct ldlm_lock_desc *,
                       void *data, int flag);
struct dentry *ll_splice_alias(struct inode *inode, struct dentry *de);
int ll_rmdir_entry(struct inode *dir, char *name, int namelen);
void ll_update_times(struct ptlrpc_request *request, struct inode *inode);

/* llite/rw.c */
int ll_writepage(struct page *page, struct writeback_control *wbc);
int ll_writepages(struct address_space *, struct writeback_control *wbc);
int ll_readpage(struct file *file, struct page *page);
int ll_io_read_page(const struct lu_env *env, struct cl_io *io,
			   struct cl_page *page, struct file *file);
void ll_readahead_init(struct inode *inode, struct ll_readahead_state *ras);
int vvp_io_write_commit(const struct lu_env *env, struct cl_io *io);

enum lcc_type;
void ll_cl_add(struct file *file, const struct lu_env *env, struct cl_io *io,
	       enum lcc_type type);
void ll_cl_remove(struct file *file, const struct lu_env *env);
struct ll_cl_context *ll_cl_find(struct file *file);

extern const struct address_space_operations ll_aops;

/* llite/file.c */
extern struct file_operations ll_file_operations;
extern struct file_operations ll_file_operations_flock;
extern struct file_operations ll_file_operations_noflock;
extern struct inode_operations ll_file_inode_operations;
extern int ll_have_md_lock(struct inode *inode, __u64 *bits,
			   enum ldlm_mode l_req_mode);
extern enum ldlm_mode ll_take_md_lock(struct inode *inode, __u64 bits,
				      struct lustre_handle *lockh, __u64 flags,
				      enum ldlm_mode mode);

int ll_file_open(struct inode *inode, struct file *file);
int ll_file_release(struct inode *inode, struct file *file);
int ll_release_openhandle(struct dentry *, struct lookup_intent *);
int ll_md_real_close(struct inode *inode, fmode_t fmode);
extern void ll_rw_stats_tally(struct ll_sb_info *sbi, pid_t pid,
                              struct ll_file_data *file, loff_t pos,
                              size_t count, int rw);
#ifdef HAVE_INODEOPS_ENHANCED_GETATTR
int ll_getattr(const struct path *path, struct kstat *stat,
	       u32 request_mask, unsigned int flags);
#else
int ll_getattr(struct vfsmount *mnt, struct dentry *de, struct kstat *stat);
#endif
int ll_getattr_dentry(struct dentry *de, struct kstat *stat);
struct posix_acl *ll_get_acl(struct inode *inode, int type);
#ifdef HAVE_IOP_SET_ACL
#ifdef CONFIG_FS_POSIX_ACL
int ll_set_acl(struct inode *inode, struct posix_acl *acl, int type);
#else  /* !CONFIG_FS_POSIX_ACL */
#define ll_set_acl NULL
#endif /* CONFIG_FS_POSIX_ACL */

#endif
int ll_migrate(struct inode *parent, struct file *file,
	       struct lmv_user_md *lum, const char *name);
int ll_get_fid_by_name(struct inode *parent, const char *name,
		       int namelen, struct lu_fid *fid, struct inode **inode);
int ll_inode_permission(struct inode *inode, int mask);
int ll_ioctl_check_project(struct inode *inode, struct fsxattr *fa);
int ll_ioctl_fsgetxattr(struct inode *inode, unsigned int cmd,
			unsigned long arg);
int ll_ioctl_fssetxattr(struct inode *inode, unsigned int cmd,
			unsigned long arg);

int ll_lov_setstripe_ea_info(struct inode *inode, struct dentry *dentry,
			     __u64 flags, struct lov_user_md *lum,
			     int lum_size);
int ll_lov_getstripe_ea_info(struct inode *inode, const char *filename,
                             struct lov_mds_md **lmm, int *lmm_size,
                             struct ptlrpc_request **request);
int ll_dir_setstripe(struct inode *inode, struct lov_user_md *lump,
                     int set_default);
int ll_dir_getstripe(struct inode *inode, void **lmmp,
		     int *lmm_size, struct ptlrpc_request **request,
		     u64 valid);
int ll_fsync(struct file *file, loff_t start, loff_t end, int data);
int ll_merge_attr(const struct lu_env *env, struct inode *inode);
int ll_fid2path(struct inode *inode, void __user *arg);
int ll_data_version(struct inode *inode, __u64 *data_version, int flags);
int ll_hsm_release(struct inode *inode);
int ll_hsm_state_set(struct inode *inode, struct hsm_state_set *hss);
void ll_io_set_mirror(struct cl_io *io, const struct file *file);

/* llite/dcache.c */

int ll_d_init(struct dentry *de);
extern const struct dentry_operations ll_d_ops;
void ll_intent_drop_lock(struct lookup_intent *);
void ll_intent_release(struct lookup_intent *);
void ll_invalidate_aliases(struct inode *);
void ll_lookup_finish_locks(struct lookup_intent *it, struct dentry *dentry);
int ll_revalidate_it_finish(struct ptlrpc_request *request,
                            struct lookup_intent *it, struct dentry *de);

/* llite/llite_lib.c */
extern struct super_operations lustre_super_operations;

void ll_lli_init(struct ll_inode_info *lli);
int ll_fill_super(struct super_block *sb, struct vfsmount *mnt);
void ll_put_super(struct super_block *sb);
void ll_kill_super(struct super_block *sb);
struct inode *ll_inode_from_resource_lock(struct ldlm_lock *lock);
void ll_dir_clear_lsm_md(struct inode *inode);
void ll_clear_inode(struct inode *inode);
int ll_setattr_raw(struct dentry *dentry, struct iattr *attr,
		   enum op_xvalid xvalid, bool hsm_import);
int ll_setattr(struct dentry *de, struct iattr *attr);
int ll_statfs(struct dentry *de, struct kstatfs *sfs);
int ll_statfs_internal(struct ll_sb_info *sbi, struct obd_statfs *osfs,
		       u32 flags);
int ll_update_inode(struct inode *inode, struct lustre_md *md);
void ll_update_inode_flags(struct inode *inode, int ext_flags);
int ll_read_inode2(struct inode *inode, void *opaque);
void ll_delete_inode(struct inode *inode);
int ll_iocontrol(struct inode *inode, struct file *file,
                 unsigned int cmd, unsigned long arg);
int ll_flush_ctx(struct inode *inode);
void ll_umount_begin(struct super_block *sb);
int ll_remount_fs(struct super_block *sb, int *flags, char *data);
#ifdef HAVE_SUPEROPS_USE_DENTRY
int ll_show_options(struct seq_file *seq, struct dentry *dentry);
#else
int ll_show_options(struct seq_file *seq, struct vfsmount *vfs);
#endif
void ll_dirty_page_discard_warn(struct page *page, int ioret);
int ll_prep_inode(struct inode **inode, struct ptlrpc_request *req,
		  struct super_block *, struct lookup_intent *);
int ll_obd_statfs(struct inode *inode, void __user *arg);
int ll_get_max_mdsize(struct ll_sb_info *sbi, int *max_mdsize);
int ll_get_default_mdsize(struct ll_sb_info *sbi, int *default_mdsize);
int ll_set_default_mdsize(struct ll_sb_info *sbi, int default_mdsize);

void ll_unlock_md_op_lsm(struct md_op_data *op_data);
struct md_op_data *ll_prep_md_op_data(struct md_op_data *op_data,
				      struct inode *i1, struct inode *i2,
				      const char *name, size_t namelen,
				      __u32 mode, enum md_op_code opc,
				      void *data);
void ll_finish_md_op_data(struct md_op_data *op_data);
int ll_get_obd_name(struct inode *inode, unsigned int cmd, unsigned long arg);
void ll_compute_rootsquash_state(struct ll_sb_info *sbi);
ssize_t ll_copy_user_md(const struct lov_user_md __user *md,
			struct lov_user_md **kbuf);
void ll_open_cleanup(struct super_block *sb, struct ptlrpc_request *open_req);

void ll_dom_finish_open(struct inode *inode, struct ptlrpc_request *req,
			struct lookup_intent *it);

/* Compute expected user md size when passing in a md from user space */
static inline ssize_t ll_lov_user_md_size(const struct lov_user_md *lum)
{
	switch (lum->lmm_magic) {
	case LOV_USER_MAGIC_V1:
		return sizeof(struct lov_user_md_v1);
	case LOV_USER_MAGIC_V3:
		return sizeof(struct lov_user_md_v3);
	case LOV_USER_MAGIC_SPECIFIC:
		if (lum->lmm_stripe_count > LOV_MAX_STRIPE_COUNT)
			return -EINVAL;

		return lov_user_md_size(lum->lmm_stripe_count,
					LOV_USER_MAGIC_SPECIFIC);
	case LOV_USER_MAGIC_COMP_V1:
		return ((struct lov_comp_md_v1 *)lum)->lcm_size;
	case LOV_USER_MAGIC_FOREIGN:
		return foreign_size(lum);
	}

	return -EINVAL;
}

/* llite/llite_nfs.c */
extern struct export_operations lustre_export_operations;
__u32 get_uuid2int(const char *name, int len);
struct inode *search_inode_for_lustre(struct super_block *sb,
				      const struct lu_fid *fid);
int ll_dir_get_parent_fid(struct inode *dir, struct lu_fid *parent_fid);

/* llite/symlink.c */
extern struct inode_operations ll_fast_symlink_inode_operations;

/**
 * IO arguments for various VFS I/O interfaces.
 */
struct vvp_io_args {
        /** normal/sendfile/splice */
        enum vvp_io_subtype via_io_subtype;

        union {
                struct {
                        struct kiocb      *via_iocb;
			struct iov_iter   *via_iter;
                } normal;
                struct {
                        struct pipe_inode_info  *via_pipe;
                        unsigned int       via_flags;
                } splice;
        } u;
};

enum lcc_type {
	LCC_RW = 1,
	LCC_MMAP
};

struct ll_cl_context {
	struct list_head	 lcc_list;
	void			*lcc_cookie;
	const struct lu_env	*lcc_env;
	struct cl_io		*lcc_io;
	struct cl_page		*lcc_page;
	enum lcc_type		 lcc_type;
};

struct ll_thread_info {
	struct iov_iter		lti_iter;
	struct vvp_io_args	lti_args;
	struct ra_io_arg	lti_ria;
	struct ll_cl_context	lti_io_ctx;
};

extern struct lu_context_key ll_thread_key;

static inline struct ll_thread_info *ll_env_info(const struct lu_env *env)
{
	struct ll_thread_info *lti;

	lti = lu_context_key_get(&env->le_ctx, &ll_thread_key);
	LASSERT(lti != NULL);

	return lti;
}

static inline struct vvp_io_args *ll_env_args(const struct lu_env *env,
					      enum vvp_io_subtype type)
{
	struct vvp_io_args *via = &ll_env_info(env)->lti_args;

	via->via_io_subtype = type;

	return via;
}

void ll_io_init(struct cl_io *io, struct file *file, enum cl_io_type iot,
		struct vvp_io_args *args);

/* llite/llite_mmap.c */

int ll_teardown_mmaps(struct address_space *mapping, __u64 first, __u64 last);
int ll_file_mmap(struct file * file, struct vm_area_struct * vma);
void policy_from_vma(union ldlm_policy_data *policy, struct vm_area_struct *vma,
		     unsigned long addr, size_t count);
struct vm_area_struct *our_vma(struct mm_struct *mm, unsigned long addr,
                               size_t count);

#define    ll_s2sbi(sb)        (s2lsi(sb)->lsi_llsbi)

/* don't need an addref as the sb_info should be holding one */
static inline struct obd_export *ll_s2dtexp(struct super_block *sb)
{
        return ll_s2sbi(sb)->ll_dt_exp;
}

/* don't need an addref as the sb_info should be holding one */
static inline struct obd_export *ll_s2mdexp(struct super_block *sb)
{
        return ll_s2sbi(sb)->ll_md_exp;
}

static inline struct client_obd *sbi2mdc(struct ll_sb_info *sbi)
{
        struct obd_device *obd = sbi->ll_md_exp->exp_obd;
        if (obd == NULL)
                LBUG();
        return &obd->u.cli;
}

// FIXME: replace the name of this with LL_SB to conform to kernel stuff
static inline struct ll_sb_info *ll_i2sbi(struct inode *inode)
{
        return ll_s2sbi(inode->i_sb);
}

static inline struct obd_export *ll_i2dtexp(struct inode *inode)
{
        return ll_s2dtexp(inode->i_sb);
}

static inline struct obd_export *ll_i2mdexp(struct inode *inode)
{
        return ll_s2mdexp(inode->i_sb);
}

static inline struct lu_fid *ll_inode2fid(struct inode *inode)
{
        struct lu_fid *fid;

        LASSERT(inode != NULL);
        fid = &ll_i2info(inode)->lli_fid;

        return fid;
}

static inline bool ll_dir_striped(struct inode *inode)
{
	LASSERT(inode);
	return S_ISDIR(inode->i_mode) &&
	       lmv_dir_striped(ll_i2info(inode)->lli_lsm_md);
}

static inline loff_t ll_file_maxbytes(struct inode *inode)
{
	struct cl_object *obj = ll_i2info(inode)->lli_clob;

	if (obj == NULL)
		return MAX_LFS_FILESIZE;

	return min_t(loff_t, cl_object_maxbytes(obj), MAX_LFS_FILESIZE);
}

/* llite/xattr.c */
extern const struct xattr_handler *ll_xattr_handlers[];

#define XATTR_USER_T		1
#define XATTR_TRUSTED_T		2
#define XATTR_SECURITY_T	3
#define XATTR_ACL_ACCESS_T	4
#define XATTR_ACL_DEFAULT_T	5
#define XATTR_LUSTRE_T		6
#define XATTR_OTHER_T		7

ssize_t ll_listxattr(struct dentry *dentry, char *buffer, size_t size);
int ll_xattr_list(struct inode *inode, const char *name, int type,
		  void *buffer, size_t size, u64 valid);
const struct xattr_handler *get_xattr_type(const char *name);

/**
 * Common IO arguments for various VFS I/O interfaces.
 */
int cl_sb_init(struct super_block *sb);
int cl_sb_fini(struct super_block *sb);

enum ras_update_flags {
	LL_RAS_HIT  = 0x1,
	LL_RAS_MMAP = 0x2
};
void ll_ra_count_put(struct ll_sb_info *sbi, unsigned long len);
void ll_ra_stats_inc(struct inode *inode, enum ra_stat which);

/* statahead.c */

#define LL_SA_RPC_MIN           2
#define LL_SA_RPC_DEF           32
#define LL_SA_RPC_MAX           512

/* XXX: If want to support more concurrent statahead instances,
 *	please consider to decentralize the RPC lists attached
 *	on related import, such as imp_{sending,delayed}_list.
 *	LU-11079 */
#define LL_SA_RUNNING_MAX	256
#define LL_SA_RUNNING_DEF	16

#define LL_SA_CACHE_BIT         5
#define LL_SA_CACHE_SIZE        (1 << LL_SA_CACHE_BIT)
#define LL_SA_CACHE_MASK        (LL_SA_CACHE_SIZE - 1)

/* per inode struct, for dir only */
struct ll_statahead_info {
	struct dentry	       *sai_dentry;
	atomic_t		sai_refcount;   /* when access this struct, hold
						 * refcount */
	unsigned int            sai_max;        /* max ahead of lookup */
	__u64                   sai_sent;       /* stat requests sent count */
	__u64                   sai_replied;    /* stat requests which received
						 * reply */
	__u64                   sai_index;      /* index of statahead entry */
	__u64                   sai_index_wait; /* index of entry which is the
						 * caller is waiting for */
	__u64                   sai_hit;        /* hit count */
	__u64                   sai_miss;       /* miss count:
						 * for "ls -al" case, includes
						 * hidden dentry miss;
						 * for "ls -l" case, it does not
						 * include hidden dentry miss.
						 * "sai_miss_hidden" is used for
						 * the later case.
						 */
	unsigned int            sai_consecutive_miss; /* consecutive miss */
	unsigned int            sai_miss_hidden;/* "ls -al", but first dentry
						 * is not a hidden one */
	unsigned int            sai_skip_hidden;/* skipped hidden dentry count
						 */
	unsigned int            sai_ls_all:1,   /* "ls -al", do stat-ahead for
						 * hidden entries */
				sai_agl_valid:1,/* AGL is valid for the dir */
				sai_in_readpage:1;/* statahead is in readdir()*/
	wait_queue_head_t	sai_waitq;	/* stat-ahead wait queue */
	struct ptlrpc_thread	sai_thread;	/* stat-ahead thread */
	struct ptlrpc_thread	sai_agl_thread;	/* AGL thread */
	struct list_head	sai_interim_entries; /* entries which got async
						      * stat reply, but not
						      * instantiated */
	struct list_head	sai_entries;    /* completed entries */
	struct list_head	sai_agls;	/* AGLs to be sent */
	struct list_head	sai_cache[LL_SA_CACHE_SIZE];
	spinlock_t		sai_cache_lock[LL_SA_CACHE_SIZE];
	atomic_t		sai_cache_count; /* entry count in cache */
};

int ll_statahead(struct inode *dir, struct dentry **dentry, bool unplug);
void ll_authorize_statahead(struct inode *dir, void *key);
void ll_deauthorize_statahead(struct inode *dir, void *key);

/* glimpse.c */
blkcnt_t dirty_cnt(struct inode *inode);

int cl_glimpse_size0(struct inode *inode, int agl);
int cl_glimpse_lock(const struct lu_env *env, struct cl_io *io,
		    struct inode *inode, struct cl_object *clob, int agl);

static inline int cl_glimpse_size(struct inode *inode)
{
	return cl_glimpse_size0(inode, 0);
}

/* AGL is 'asychronous glimpse lock', which is a speculative lock taken as
 * part of statahead */
static inline int cl_agl(struct inode *inode)
{
	return cl_glimpse_size0(inode, 1);
}

int ll_file_lock_ahead(struct file *file, struct llapi_lu_ladvise *ladvise);

int cl_io_get(struct inode *inode, struct lu_env **envout,
	      struct cl_io **ioout, __u16 *refcheck);

static inline int ll_glimpse_size(struct inode *inode)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	int rc;

	down_read(&lli->lli_glimpse_sem);
	rc = cl_glimpse_size(inode);
	lli->lli_glimpse_time = ktime_get();
	up_read(&lli->lli_glimpse_sem);
	return rc;
}

/* dentry may statahead when statahead is enabled and current process has opened
 * parent directory, and this dentry hasn't accessed statahead cache before */
static inline bool
dentry_may_statahead(struct inode *dir, struct dentry *dentry)
{
	struct ll_inode_info  *lli;
	struct ll_dentry_data *ldd;

	if (ll_i2sbi(dir)->ll_sa_max == 0)
		return false;

	lli = ll_i2info(dir);

	/* statahead is not allowed for this dir, there may be three causes:
	 * 1. dir is not opened.
	 * 2. statahead hit ratio is too low.
	 * 3. previous stat started statahead thread failed. */
	if (!lli->lli_sa_enabled)
		return false;

	/* not the same process, don't statahead */
	if (lli->lli_opendir_pid != current_pid())
		return false;

	/*
	 * When stating a dentry, kernel may trigger 'revalidate' or 'lookup'
	 * multiple times, eg. for 'getattr', 'getxattr' and etc.
	 * For patchless client, lookup intent is not accurate, which may
	 * misguide statahead. For example:
	 * The 'revalidate' call for 'getattr' and 'getxattr' of a dentry will
	 * have the same intent -- IT_GETATTR, while one dentry should access
	 * statahead cache once, otherwise statahead windows is messed up.
	 * The solution is as following:
	 * Assign 'lld_sa_generation' with 'lli_sa_generation' when a dentry
	 * IT_GETATTR for the first time, and subsequent IT_GETATTR will
	 * bypass interacting with statahead cache by checking
	 * 'lld_sa_generation == lli->lli_sa_generation'.
	 */
	ldd = ll_d2d(dentry);
	if (ldd != NULL && ldd->lld_sa_generation == lli->lli_sa_generation)
		return false;

	return true;
}

int cl_sync_file_range(struct inode *inode, loff_t start, loff_t end,
		       enum cl_fsync_mode mode, int ignore_layout);

static inline int ll_file_nolock(const struct file *file)
{
        struct ll_file_data *fd = LUSTRE_FPRIVATE(file);
	struct inode *inode = file_inode((struct file *)file);

        LASSERT(fd != NULL);
        return ((fd->fd_flags & LL_FILE_IGNORE_LOCK) ||
                (ll_i2sbi(inode)->ll_flags & LL_SBI_NOLCK));
}

static inline void ll_set_lock_data(struct obd_export *exp, struct inode *inode,
                                    struct lookup_intent *it, __u64 *bits)
{
	if (!it->it_lock_set) {
		struct lustre_handle handle;

		/* If this inode is a remote object, it will get two
		 * separate locks in different namespaces, Master MDT,
		 * where the name entry is, will grant LOOKUP lock,
		 * remote MDT, where the object is, will grant
		 * UPDATE|PERM lock. The inode will be attched to both
		 * LOOKUP and PERM locks, so revoking either locks will
		 * case the dcache being cleared */
		if (it->it_remote_lock_mode) {
			handle.cookie = it->it_remote_lock_handle;
			CDEBUG(D_DLMTRACE, "setting l_data to inode "DFID
			       "(%p) for remote lock %#llx\n",
			       PFID(ll_inode2fid(inode)), inode,
			       handle.cookie);
			md_set_lock_data(exp, &handle, inode, NULL);
		}

		handle.cookie = it->it_lock_handle;

		CDEBUG(D_DLMTRACE, "setting l_data to inode "DFID"(%p)"
		       " for lock %#llx\n",
		       PFID(ll_inode2fid(inode)), inode, handle.cookie);

		md_set_lock_data(exp, &handle, inode, &it->it_lock_bits);
		it->it_lock_set = 1;
	}

	if (bits != NULL)
		*bits = it->it_lock_bits;
}

static inline int d_lustre_invalid(const struct dentry *dentry)
{
	struct ll_dentry_data *lld = ll_d2d(dentry);

	return (lld == NULL) || lld->lld_invalid;
}

static inline void __d_lustre_invalidate(struct dentry *dentry)
{
	struct ll_dentry_data *lld = ll_d2d(dentry);

	if (lld != NULL)
		lld->lld_invalid = 1;
}

/*
 * Mark dentry INVALID, if dentry refcount is zero (this is normally case for
 * ll_md_blocking_ast), unhash this dentry, and let dcache to reclaim it later;
 * else dput() of the last refcount will unhash this dentry and kill it.
 */
static inline void d_lustre_invalidate(struct dentry *dentry, int nested)
{
	CDEBUG(D_DENTRY, "invalidate dentry %.*s (%p) parent %p inode %p "
	       "refc %d\n", dentry->d_name.len, dentry->d_name.name, dentry,
	       dentry->d_parent, dentry->d_inode, ll_d_count(dentry));

	spin_lock_nested(&dentry->d_lock,
			 nested ? DENTRY_D_LOCK_NESTED : DENTRY_D_LOCK_NORMAL);
	__d_lustre_invalidate(dentry);
	/*
	 * We should be careful about dentries created by d_obtain_alias().
	 * These dentries are not put in the dentry tree, instead they are
	 * linked to sb->s_anon through dentry->d_hash.
	 * shrink_dcache_for_umount() shrinks the tree and sb->s_anon list.
	 * If we unhashed such a dentry, unmount would not be able to find
	 * it and busy inodes would be reported.
	 */
	if (ll_d_count(dentry) == 0 && !(dentry->d_flags & DCACHE_DISCONNECTED))
		__d_drop(dentry);
	spin_unlock(&dentry->d_lock);
}

static inline void d_lustre_revalidate(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	LASSERT(ll_d2d(dentry) != NULL);
	ll_d2d(dentry)->lld_invalid = 0;
	spin_unlock(&dentry->d_lock);
}

static inline dev_t ll_compat_encode_dev(dev_t dev)
{
	/* The compat_sys_*stat*() syscalls will fail unless the
	 * device majors and minors are both less than 256. Note that
	 * the value returned here will be passed through
	 * old_encode_dev() in cp_compat_stat(). And so we are not
	 * trying to return a valid compat (u16) device number, just
	 * one that will pass the old_valid_dev() check. */

	return MKDEV(MAJOR(dev) & 0xff, MINOR(dev) & 0xff);
}

int ll_layout_conf(struct inode *inode, const struct cl_object_conf *conf);
int ll_layout_refresh(struct inode *inode, __u32 *gen);
int ll_layout_restore(struct inode *inode, loff_t start, __u64 length);
int ll_layout_write_intent(struct inode *inode, enum layout_intent_opc opc,
			   struct lu_extent *ext);

int ll_xattr_init(void);
void ll_xattr_fini(void);

int ll_page_sync_io(const struct lu_env *env, struct cl_io *io,
		    struct cl_page *page, enum cl_req_type crt);

int ll_getparent(struct file *file, struct getparent __user *arg);

/* lcommon_cl.c */
int cl_setattr_ost(struct cl_object *obj, const struct iattr *attr,
		   enum op_xvalid xvalid, unsigned int attr_flags);

extern struct lu_env *cl_inode_fini_env;
extern __u16 cl_inode_fini_refcheck;

int cl_file_inode_init(struct inode *inode, struct lustre_md *md);
void cl_inode_fini(struct inode *inode);

u64 cl_fid_build_ino(const struct lu_fid *fid, int api32);
u32 cl_fid_build_gen(const struct lu_fid *fid);

#endif /* LLITE_INTERNAL_H */
