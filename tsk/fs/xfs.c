/*
** The Sleuth Kit
**
** Brian Carrier [carrier <at> sleuthkit [dot] org]
** Copyright (c) 2003-2011 Brian Carrier.  All rights reserved
**
** ICS Laboratory [515lab.ics <at> gmail [dot] com]
** Copyright (c) 2019 ICS Laboratory.  All rights reserved.
**
** This software is distributed under the Common Public License 1.0
*/

#include "tsk_fs_i.h"
#include "tsk_xfs.h"

static int
xfs_mount_validate_sb(
    TSK_FS_INFO* fs, xfs_sb *sbp)
{
    if (tsk_fs_guessu64(fs, sbp->sb_magicnum, XFS_FS_MAGIC)) {
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_MAGIC);
        tsk_error_set_errstr("xfs: Invalid magic number");
        return 0;
    }

    if (!xfs_sb_good_version(fs, sbp)) {
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_UNSUPTYPE);
        tsk_error_set_errstr("xfs: unsupported superblock version or corrupted superblock");
        return 0;
    }

    /*
     * Version 5 superblock feature mask validation. Reject combinations the
     * kernel cannot support up front before checking anything else. For
     * write validation, we don't need to check feature masks.
     */
    if (tsk_fs_guessu64(fs, sbp->sb_versionnum, XFS_SB_VERSION_5)) {
    
        if (xfs_sb_has_compat_feature(fs, sbp, XFS_SB_FEAT_COMPAT_UNKNOWN)) {
            tsk_fprintf(stderr,
                "Superblock has unknown compatible features (0x%x) enabled.",
                (tsk_getu32(fs->endian, sbp->sb_features_compat) & XFS_SB_FEAT_COMPAT_UNKNOWN));
            tsk_fprintf(stderr, "Using a more recent kernel is recommended.");
        }

        if (xfs_sb_has_ro_compat_feature(fs, sbp, XFS_SB_FEAT_RO_COMPAT_UNKNOWN)) {
            tsk_fprintf(stderr, 
                "Superblock has unknown read-only compatible features (0x%x) enabled.",
                (tsk_getu32(fs->endian, sbp->sb_features_ro_compat) & XFS_SB_FEAT_RO_COMPAT_UNKNOWN));
        }

        if (xfs_sb_has_incompat_feature(fs, sbp, XFS_SB_FEAT_INCOMPAT_UNKNOWN)) {
            tsk_fprintf(stderr, 
                "Superblock has unknown incompatible features (0x%x) enabled.",
                (tsk_getu32(fs->endian, sbp->sb_features_incompat) & XFS_SB_FEAT_INCOMPAT_UNKNOWN));
            tsk_fprintf(stderr, 
                "Filesystem can not be safely mounted by tsk.");
        }
    }

    /*
     * More sanity checking.  Most of these were stolen directly from
     * xfs_repair.
     */
    if ((tsk_getu32(fs->endian, sbp->sb_agcount) <= 0                    ||
        tsk_getu16(fs->endian, sbp->sb_sectsize) < XFS_MIN_SECTORSIZE           ||
        tsk_getu16(fs->endian, sbp->sb_sectsize) > XFS_MAX_SECTORSIZE           ||
        tsk_getu8(fs->endian, sbp->sb_sectlog) < XFS_MIN_SECTORSIZE_LOG            ||
        tsk_getu8(fs->endian, sbp->sb_sectlog) > XFS_MAX_SECTORSIZE_LOG            ||
        tsk_getu16(fs->endian, sbp->sb_sectsize) != (1 << tsk_getu8(fs->endian, sbp->sb_sectlog))          ||
        tsk_getu32(fs->endian, sbp->sb_blocksize) < XFS_MIN_BLOCKSIZE           ||
        tsk_getu32(fs->endian, sbp->sb_blocksize) > XFS_MAX_BLOCKSIZE           ||
        tsk_getu8(fs->endian, sbp->sb_blocklog) < XFS_MIN_BLOCKSIZE_LOG            ||
        tsk_getu8(fs->endian, sbp->sb_blocklog) > XFS_MAX_BLOCKSIZE_LOG            ||
        tsk_getu32(fs->endian, sbp->sb_blocksize) != ((uint32_t)1 << tsk_getu8(fs->endian, sbp->sb_blocklog))        ||
        tsk_getu8(fs->endian, sbp->sb_dirblklog) + tsk_getu8(fs->endian, sbp->sb_blocklog) > XFS_MAX_BLOCKSIZE_LOG ||
        tsk_getu16(fs->endian, sbp->sb_inodesize) < XFS_DINODE_MIN_SIZE         ||
        tsk_getu16(fs->endian, sbp->sb_inodesize) > XFS_DINODE_MAX_SIZE         ||
        tsk_getu8(fs->endian, sbp->sb_inodelog) < XFS_DINODE_MIN_LOG           ||
        tsk_getu8(fs->endian, sbp->sb_inodelog) > XFS_DINODE_MAX_LOG           ||
        tsk_getu16(fs->endian, sbp->sb_inodesize) != (1 << tsk_getu8(fs->endian, sbp->sb_inodelog))        ||
        tsk_getu32(fs->endian, sbp->sb_logsunit) > XLOG_MAX_RECORD_BSIZE            ||
        tsk_getu16(fs->endian, sbp->sb_inopblock) != howmany(tsk_getu16(fs->endian, sbp->sb_blocksize), tsk_getu16(fs->endian, sbp->sb_inodesize)) ||
        (tsk_getu8(fs->endian, sbp->sb_blocklog) - tsk_getu8(fs->endian, sbp->sb_inodelog) != tsk_getu8(fs->endian, sbp->sb_inopblog))   ||
        (tsk_getu32(fs->endian, sbp->sb_rextsize) * tsk_getu32(fs->endian, sbp->sb_blocksize) > XFS_MAX_RTEXTSIZE)  ||
        (tsk_getu32(fs->endian, sbp->sb_rextsize) * tsk_getu32(fs->endian, sbp->sb_blocksize) < XFS_MIN_RTEXTSIZE)  ||
        (tsk_getu8(fs->endian, sbp->sb_imax_pct) > 100 /* zero sb_imax_pct is valid */)    ||
        tsk_getu64(fs->endian, sbp->sb_dblocks) == 0                    ||
        tsk_getu64(fs->endian, sbp->sb_dblocks) > XFS_MAX_DBLOCKS(fs, sbp)          ||
        tsk_getu64(fs->endian, sbp->sb_dblocks) < XFS_MIN_DBLOCKS(fs, sbp)          ||
        tsk_getu8(fs->endian, sbp->sb_shared_vn) != 0)) {
        tsk_fprintf(stderr, "Superblock sanity check failed");
        return NULL;
    }

    if (tsk_fs_guessu16(fs, sbp->sb_versionnum, XFS_SB_VERSION_5) &&
        tsk_getu32(fs->endian, sbp->sb_blocksize) < XFS_MIN_CRC_BLOCKSIZE) {
        tsk_fprintf(stderr, "v5 Superblock sanity check failed");
        return NULL;
    }

    /*
     * Currently only very few inode sizes are supported.
     */
    switch (tsk_getu16(fs->endian, sbp->sb_inodesize)) {
    case 256:
    case 512:
    case 1024:
    case 2048:
        break;
    default:
        tsk_fprintf(stderr, "inode size of %d bytes not supported",
                tsk_getu16(fs->endian, sbp->sb_inodesize));
        return -ENOSYS;
    }

    return 0;
}

