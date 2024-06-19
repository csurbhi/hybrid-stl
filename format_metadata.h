#include <linux/types.h>

/*
 * Design:
 * SB1, SB2, CKPT1, CKPT2, Seq zone information, SIT, Rev Translation Map, Data
 *
 * The reverse translation entries for zones in the cache.
 * a) We only keep one copy. The entries are updated in a sequential manner, much like a log.
 * You can loose entries worth one block, as this is the minimum write unit. There is nothing
 * we can do about it.
 * b) These are entries maintained only for the log structured cache.
 *
 *
 *  SIT for zones in the cache:
 * 1. We keep only one copy.
 * 2. If one sector gets destroyed while writing, then we do the following:
 *       i) We create the RB tree by reading the translation map.
 *       ii) While doing so, we keep a count of the valid blocks in every 
 *	    sector and then rebuild the SIT.
 *       iii) This way, we can ignore what is on disk.
 *       iv) We use the mtime that is stored in the last ckpt for entries where
 *	    the valid blocks dont match with our calculation.
 *
 *
 * Checkpoint for zones in the cache:
 *        We keep two copies of this. We need the mtime for further SIT
 *        calculation. Also this is just one sector of information. We keep one
 *        checkpoint in one block. We write alternately to the checkpoints. The
 *        checkpoint with the higher elapsed time is the right checkpoint.
 * 
 *
 * So our order of writing is as follows:
 * 1. Write the data
 * 2. Write trans map entries: when a block full of entries are written, flush them to the disk.
 * 3. Write checkpoint
 * 4. Write SIT
 * 
 */

typedef __le64 u64;
typedef __le32 u32;
typedef __le16 u16;
typedef u64 uint64_t;
typedef u32 uint32_t;
typedef u16 uint16_t;
typedef u64 sector_t;

#ifndef SECTOR_SIZE
	#define SECTOR_SIZE 512
#endif
#ifndef BLOCK_SIZE
	#define BLOCK_SIZE 4096
#endif
#define BLK_SZ 4096
#define STL_SB_MAGIC 0x7853544c
#define STL_CKPT_MAGIC 0x1A2B3C4D

/* Each zone is 256MB in size.
 * there are 65536 blocks in a zone
 * there are 524288 sectors in a zone
 * if 1 bit per block, then we need
 * 8192 bytes for the block bitmap in a zone.
 * We are right now using a block bitmap
 * rather than a sector bitmap as FS writes
 * in terms of blocks.
 * 
 * A 8TB disk has 32768 zones. Thus we need
 * 65536 blocks to maintain the bitmaps alone.
 * and 642 blocks to maintain the other information
 * such as vblocks and mtime.
 * Thus total of 66178 blocks are needed 
 * that comes out to be 258MB of metadata
 * information for GC.
#define VBLK_MAP_SIZE 8192
__u8 valid_map[VBLK_MAP_SIZE];
 */

#define NR_CACHE_ZONES 64

#define SIT_ENTRIES_BLK 	(BLK_SIZE/sizeof(struct lsdm_seg_entry))
#define REV_TM_ENTRIES_BLK 		(BLK_SIZE/sizeof(struct rev_tm_entry))

struct lsdm_seg_entry {
	unsigned int vblocks;  /* maximum vblocks currently are 65536 */
	unsigned long mtime;
	unsigned int lzones;
	/* We do not store any valid map here
	 * as an extent map is stored separately
	 * as a part of the translation map
	 */
    	/* can also add Type of segment */
} __attribute__((packed));

struct stl_dzones_info {
	unsigned int pzonenr;	/* The actual physical zonenr where this logical zone's data is stored */
	sector_t wp;
}__attribute__((packed));


#define NR_SECTORS_PER_BLK		8	/* 4096 / 512 */
#define BLK_SIZE			4096
#define NR_EXT_ENTRIES_PER_SEC		((SECTOR_SIZE - 4)/(sizeof(struct lsdm_revmap_extent)))
//#define NR_EXT_ENTRIES_PER_BLK 		(NR_EXT_ENTRIES_PER_SEC * NR_SECTORS_IN_BLK)
#define NR_EXT_ENTRIES_PER_BLK 		48
#define MAX_EXTENTS_PER_ZONE		65536

struct lsdm_ckpt {
	uint32_t magic;
	__le64 version;
	__le64 user_block_count;
	__le32 nr_invalid_zones;	/* zones that have errors in them */
	__le64 hot_frontier_pba;
	__le32 nr_free_cache_zones;
	__le64 elapsed_time;		/* records the time elapsed since all the mounts */
	__u8 clean;			/* becomes 0 in ctr and 1 in dtr. Used to identify crash */
	__le64 crc;
	unsigned char padding[0]; /* write all this in the padding */
} __attribute__((packed));

#define STL_SB_SIZE 4096

struct lsdm_sb {
	__le32 magic;			/* Magic Number */
	__le32 version;			/* Superblock version */
	__le32 log_sector_size;		/* log2 sector size in bytes */
	__le32 log_block_size;		/* log2 block size in bytes */
	__le32 log_zone_size;		/* log2 zone size in bytes */
	__le32 checksum_offset;		/* checksum offset inside super block */
	__le64 blk_count_ckpt;		/* # of blocks for checkpoint */
	__le64 blk_count_rtm;		/* # of segments for Translation map */
	__le64 blk_count_sit;		/* # of segments for SIT */
	__le64 blk_count_dzit;		/* # of segments for SIT */
	__le64 zone_count;		/* total # of segments */
	__le64 zone_count_data;		/* # of segments for data area */
	__le64 zone_count_cache;
	__le64 zone_count_metadata;
	__le64 rtm_pba;			/* start block address of translation map */
	__le64 ckpt1_pba;		/* start address of checkpoint 1 */
        __le64 ckpt2_pba;		/* start address of checkpoint 2 */
	__le64 sit_pba;			/* start block address of SIT */
	__le64 dzit_pba;		/* start block address of seq zones information */
	__le32 nr_lbas_in_zone;
	__le64 nr_cmr_zones;
	__le64 dzone0_pba;		/* start block address of sequential/data zone 0 */
	__le64 czone0_pba;		/* start block address of cache zone 0 */
	__le64 max_pba;                 /* The last lba in the disk */
	__le64 max_cache_pba;           /* The last lba in the cache */
	__le32 crc;			/* checksum of superblock */
	__u8 reserved[0];		/* valid reserved region. Rest of the block space */
} __attribute__((packed));

struct rev_tm_entry {
	sector_t lba;
} __attribute__((packed));