void
xfs_bmbt_disk_get_all(
    XFS_INFO* xfs,
    struct xfs_bmbt_rec *rec,
    struct xfs_bmbt_irec    *irec)
{
    uint64_t        l0 = tsk_getu64(xfs->fs_info.endian, rec->l0);
    uint64_t        l1 = tsk_getu64(xfs->fs_info.endian, rec->l1);

    irec->br_startoff = (l0 & xfs_mask64lo(64 - BMBT_EXNTFLAG_BITLEN)) >> 9;
    irec->br_startblock = ((l0 & xfs_mask64lo(9)) << 43) | (l1 >> 21);
    irec->br_blockcount = l1 & xfs_mask64lo(21);

    if (l0 >> (64 - BMBT_EXNTFLAG_BITLEN))
        irec->br_state = XFS_EXT_UNWRITTEN;
    else
        irec->br_state = XFS_EXT_NORM;
}

/** \internal
 * Add a single extent -- that is, a single data ran -- to the file data attribute.
 * @return 0 on success, 1 on error.
 */
static TSK_OFF_T
xfs_make_data_run_extent(TSK_FS_INFO * fs_info, TSK_FS_ATTR * fs_attr,
    xfs_bmbt_rec_t* extent)
{
    TSK_FS_ATTR_RUN *data_run;
    XFS_INFO * xfs = (XFS_INFO *)fs_info;

    if ((data_run = tsk_fs_attr_run_alloc()) == NULL)
        return 1;
    
    xfs_bmbt_irec_t *irec = (xfs_bmbt_irec_t*)tsk_malloc(sizeof(xfs_bmbt_irec_t));
    xfs_bmbt_disk_get_all(xfs, extent, irec);
    
    uint32_t agno =  XFS_FSB_TO_AGNO(xfs, irec->br_startblock);
    uint32_t blkno = XFS_FSB_TO_AGBNO(xfs, irec->br_startblock);

    data_run->offset = 0;
    data_run->addr = agno * (TSK_DADDR_T)tsk_getu32(fs_info->endian, xfs->fs->sb_agblocks) + blkno;

    data_run->len = irec->br_blockcount;

    if (tsk_fs_attr_add_run(fs_info, fs_attr, data_run)) {
        return 1;
    }

    return 0;
}


/** \internal
 * Given a block that contains an extent node (which starts with extent_header),
 * walk it, and add everything encountered to the appropriate attributes.
 * @return 0 on success, 1 on error.
 */
static TSK_OFF_T
xfs_make_data_run_extent_index(TSK_FS_INFO * fs_info,
    TSK_FS_ATTR * fs_attr, TSK_FS_ATTR * fs_attr_extent,
    TSK_DADDR_T idx_block)
{
    fprintf(stderr, "[i] xfs_make_data_run_extent_index: xfs.c: %d - not implemented\n", __LINE__);

    return 1;
}

/**
 * \internal
 * Loads attribute for XFS Extents-based storage method.
 * @param fs_file File system to analyze
 * @returns 0 on success, 1 otherwise
 */
static uint8_t
xfs_load_attrs_block(TSK_FS_FILE *fs_file)
{
    TSK_FS_META *fs_meta = fs_file->meta;
    TSK_FS_INFO *fs_info = fs_file->fs_info;
    XFS_INFO * xfs = (XFS_INFO*)fs_info;
    TSK_OFF_T length = 0;
    TSK_FS_ATTR * fs_attr;
    int i;
    xfs_bmbt_rec_t *rec;

    rec = (xfs_bmbt_rec_t*)fs_meta->content_ptr;    
    
    if ((fs_meta->attr != NULL)
        && (fs_meta->attr_state == TSK_FS_META_ATTR_STUDIED)) {
        fprintf(stderr, "[i] xfs_load_attr_block: xfs.c: %d - already studied, exiting load_attr_blk\n", __LINE__);
        return 0;
    }else if (fs_meta->attr_state == TSK_FS_META_ATTR_ERROR) {
        fprintf(stderr, "[i] xfs_load_attr_block: xfs.c: %d - error on attr, exiting load_attr_blk\n", __LINE__);
        return 1;
    }

    if (fs_meta->attr != NULL) {
        tsk_fs_attrlist_markunused(fs_meta->attr);
    }
    else {
        fs_meta->attr = tsk_fs_attrlist_alloc();
    }

    if (TSK_FS_TYPE_ISXFS(fs_info->ftype) == 0) {
        tsk_error_set_errno(TSK_ERR_FS_INODE_COR);
        tsk_error_set_errstr
        ("xfs_load_attr: Called with non-xfs file system: %x",
         fs_info->ftype);
        return 1;
    }

    length = roundup(fs_meta->size, fs_info->block_size);
    
    if ((fs_attr =
         tsk_fs_attrlist_getnew(fs_meta->attr,
                                TSK_FS_ATTR_NONRES)) == NULL) {
        return 1;
    }

    if (tsk_fs_attr_set_run(fs_file, fs_attr, NULL, NULL,
                            TSK_FS_ATTR_TYPE_DEFAULT, TSK_FS_ATTR_ID_DEFAULT,
                            fs_meta->size, fs_meta->size, length, 0, 0)) {
        return 1;
    }

    while (true)
    {
        if (tsk_getu64(fs_info->endian, rec->l0) == 0 && tsk_getu64(fs_info->endian, rec->l1) == 0)
            break;
           
        if (xfs_make_data_run_extent(fs_info, fs_attr, rec)) {
            fprintf(stderr, "[i] xfs_load_attr_block: xfs.c: %d - xfs_make_data_run_extent failed.\n",
                __LINE__);
            return 1;
        }

        rec = xfs_dir2_data_nextentry(rec);
    }
    
    fs_meta->attr_state = TSK_FS_META_ATTR_STUDIED;
    
    return 0;
}

/** \internal
 * Add the data runs and extents to the file attributes.
 *
 * @param fs_file File system to analyze
 * @returns 0 on success, 1 otherwise
 */
static uint8_t
xfs_load_attrs(TSK_FS_FILE * fs_file)
{
    TSK_FS_INFO * fs = (TSK_FS_INFO*)fs_file->fs_info;

    // not needed to implement about shortform data fork. shortform does not have location of real file.
    if (fs_file->meta->content_type == TSK_FS_META_CONTENT_TYPE_XFS_DATA_FORK_EXTENTS) {
        xfs_load_attrs_block(fs_file);
    }   
    else if (fs_file->meta->content_type == TSK_FS_META_CONTENT_TYPE_XFS_DATA_FORK_BTREE) {
        return 1        ;
    }
    else {
        fprintf(stderr, "contenttype = unknown content type\n");
        return 1;
    }

    return 0;
}

    
static uint8_t
xfs_dinode_load(XFS_INFO * xfs, TSK_INUM_T dino_inum,
    xfs_dinode * dino_buf)
{
    TSK_OFF_T addr;
    ssize_t cnt;
    TSK_INUM_T rel_inum;
    TSK_FS_INFO *fs = (TSK_FS_INFO *) & xfs->fs_info;
    /*
     * Sanity check.
     * Use last_num-1 to account for virtual Orphan directory in last_inum.
     */
    if ((dino_inum < fs->first_inum) || (dino_inum > fs->last_inum - 1)) {
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_INODE_NUM);
        tsk_error_set_errstr("xfs_dinode_load: address: %" PRIuINUM,
            dino_inum);
        return 1;
    }

    if (dino_buf == NULL) {
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_ARG);
        tsk_error_set_errstr("xfs_dinode_load: dino_buf is NULL");
        return 1;
    }

    addr = xfs_inode_get_offset(xfs, dino_inum);

    cnt = tsk_fs_read(fs, addr, (char *)dino_buf, xfs->inode_size);
    
    if (cnt != xfs->inode_size) {
        if (cnt >= 0) {
            tsk_error_reset();
            tsk_error_set_errno(TSK_ERR_FS_READ);
        }

        tsk_error_set_errstr2("xfs_dinode_load: Inode %" PRIuINUM
            " from %" PRIuOFF, dino_inum, addr);

        return 1;
    }

    return 0;
}

static uint8_t
xfs_dinode_copy(XFS_INFO * xfs, TSK_FS_META * fs_meta,
    TSK_INUM_T inum, const xfs_dinode * dino_buf)
{
    int i;
    TSK_FS_INFO *fs = (TSK_FS_INFO *) & xfs->fs_info;
    xfs_sb *sb = xfs->fs;
    TSK_INUM_T ibase = 0;

    if (dino_buf == NULL) {
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_ARG);
        tsk_error_set_errstr("xfs_dinode_copy: dino_buf is NULL");
        return 1;
    }

    fs_meta->attr_state = TSK_FS_META_ATTR_EMPTY;
    if (fs_meta->attr) {
        tsk_fs_attrlist_markunused(fs_meta->attr);
    }

    // set the type
    switch (tsk_getu16(fs->endian, dino_buf->di_mode) & XFS_IN_FMT) {
        case XFS_IN_REG:
            fs_meta->type = TSK_FS_META_TYPE_REG;
            break;
        case XFS_IN_DIR:
            fs_meta->type = TSK_FS_META_TYPE_DIR;
            break;
        case XFS_IN_SOCK:
            fs_meta->type = TSK_FS_META_TYPE_SOCK;
            break;
        case XFS_IN_LNK:
            fs_meta->type = TSK_FS_META_TYPE_LNK;
            break;
        case XFS_IN_BLK:
            fs_meta->type = TSK_FS_META_TYPE_BLK;
            break;
        case XFS_IN_CHR:
            fs_meta->type = TSK_FS_META_TYPE_CHR;
            break;
        case XFS_IN_FIFO:
            fs_meta->type = TSK_FS_META_TYPE_FIFO;
            break;
        default:
            fs_meta->type = TSK_FS_META_TYPE_UNDEF;
            break;
    }

    // set the mode
    fs_meta->mode = 0;
    if (tsk_getu16(fs->endian, dino_buf->di_mode) & XFS_IN_ISUID)
        fs_meta->mode |= TSK_FS_META_MODE_ISUID;
    if (tsk_getu16(fs->endian, dino_buf->di_mode) & XFS_IN_ISGID)
        fs_meta->mode |= TSK_FS_META_MODE_ISGID;
    if (tsk_getu16(fs->endian, dino_buf->di_mode) & XFS_IN_ISVTX)
        fs_meta->mode |= TSK_FS_META_MODE_ISVTX;

    if (tsk_getu16(fs->endian, dino_buf->di_mode) & XFS_IN_IRUSR)
        fs_meta->mode |= TSK_FS_META_MODE_IRUSR;
    if (tsk_getu16(fs->endian, dino_buf->di_mode) & XFS_IN_IWUSR)
        fs_meta->mode |= TSK_FS_META_MODE_IWUSR;
    if (tsk_getu16(fs->endian, dino_buf->di_mode) & XFS_IN_IXUSR)
        fs_meta->mode |= TSK_FS_META_MODE_IXUSR;

    if (tsk_getu16(fs->endian, dino_buf->di_mode) & XFS_IN_IRGRP)
        fs_meta->mode |= TSK_FS_META_MODE_IRGRP;
    if (tsk_getu16(fs->endian, dino_buf->di_mode) & XFS_IN_IWGRP)
        fs_meta->mode |= TSK_FS_META_MODE_IWGRP;
    if (tsk_getu16(fs->endian, dino_buf->di_mode) & XFS_IN_IXGRP)
        fs_meta->mode |= TSK_FS_META_MODE_IXGRP;

    if (tsk_getu16(fs->endian, dino_buf->di_mode) & XFS_IN_IROTH)
        fs_meta->mode |= TSK_FS_META_MODE_IROTH;
    if (tsk_getu16(fs->endian, dino_buf->di_mode) & XFS_IN_IWOTH)
        fs_meta->mode |= TSK_FS_META_MODE_IWOTH;
    if (tsk_getu16(fs->endian, dino_buf->di_mode) & XFS_IN_IXOTH)
        fs_meta->mode |= TSK_FS_META_MODE_IXOTH;

    fs_meta->nlink = tsk_getu32(fs->endian, dino_buf->di_nlink);
    fs_meta->size = tsk_getu64(fs->endian, dino_buf->di_size);
    fs_meta->addr = inum;

    /* the general size value in the inode is only 32-bits,
     * but the i_dir_acl value is used for regular files to
     * hold the upper 32-bits
     *
     * The RO_COMPAT_LARGE_FILE flag in the super block will identify
     * if there are any large files in the file system
     */
    fs_meta->uid = tsk_getu32(fs->endian, dino_buf->di_uid);
    fs_meta->gid = tsk_getu32(fs->endian, dino_buf->di_gid);

    fs_meta->mtime = dino_buf->di_mtime.t_sec;
    fs_meta->atime = dino_buf->di_atime.t_sec;
    fs_meta->ctime = dino_buf->di_ctime.t_sec;

    fs_meta->mtime_nano = dino_buf->di_mtime.t_nsec;            
    fs_meta->atime_nano = dino_buf->di_atime.t_nsec;
    fs_meta->ctime_nano = dino_buf->di_ctime.t_nsec;
    fs_meta->seq = 0;

    if (fs_meta->link) {
         free(fs_meta->link);
         fs_meta->link = NULL;
    }

    if (fs_meta->content_len != XFS_CONTENT_LEN_V5(xfs)) {
         if (tsk_verbose) {
            fprintf(stderr, "xfs.c: content_len is not XFS_CONTENT_LEN_V5\n");
         }

         if ((fs_meta =
                 tsk_fs_meta_realloc(fs_meta,
                     XFS_CONTENT_LEN_V5(xfs))) == NULL) {
             return 1;
         }
    }
    
    // Allocating datafork area in content_ptr
    // Contents after inode core must be copied to content ptr
    TSK_OFF_T dfork_offset = xfs_inode_get_offset(xfs, inum) + sizeof(xfs_dinode);

    char* content_buf = (char*)tsk_malloc(XFS_CONTENT_LEN_V5(xfs));
    ssize_t cnt = tsk_fs_read(fs, dfork_offset, content_buf, XFS_CONTENT_LEN_V5(xfs));

    if (cnt != XFS_CONTENT_LEN_V5(xfs)){
        if (tsk_verbose) {
            fprintf(stderr, "invalid datafork read size, cnt: %d\n", cnt);
        }
        return -1;
    }

    fs_meta->content_ptr = (void*) content_buf;

    if (dino_buf->di_format == XFS_DINODE_FMT_LOCAL){
        fs_meta->content_type = TSK_FS_META_CONTENT_TYPE_XFS_DATA_FORK_SHORTFORM;  
    }
    else if (dino_buf->di_format == XFS_DINODE_FMT_EXTENTS){
        fs_meta->content_type = TSK_FS_META_CONTENT_TYPE_XFS_DATA_FORK_EXTENTS;
    }
    else if (dino_buf->di_format == XFS_DINODE_FMT_BTREE){
        fs_meta->content_type = TSK_FS_META_CONTENT_TYPE_XFS_DATA_FORK_BTREE;
    }
    else{
        fprintf(stderr, "xfs : inode core format not supported : inode format %d\n", dino_buf->di_format);
    } 

    return 0;
}    

uint8_t xfs_inode_walk(TSK_FS_INFO * fs, TSK_INUM_T start_inum, TSK_INUM_T end_inum,
    TSK_FS_META_FLAG_ENUM flags, TSK_FS_META_WALK_CB a_action, void *a_ptr)
{
    char *myname = "xfs_inode_walk";
    TSK_INUM_T inum;
    TSK_INUM_T end_inum_tmp;
    TSK_FS_FILE * fs_file;
    unsigned int myflags;
    
    tsk_error_reset();

    if(start_inum < fs->first_inum || start_inum > fs->last_inum){
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_WALK_RNG);
        tsk_error_set_errstr("%s: start inode: %s" PRIuINUM "", myname, start_inum);
        return 1;
    }
    if(end_inum < fs->first_inum || end_inum > fs->last_inum || end_inum < start_inum){
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_WALK_RNG);
        tsk_error_set_errstr("%s: end inode: %s" PRIuINUM "", myname, end_inum);
        return 1;
    }
    if (flags & TSK_FS_META_FLAG_ORPHAN) {
        flags |= TSK_FS_META_FLAG_UNALLOC;
        flags &= ~TSK_FS_META_FLAG_ALLOC;
        flags |= TSK_FS_META_FLAG_USED;
        flags &= ~TSK_FS_META_FLAG_UNUSED;
    }
    else {
        if (((flags & TSK_FS_META_FLAG_ALLOC) == 0) &&
            ((flags & TSK_FS_META_FLAG_UNALLOC) == 0)) {
            flags |= (TSK_FS_META_FLAG_ALLOC | TSK_FS_META_FLAG_UNALLOC);
        }

        /* If neither of the USED or UNUSED flags are set, then set them
         * both
         */
        if (((flags & TSK_FS_META_FLAG_USED) == 0) &&
            ((flags & TSK_FS_META_FLAG_UNUSED) == 0)) {
            flags |= (TSK_FS_META_FLAG_USED | TSK_FS_META_FLAG_UNUSED);
        }
    }
    /* If we are looking for orphan files and have not yet filled
     * in the list of unalloc inodes that are pointed to, then fill
     * in the list
     */
    if ((flags & TSK_FS_META_FLAG_ORPHAN)) {
        if (tsk_fs_dir_load_inum_named(fs) != TSK_OK) {
            tsk_error_errstr2_concat
                ("- ext2fs_inode_walk: identifying inodes allocated by file names");
            return 1;
        }
    }
    
    if ((fs_file = tsk_fs_file_alloc(fs)) == NULL)
        return 1;

    return -1;
}

//block walk
uint8_t xfs_block_walk(TSK_FS_INFO * fs, TSK_DADDR_T start, TSK_DADDR_T end, 
    TSK_FS_BLOCK_WALK_FLAG_ENUM flags, TSK_FS_BLOCK_WALK_CB cb, void *ptr)
{
    return -1;
}

//block_getflags
TSK_FS_BLOCK_FLAG_ENUM xfs_block_getflags(TSK_FS_INFO * a_fs, TSK_DADDR_T a_addr)
{
    int flags = 0;

    return flags;
}

static uint8_t 
xfs_inode_lookup(TSK_FS_INFO * fs, TSK_FS_FILE * a_fs_file,  // = file_add_meta
    TSK_INUM_T inum)
{
    XFS_INFO * xfs = (XFS_INFO *) fs;
    xfs_dinode * dino_buf = NULL;
    unsigned int size = 0;
    if (a_fs_file == NULL) {
        tsk_error_set_errno(TSK_ERR_FS_ARG);
        tsk_error_set_errstr("xfs_inode_lookup: fs_file is NULL");
        return 1;
    }

    if (a_fs_file->meta == NULL) {
        if ((a_fs_file->meta =      
                tsk_fs_meta_alloc(XFS_CONTENT_LEN_V5(xfs))) == NULL) // #define XFS_CONTENT_LEN 
            return 1;
    }
    else {
        tsk_fs_meta_reset(a_fs_file->meta);
    }

    // see if they are looking for the special "orphans" directory
    if (inum == TSK_FS_ORPHANDIR_INUM(fs)) {
        if (tsk_fs_dir_make_orphan_dir_meta(fs, a_fs_file->meta))
            return 1;
        else
            return 0;
    }

    size =
        xfs->inode_size > 
        sizeof(xfs_dinode) ? xfs->inode_size : sizeof(xfs_dinode);

    if((dino_buf = (xfs_dinode *)tsk_malloc(size)) == NULL){
        return 1;
    }

    if (xfs_dinode_load(xfs, inum, dino_buf)){
        free(dino_buf);
        return 1;
    }

    if (xfs_dinode_copy(xfs, a_fs_file->meta, inum, dino_buf)){
        free(dino_buf);
        return 1;
    }

    free(dino_buf);
    return 0;
}

//fsstat
uint8_t xfs_fsstat(TSK_FS_INFO * fs, FILE * hFile)
{
	unsigned int i;
    XFS_INFO * xfs = (XFS_INFO *) fs;
    xfs_sb *sb = xfs->fs;
    
    int ibpg;
    time_t imptime;
    char timeBuf[128];
    const char *tmptypename;
    
    tsk_error_reset();
    tsk_fprintf(hFile, "FILE SYSTEM INFORMATION\n");
    tsk_fprintf(hFile, "--------------------------------------------\n");
    
    if (tsk_getu32(fs->endian, sb->sb_magicnum) == XFS_FS_MAGIC)
        tmptypename = "XFS";
    
    tsk_fprintf(hFile, "File System Type : %s\n", tmptypename);
    tsk_fprintf(hFile, "Volume Name : %s\n", sb->sb_fname);
    tsk_fprintf(hFile, "\n");
    
    
    if(tsk_getu32(fs->endian, sb->sb_features_incompat)) {
        tsk_fprintf(hFile, "InCompat Features: ");
        
        if (tsk_getu32(fs->endian, sb->sb_features_incompat) &
            XFS_SB_FEAT_INCOMPAT_FTYPE)
            tsk_fprintf(hFile, "Directory file type, ");
        if (tsk_getu32(fs->endian, sb->sb_features_incompat) &
            XFS_SB_FEAT_INCOMPAT_SPINODES)
            tsk_fprintf(hFile, "Sparse inodes, ");
        if (tsk_getu32(fs->endian, sb->sb_features_incompat) &
            XFS_SB_FEAT_INCOMPAT_META_UUID)
            tsk_fprintf(hFile, "Metadata UUID");
        
        tsk_fprintf(hFile, "\n");
    }
    
    if(tsk_getu32(fs->endian, sb->sb_features_ro_compat)) {
        tsk_fprintf(hFile, "Read Only Compat Features : " );
        
        if (tsk_getu32(fs->endian, sb->sb_features_ro_compat) &
            XFS_SB_FEAT_RO_COMPAT_FINOBT)
            tsk_fprintf(hFile, "Free inode B+tree, ");
        if (tsk_getu32(fs->endian, sb->sb_features_ro_compat) &
            XFS_SB_FEAT_RO_COMPAT_RMAPBT)
            tsk_fprintf(hFile, "Reverse mapping B+tree, ");
        if (tsk_getu32(fs->endian, sb->sb_features_ro_compat) &
            XFS_SB_FEAT_RO_COMPAT_REFLINK)
            tsk_fprintf(hFile, "Reference count B+tree");
        
        tsk_fprintf(hFile, "\n");
    }
    
    tsk_fprintf(hFile, "\nMETADATA INFORMATION\n");
    tsk_fprintf(hFile, "--------------------------------------------\n");
    tsk_fprintf(hFile, "Root Inode : %" PRIu64 "\n", tsk_getu64(fs->endian, sb->sb_rootino));
    tsk_fprintf(hFile, "Inode Count : %" PRIu64 "\n", tsk_getu64(fs->endian, sb->sb_icount));
    tsk_fprintf(hFile, "Free Inode Count : %" PRIu64 "\n", tsk_getu64(fs->endian, sb->sb_ifree));
    tsk_fprintf(hFile, "Inode Size : %" PRIu16 "\n", tsk_getu16(fs->endian, sb->sb_inodesize));
    tsk_fprintf(hFile, "Inode per Block : %" PRIu8 "\n", sb->sb_inopblog);
    
    tsk_fprintf(hFile, "\nCONTENT INFORMATION\n");
    tsk_fprintf(hFile, "--------------------------------------------\n");
    tsk_fprintf(hFile, "Block Range : %" PRIuINUM " - %" PRIuINUM "\n", fs->first_block, fs->last_block);
    tsk_fprintf(hFile, "Block Size : %" PRIu32 "\n", tsk_getu32(fs->endian, sb->sb_blocksize));
    tsk_fprintf(hFile, "Block Count : %" PRIu64 "\n", tsk_getu64(fs->endian, sb->sb_dblocks));
    tsk_fprintf(hFile, "Free Block Count : %" PRIu64 "\n", tsk_getu64(fs->endian, sb->sb_fdblocks));
    tsk_fprintf(hFile, "Allocation Group Block Size : % " PRIu32 "\n", tsk_getu32(fs->endian, sb->sb_agblocks));
    tsk_fprintf(hFile, "Allocation Group Count : %" PRIu32 "\n", tsk_getu32(fs->endian, sb->sb_agcount));
    tsk_fprintf(hFile, "Sector Size : %" PRIu16 "\n", tsk_getu16(fs->endian, sb->sb_sectsize));
    
    tsk_fprintf(hFile, "\nLOG INFORMATION\n");
    tsk_fprintf(hFile, "——————————————————————\n");
    tsk_fprintf(hFile, "Log2 of Block Size : %" PRIu8 "\n", sb->sb_blocklog);
    tsk_fprintf(hFile, "Log2 of Sector Size : %" PRIu8 "\n", sb->sb_sectlog);
    tsk_fprintf(hFile, "Log2 of Inode Size : %" PRIu8 "\n", sb->sb_inodelog);
    tsk_fprintf(hFile, "Log2 of Inode per Block : %" PRIu8 "\n", sb->sb_inopblog);
    tsk_fprintf(hFile, "Log2 of Allocation Block Size : %" PRIu8 "\n", sb->sb_agblklog);
    tsk_fprintf(hFile, "Log2 of Extent Count : %" PRIu8 "\n", sb->sb_rextslog);
    tsk_fprintf(hFile, "Log2 of Extent Count : %" PRIu8 "\n", sb->sb_inprogress);
    tsk_fprintf(hFile, "Inode max persentage : %" PRIu8 "\n", sb->sb_imax_pct);
    
    return -1;
}

uint8_t xfs_fscheck(TSK_FS_INFO * fs, FILE * HFile)
{
    return -1;
}

uint8_t xfs_istat(TSK_FS_INFO * fs, TSK_FS_ISTAT_FLAG_ENUM flags, FILE * hFile, TSK_INUM_T inum,
            TSK_DADDR_T numblock, int32_t sec_skew)
{
    return -1;
}

void xfs_close(TSK_FS_INFO * fs)
{
    XFS_INFO * xfs = (XFS_INFO *) fs;

    fs->tag = 0;
    free(xfs->fs);
    free(xfs->bmap_buf);
    free(xfs->imap_buf);
    
    tsk_deinit_lock(&xfs->lock);
    tsk_fs_free(fs);
    return;
}

TSK_FS_INFO *
xfs_open(TSK_IMG_INFO * img_info, TSK_OFF_T offset,
    TSK_FS_TYPE_ENUM ftype, uint8_t test)
{
    XFS_INFO *xfs;
    unsigned int len;
    TSK_FS_INFO *fs;
    ssize_t cnt;

    tsk_error_reset();

    if(TSK_FS_TYPE_ISXFS(ftype) == 0){
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_ARG);
        tsk_error_set_errstr("Invalid FS Type in xfs_open");
        return NULL;
    }

    if (img_info->sector_size == 0) {
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_ARG);
        tsk_error_set_errstr("xfs_open: sector size is 0");
        return NULL;
    }

    if ((xfs = (XFS_INFO *) tsk_fs_malloc(sizeof(*xfs))) == NULL)
        return NULL;
    fs = &(xfs->fs_info);
    fs->ftype = ftype;
    fs->flags = 0;
    fs->img_info = img_info;
    fs->offset = offset;
    fs->tag = TSK_FS_INFO_TAG;

    len = sizeof(xfs_sb);    

    if ((xfs->fs = (xfs_sb *) tsk_malloc(len)) == NULL) {
        fs->tag = 0;
        tsk_fs_free((TSK_FS_INFO *)xfs);
        return NULL;
    }

    cnt = tsk_fs_read(fs, XFS_SBOFF, (char *) xfs->fs, len);

    if (cnt != len){
        if (cnt >= 0) {
            tsk_error_reset();
            tsk_error_set_errno(TSK_ERR_FS_READ);
        }
        tsk_error_set_errstr("xfs_open: superblock");
        fs->tag = 0;
        free(xfs->fs);
        tsk_fs_free((TSK_FS_INFO *)xfs);
        return NULL;
    }
    
    if(tsk_fs_guessu32(fs, xfs->fs->sb_magicnum, XFS_FS_MAGIC)){
        if (tsk_verbose){
            fprintf(stderr, "xfs_open : superblock magic failed\n");
            fprintf(stderr, "xfs_open : superblock read : %x%x%x%x\n", 
                xfs->fs->sb_magicnum[0], xfs->fs->sb_magicnum[1], xfs->fs->sb_magicnum[2], xfs->fs->sb_magicnum[3]);
        }

        fs->tag = 0;
        free(xfs->fs);
        tsk_fs_free((TSK_FS_INFO *)xfs);
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_MAGIC);
        tsk_error_set_errstr("not an xfs file system (magic)");
        
        if (tsk_verbose)
            fprintf(stderr, "xfs_open : invalid magic\n");
        return NULL;
    }

    fs->inum_count = tsk_getu64(fs->endian, xfs->fs->sb_icount);
    fs->last_inum = 0xFFFFFFFFFFFFFFFF;
    fs->first_inum = XFS_FIRSTINO;
    fs->root_inum = tsk_getu64(fs->endian, xfs->fs->sb_rootino);
    
    if (tsk_getu64(fs->endian, xfs->fs->sb_icount) < 10) {
        fs->tag = 0;
        free(xfs->fs);
        tsk_fs_free((TSK_FS_INFO *)xfs);
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_MAGIC);
        tsk_error_set_errstr("Not an XFS file system (inum count)");
        if (tsk_verbose)
            fprintf(stderr, "xfs_open: two few inodes\n");
        return NULL;
    }
    /* Set the size of the inode, but default to our data structure
     * size if it is larger */
    xfs->inode_size = tsk_getu16(fs->endian, xfs->fs->sb_inodesize);
    if (xfs->inode_size < sizeof(xfs_dinode)) {
        if (tsk_verbose)
            tsk_fprintf(stderr, "SB inode size is small");
    }

    /*
     * Calculate the block info
     */
    fs->dev_bsize = img_info->sector_size;
    fs->first_block = 0;
    fs->block_count = (TSK_DADDR_T)tsk_getu64(fs->endian, xfs->fs->sb_dblocks);
    fs->last_block_act = fs->last_block = fs->block_count - 1;
    fs->block_size = tsk_getu32(fs->endian, xfs->fs->sb_blocksize);

    if((fs->block_size == 0) || (fs->block_size % 512)){
        fs->tag = 0;
        free(xfs->fs);
        tsk_fs_free((TSK_FS_INFO *)xfs);
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_MAGIC);
        tsk_error_set_errstr("Not an XFS file system (block size)");
        if(tsk_verbose)
            fprintf(stderr, "xfs_open : invalid block size\n");
        return NULL;
    }

    if ((TSK_DADDR_T) ((img_info->size - offset) / fs->block_size) <
        fs->block_count)
        fs->last_block_act =
            (img_info->size - offset) / fs->block_size - 1;

    /* Volume ID */
    for(fs->fs_id_used = 0 ; fs->fs_id_used < 16; fs->fs_id_used++){
        fs->fs_id[fs->fs_id_used] = xfs->fs->sb_uuid[fs->fs_id_used];
    }

    /* Set the generic function pointers */
    fs->inode_walk = xfs_inode_walk;
    fs->block_walk = xfs_block_walk;
    fs->block_getflags = xfs_block_getflags;

    fs->get_default_attr_type = tsk_fs_unix_get_default_attr_type;
    fs->load_attrs = xfs_load_attrs;

    fs->file_add_meta = xfs_inode_lookup;
    fs->dir_open_meta = xfs_dir_open_meta;
    fs->fsstat = xfs_fsstat;
    fs->fscheck = xfs_fscheck;
    fs->istat = xfs_istat;
    fs->name_cmp = tsk_fs_unix_name_cmp;
    fs->close = xfs_close;

    /*
     * Print some stats.
     */
    if (tsk_verbose)
        tsk_fprintf(stderr,
            "inodes %" PRIu32 " root ino %" PRIuINUM " blocks %" PRIu32
            " inodes/block %" PRIu32 "\n", tsk_getu64(fs->endian,
                xfs->fs->sb_icount),
            fs->root_inum, tsk_getu64(fs->endian,
                xfs->fs->sb_dblocks), tsk_getu16(fs->endian,
                xfs->fs->sb_inopblock));

    tsk_init_lock(&xfs->lock);

    return (fs);
}

