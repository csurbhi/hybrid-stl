#include <stdio.h>

#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
#include <sys/types.h>
#include <unistd.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>
#include <linux/types.h>
#include <linux/fs.h>
#include "format_metadata.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
//#include <zlib.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <linux/blkzoned.h>
#include <string.h>



#define BLK_SZ 4096
#define NR_CKPT_COPIES 2 
#define NR_BLKS_SB 2
#define NR_SECTORS_IN_BLK 8
#define SECTORS_SHIFT 3
#define BITS_IN_BYTE 8

/* We want the cache to be 0.05 percent of the data zones.
 * Nr of data zones is 29808 for this HA-SMR drive.
 * TODO: Add the nr of zones in cache as a command line argument.
 */

#define NR_CACHE_ZONES 140

int get_total_cache_zones()
{
	return NR_CACHE_ZONES;
}


unsigned int crc32(int d, unsigned char *buf, unsigned int size)
{
	return 0;
}

int open_disk(char *dname)
{
	int fd;

	printf("\n Opening %s ", dname);

	fd = open(dname, O_RDWR);
	if (fd < 0) {
		perror("Could not open the disk: ");
		printf("\n");
		exit(errno);
	}
	printf("\n %s opened with fd: %d ", dname, fd);
	return fd;
}

int write_to_disk(int fd, char *buf, unsigned long sectornr)
{
	int ret = 0;
	u64 offset = sectornr * SECTOR_SIZE;

	if (fd < 0) {	
		printf("\n Invalid, closed fd sent!");
		return -1;
	}

	//printf("\n %s writing at sectornr: %d", __func__, sectornr);
	ret = lseek64(fd, offset, SEEK_SET);
	if (ret < 0) {
		printf("\n write to disk offset: %u, sectornr: %d ret: %d", offset, sectornr, ret);
		perror("!! (before write) Error in lseek64: \n");
		exit(errno);
	}
	ret = write(fd, buf, BLK_SZ);
	if (ret < 0) {
		perror("Error while writing: ");
		exit(errno);
	}
	return (ret);
}



__le32 get_zone_count(int fd)
{
	__le32 zone_count = 0, zonesz;
	char str[400];
    	__u64 capacity = 0;

	/* get the capacity in bytes */
	if (ioctl(fd, BLKGETSIZE64, &capacity) < 0) {
		sprintf(str, "Get capacity failed %d (%s)\n", errno, strerror(errno));
		perror(str);
		exit(errno);
	}
	printf("\n capacity: %llu", capacity);

	if (ioctl(fd, BLKGETZONESZ, &zonesz) < 0) {
		sprintf(str, "Get zone size failed %d (%s)\n", errno, strerror(errno));
		perror(str);
		exit(errno);
	}
	printf("\n ZONE size: %llu", zonesz);
	/* zonesz is number of addressable lbas */
	zonesz = zonesz << 9;

	if (ioctl(fd, BLKGETNRZONES, &zone_count) < 0) {
		sprintf(str, "Get nr of zones ioctl failed %d (%s)\n", errno, strerror(errno));
		perror(str);
		exit(errno);
	}

	printf("\n Nr of zones reported by disk :%llu", zone_count);
	/* Use zone queries and find this eventually
	 * Doing this manually for now for a 20GB 
	 * harddisk and 256MB zone size.
	 */
	if (zone_count >= (capacity/zonesz)) {
		printf("\n Number of zones: %d ", zone_count);
		printf("\n capacity/ZONE_SZ: %d ", capacity/zonesz);
		return capacity/zonesz;
	}
	printf("\n Actual zone count calculated: %d ", (capacity/zonesz));
	//return 16500;
	//return 28200;
	return zone_count;
	//return 4500;
	//return 9000;
	//return 10000;
	// 2048 GB
	//return 8192;
	//return 29807;
	//return (capacity/zonesz);
	//return 100;
	//return 1900;
	/* we test with a disk capacity of 1 TB */
	//return 4032;
	//return 500;
	//return 256;
	//return 100;
	/* 
	 * resilvering test1 - returned this
	 * return 3000;
	 *
	 * 2000 zones is 500GB. 450GB is 90% of 500GB.
	 * We are using this to perform the test.
	 *
	 * Next we will perform the test with 1900 zones
	 * ie 475 GB
	 *
	 */
	//return 2500;
	//return 29808;
	//return 209;
}

/* Note,  that this also account for the first few metadata zones.
 * but we do not care right now. While writing the extents, its 
 * just for the data area. So we end up allocating slightly
 * more space than is strictly necessary.
 */
__le64 get_nr_blks(struct lsdm_sb *sb)
{
	unsigned long nr_blks = (sb->zone_count << (sb->log_zone_size - sb->log_block_size));
	return nr_blks;
}

__le64 get_max_cache_pba(struct lsdm_sb *sb, unsigned int cache_zones)
{
	return (cache_zones << (sb->log_zone_size - sb->log_sector_size));
}
/* We store a tm entry for every 4096 block
 *
 * This accounts for the blks in the main area and excludes the blocks
 * for the metadata. Hence we do NOT call get_nr_blks()
 */
__le32 get_rtm_blk_count(struct lsdm_sb *sb)
{
	u64 nr_rtm_entries = (sb->max_cache_pba / NR_SECTORS_IN_BLK);
	u32 nr_rtm_entries_per_blk = BLK_SZ/ sizeof(struct rev_tm_entry);
	u64 nr_rtm_blks = nr_rtm_entries / nr_rtm_entries_per_blk;
	if (nr_rtm_entries % nr_rtm_entries_per_blk)
		nr_rtm_blks++;

	printf("\n %s blocksize: %d sizeof(struct rtm_entry): %d ", __func__, BLK_SZ, sizeof(struct rev_tm_entry));
	printf("\n %s nr_rtm_entries: %llu ", __func__, nr_rtm_entries);
	printf("\n %s nr_rtm_entries_per_blk: %d ", __func__, nr_rtm_entries_per_blk);
	printf("\n %s nr_rtm_blks: %llu",  __func__, nr_rtm_entries / nr_rtm_entries_per_blk);	
	printf("\n %s nr_rtm_count: %llu", __func__, nr_rtm_blks);
	return nr_rtm_blks;
}

__le32 get_nr_cache_zones(struct lsdm_sb *sb)
{
	return sb->zone_count_cache;
}

__le32 get_sit_blk_count(struct lsdm_sb *sb)
{
	unsigned int one_sit_sz = 80; /* in bytes */
	unsigned int nr_sits_in_blk = BLK_SZ / one_sit_sz;
	__le32 nr_sits = get_total_cache_zones();
	unsigned int blks_for_sit = nr_sits / nr_sits_in_blk;
	if (nr_sits % nr_sits_in_blk > 0)
		blks_for_sit = blks_for_sit + 1;

	return blks_for_sit;
}


__le32 get_seqz_blk_count(struct lsdm_sb *sb)
{
	unsigned int nr_seq_zns = sb->zone_count - get_total_cache_zones();
	unsigned int one_seqz_sz = sizeof(struct stl_dzones_info);
	unsigned int nr_seqz_in_blk = BLK_SZ/one_seqz_sz;
	unsigned int blks_for_dzit = nr_seq_zns / nr_seqz_in_blk;
       if (nr_seq_zns % nr_seqz_in_blk	> 0)
	       blks_for_dzit = blks_for_dzit + 1;

       return blks_for_dzit;
}

/* We write all the metadata blks sequentially in the CMR zones
 * that can be rewritten in place
 */
__le32 get_metadata_zone_count(struct lsdm_sb *sb)
{
	/* we add the 2 for the superblock */
	unsigned long metadata_blk_count = NR_BLKS_SB + sb->blk_count_ckpt + sb->blk_count_dzit + sb->blk_count_rtm + sb->blk_count_sit;
	unsigned long nr_blks_in_zone = (1 << sb->log_zone_size - sb->log_block_size);
	unsigned long metadata_zone_count = metadata_blk_count / nr_blks_in_zone;
	if (metadata_blk_count % nr_blks_in_zone)
		metadata_zone_count = metadata_zone_count + 1;
	
	printf("\n\n");
	printf("\n ********************************************\n");
	printf("\n sb->blk_count_ckpt: %llu", sb->blk_count_ckpt);
	printf("\n sb->blk_count_dzit: %llu", sb->blk_count_dzit);
	printf("\n sb->blk_count_rtm: %llu", sb->blk_count_rtm);
	printf("\n sb->blk_count_sit: %llu", sb->blk_count_sit);
	printf("\n metadata_blk_count: %llu", metadata_blk_count);
	printf("\n metadata_zone_count: %llu", metadata_zone_count);
	printf("\n ********************************************\n");
	printf("\n\n");
	return metadata_zone_count;
}

__le32 get_data_zone_count(struct lsdm_sb *sb)
{
	__le32 data_zone_count = 0;
	data_zone_count = sb->zone_count - sb->zone_count_metadata - sb->zone_count_cache;
	return data_zone_count;
}

__le32 get_cache_zone_count(struct lsdm_sb *sb)
{
	__le32 cache_zone_count = 0;
	cache_zone_count = get_total_cache_zones() - sb->zone_count_metadata;
	return cache_zone_count;
}

/* We write one raw ckpt in one block
 * If the first LBA is 0, then the first
 * zone has (zone_size/blk_size) - 1 LBAs
 * the first block in the second zone is 
 * the CP and that just takes the next
 * LBA as ZONE_SZ/BLK_SZ
 * The blk adderss is 256MB;
 */
__le64 get_ckpt1_pba(struct lsdm_sb *sb)
{
	return (NR_BLKS_SB * NR_SECTORS_IN_BLK);
}


__le64 get_sit_pba(struct lsdm_sb *sb)
{
	return sb->dzit_pba + (sb->blk_count_dzit * NR_SECTORS_IN_BLK);
}

__le64 get_rtm_pba(struct lsdm_sb *sb)
{
	u64 rtm_pba = sb->sit_pba + (sb->blk_count_sit * NR_SECTORS_IN_BLK);
	return rtm_pba;
}

__le64 get_seqz_pba(struct lsdm_sb *sb)
{
	u64 seqz_pba = sb->ckpt2_pba + NR_SECTORS_IN_BLK;
	return seqz_pba;
}

void read_sb(int fd, unsigned long sectornr)
{
	struct lsdm_sb *sb;
	int ret = 0;
	unsigned long long offset = sectornr * SECTOR_SIZE;

	sb = (struct lsdm_sb *)malloc(BLK_SZ);
	if (!sb)
		exit(-1);
	memset(sb, 0, BLK_SZ);

	printf("\n *********************\n");

	ret = lseek64(fd, offset, SEEK_SET);
	if (ret < 0) {
		perror("\n Could not lseek64: ");
		printf("\n");
		exit(errno);
	}

	ret = read(fd, sb, BLK_SZ);
	if (ret < 0) {
		perror("\n COuld not read the sb: ");
		printf("\n");
		exit(errno);
	}
	if (sb->magic != STL_SB_MAGIC) {
		printf("\n wrong superblock!");
		printf("\n");
		exit(-1);
	}
	printf("\n sb->magic: %d", sb->magic);
	printf("\n sb->version %d", sb->version);
	printf("\n sb->log_sector_size %d", sb->log_sector_size);
	printf("\n sb->log_block_size %d", sb->log_block_size);
	printf("\n sb->log_zone_size: %d", sb->log_zone_size);
	printf("\n sb->blk_count_ckpt %lu", sb->blk_count_ckpt);
	printf("\n sb->max_pba: %lu ", sb->max_pba);
	//printf("\n sb-> %d", sb->);
	printf("\n sb->zone_count: %d", sb->zone_count);
	printf("\n Read verified!!!");
	printf("\n ==================== \n");
	free(sb);
}

__le64 get_max_pba(struct lsdm_sb *sb)
{
	/* We test with a disk of size 1 TB, that is 4032 zones! */
	printf("\n %s zone_count: %d log_zone_size: %d log_sector_size: %d ", __func__, sb->zone_count , sb->log_zone_size, sb->log_sector_size);
	return (sb->zone_count << (sb->log_zone_size - sb->log_sector_size)); 
}

void write_zeroed_blks(int fd, sector_t pba, unsigned nr_blks)
{
	char buffer[BLK_SZ];
	int i, ret;
	u64 offset = pba * SECTOR_SIZE;

	printf("\n Writing %d zeroed blks, from pba: %llu", nr_blks, pba);
	
	ret = lseek64(fd, offset, SEEK_SET);
	if (ret < 0) {
		printf("\n write to disk offset: %u, sectornr: %lld ret: %d", offset, pba, ret);
		perror("!! (before write) Error in lseek64: \n");
		printf("\n");
		exit(errno);
	}
	memset(buffer, 0, BLK_SZ);
	for (i=0; i<nr_blks; i++) {
		//printf("\n i: %d ", i);
		ret = write(fd, buffer, BLK_SZ);
		if (ret < 0) {
			perror("Error while writing: ");
			printf("\n");
			exit(errno);
		}
	}
}


void read_block(int fd, sector_t pba, unsigned nr_blks)
{

	char buffer[4096];
	int i, ret, j;

	printf("\n Reading %d zeroed blks, from pba: %llu", nr_blks, pba);
	
	/* lseek64 offset is in  bytes not sectors, pba is in sectors */
	ret = lseek64(fd, (pba * SECTOR_SIZE), SEEK_SET);
	if (ret < 0) {
		perror(">>>> (before read) Error in lseek64: \n");
		printf("\n");
		exit(errno);
	}
	for (i=0; i<nr_blks; i++) {
		ret = read(fd, buffer, 4096);
		if (ret < 4096) {
			perror("Error while reading: ");
			printf("\n");
			exit(errno);
		}
		for(j=0; j<ret; j++) {
			if(buffer[i] != 0) {
				assert(1);
			}
		}
	}


}

void write_rtm(int fd, sector_t rtm_pba, unsigned nr_blks)
{
	printf("\n ** %s Writing rtm blocks at pba: %llu, nrblks: %u", __func__, rtm_pba, nr_blks);
	write_zeroed_blks(fd, rtm_pba, nr_blks);
	read_block(fd, rtm_pba, nr_blks);
}

unsigned long long get_current_frontier(struct lsdm_sb *sb)
{
	unsigned long rtm_end_pba = sb->rtm_pba + sb->blk_count_rtm * NR_SECTORS_IN_BLK;
	unsigned long rtm_end_blk_nr = rtm_end_pba / NR_SECTORS_IN_BLK;
	unsigned int nr_blks_per_zone = 1 << (sb->log_zone_size- sb->log_block_size);
	unsigned int rtm_end_zone_nr = rtm_end_blk_nr / nr_blks_per_zone;
	
	/* The data zones start in the next zone of that of the last
	 * metadata zone
	 */
	printf("\n rtm_end_pba: %ld", rtm_end_pba);
	printf("\n rtm_end_blk_nr: %ld", rtm_end_blk_nr);
	printf("\n Current Write frontier: tm_end_zone_nr: %d", rtm_end_zone_nr + 1);
	return (rtm_end_zone_nr + 1) << (sb->log_zone_size - sb->log_sector_size);
}


unsigned long get_data_zone_pba(struct lsdm_sb *sb)
{
	u64 dzone_pba = (sb->zone_count_cache + sb->zone_count_metadata) << (sb->log_zone_size- sb->log_sector_size);
	printf("\n Data zone pba: %llu", dzone_pba);
	return dzone_pba;
}

struct lsdm_sb * write_sb(int fd, unsigned long sb_pba, unsigned long cmr)
{
	struct lsdm_sb *sb;
	int ret = 0;
	unsigned int zonesz, logzonesz, zone_count;
	char str[SECTOR_SIZE];
	int total_cache_zones = get_total_cache_zones();

	sb = (struct lsdm_sb *)malloc(BLK_SZ);
	if (!sb)
		exit(-1);
	memset(sb, 0, BLK_SZ);


	if (ioctl(fd, BLKGETZONESZ, &zonesz) < 0) {
		sprintf(str, "Get zone size failed %d (%s)\n", errno, strerror(errno));
		perror(str);
		printf("\n");
		exit(errno);
	}
	printf("\n ZONE size: %llu", zonesz);
	
	logzonesz = 0;
	while(1){
		zonesz = zonesz >> 1;
		if (!zonesz)
			break;
		logzonesz++;
	}
	/* since the zonesz is the number of addressable LBAs */
	logzonesz = logzonesz + 9;
	printf("\n ********************************* LOG ZONE SZ: %d ", logzonesz);
	if (ioctl(fd, BLKGETNRZONES, &zone_count) < 0) {
		sprintf(str, "Get nr of zones ioctl failed %d (%s)\n", errno, strerror(errno));
		perror(str);
		printf("\n");
		exit(errno);
	}

	printf("\n Nr of zones reported by disk :%llu", zone_count);

	sb->magic = STL_SB_MAGIC;
	sb->version = 1;
	sb->log_sector_size = 9;
	/* zonesz that we get from the BLKGETZONESZ ioctl is the number of LBAs in the device */
	zonesz = zonesz << sb->log_sector_size;
	sb->log_block_size = 12;
	sb->log_zone_size = logzonesz;
	sb->checksum_offset = offsetof(struct lsdm_sb, crc);
	sb->zone_count = get_zone_count(fd);
	/* For now we are shunting the 7TB disk to a size of 4TB */
	printf("\n sb->zone_count: %d", sb->zone_count);
	sb->max_pba = get_max_pba(sb);
	printf("\n ******* sb->max_pba: %llu", sb->max_pba);
	sb->max_cache_pba = get_max_cache_pba(sb, total_cache_zones);
	sb->blk_count_rtm = get_rtm_blk_count(sb);
	printf("\n sb->blk_count_rtm: %d", sb->blk_count_rtm);
	sb->blk_count_ckpt = NR_CKPT_COPIES;
	printf("\n sb->blk_count_ckpt: %d", sb->blk_count_ckpt);
	sb->blk_count_sit = get_sit_blk_count(sb);
	printf("\n sb->blk_count_sit: %d", sb->blk_count_sit);
	sb->blk_count_dzit = get_seqz_blk_count(sb);
	printf("\n sb->blk_count_dzit: %d", sb->blk_count_dzit);
	sb->ckpt1_pba = get_ckpt1_pba(sb);
	printf("\n sb->ckpt1_pba: %u", sb->ckpt1_pba);
	sb->ckpt2_pba = sb->ckpt1_pba + NR_SECTORS_IN_BLK;
	printf("\n sb->ckpt2_pba: %u", sb->ckpt2_pba);
	sb->dzit_pba = get_seqz_pba(sb);
	printf("\n sb->dzit_pba: %u ", sb->dzit_pba);
	sb->sit_pba = get_sit_pba(sb);
	printf("\n sb->sit_pba: %u", sb->sit_pba);
	sb->rtm_pba = get_rtm_pba(sb);
	printf("\n sb->rtm_pba: %u", sb->rtm_pba);
	sb->nr_lbas_in_zone = (1 << (sb->log_zone_size - sb->log_sector_size));
	printf("\n nr_lbas_in_zone: %llu ", sb->nr_lbas_in_zone);
	sb->nr_cmr_zones = cmr;
	printf("\n sb->nr_cmr_zones: %llu", sb->nr_cmr_zones);
	sb->zone_count_metadata = get_metadata_zone_count(sb);
	printf("\n sb->zone_count_metadata: %d ", sb->zone_count_metadata);
	sb->zone_count_cache = get_cache_zone_count(sb);
	printf("\n sb->zone_count_cache: %d", sb->zone_count_cache);
	sb->zone_count_data = get_data_zone_count(sb);
	printf("\n sb->zone_count_data: %d", sb->zone_count_data);
	sb->czone0_pba = get_current_frontier(sb);
	printf("\n sb->czone0_pba: %d", sb->czone0_pba);
	sb->dzone0_pba = get_data_zone_pba(sb);
	printf("\n sb->dzone0_pba: %d", sb->dzone0_pba);

	sb->crc = 0;
	//sb->crc = crc32(-1, (unsigned char *)sb, STL_SB_SIZE);
	ret = write_to_disk(fd, (char *)sb, 0); 
	if (ret < 0) {
		printf("\n Could not write SB to the disk \n");
		exit(-1);
	}
	ret = write_to_disk(fd, (char *)sb, 8); 
	if (ret < 0) {
		printf("\n Could not write SB to the disk \n");
		exit(-1);
	}
	return sb;
}

void set_bitmap(char *bitmap, unsigned int nrzones, char ch)
{
	int nrbytes = nrzones / 8;
	if (nrzones % 8 > 0)
		nrbytes = nrbytes + 1;
	memset(bitmap, ch, nrbytes);
}



unsigned long long get_user_block_count(struct lsdm_sb *sb)
{
	unsigned long sit_end_pba = sb->sit_pba + sb->blk_count_sit * NR_SECTORS_IN_BLK;
	unsigned long sit_zone_nr = sit_end_pba >> sb->log_zone_size;
	unsigned int nr_blks_per_zone = 1 << (sb->log_zone_size - sb->log_block_size);
	return ((sb->zone_count - sit_zone_nr) * (nr_blks_per_zone));

}

void prepare_cur_seg_entry(struct lsdm_seg_entry *entry)
{
	/* Initially no block has any valid data */
	entry->vblocks = 0;
	entry->mtime = 0;
}


void prepare_prev_seg_entry(struct lsdm_seg_entry *entry)
{
	entry->vblocks = 0;
	entry->mtime = 0;
}

struct lsdm_ckpt * read_ckpt(int fd, struct lsdm_sb * sb, unsigned long ckpt_pba)
{
	struct lsdm_ckpt *ckpt;
	u64 offset = ckpt_pba * SECTOR_SIZE;
	int ret;

	ckpt = (struct lsdm_ckpt *)malloc(BLK_SZ);
	if(!ckpt)
		exit(-1);

	memset(ckpt, 0, BLK_SZ);
	printf("\n Verifying ckpt contents !!");
	printf("\n Reading from sector: %ld", ckpt_pba);

	ret = lseek64(fd, offset, SEEK_SET);
	if (ret < 0) {
		perror("in read_ckpt lseek64 failed: ");
		printf("\n");
		exit(-1);
	}

	ret = read(fd, ckpt, BLK_SZ);
	if (ret < 0) {
		printf("\n read from disk offset: %u, sectornr: %d ret: %d", offset, ckpt_pba, ret);
		perror("\n Could not read from disk because: ");
		printf("\n");
		exit(errno);
	}
	printf("\n Read checkpoint, ckpt->magic: %d ckpt->hot_frontier_pba: %lld", ckpt->magic, ckpt->hot_frontier_pba);
	if (ckpt->magic != STL_CKPT_MAGIC) {
		free(ckpt);
		printf("\n");
		exit(-1);
	}
	printf("\n hot_frontier_pba: %llu" , ckpt->hot_frontier_pba);
	return ckpt;
}

void write_ckpt(int fd, struct lsdm_sb * sb, unsigned long ckpt_pba)
{
	struct lsdm_ckpt *ckpt, *ckpt1;
	char buffer[BLK_SZ];
	int ret;

	ckpt = (struct lsdm_ckpt *)malloc(BLK_SZ);
	if(!ckpt)
		exit(-1);

	memset(ckpt, 0, BLK_SZ);
	ckpt->magic = STL_CKPT_MAGIC;
	ckpt->version = 0;
	ckpt->user_block_count = sb->zone_count_data << (sb->log_zone_size - sb->log_block_size);
	ckpt->nr_invalid_zones = 0;
	ckpt->hot_frontier_pba = sb->czone0_pba;
	ckpt->nr_free_cache_zones = sb->zone_count_cache - 1; /* one frontier */
	ckpt->elapsed_time = 0;
	ckpt->clean = 1;  /* 1 indicates clean datastructures */
	ckpt->crc = 0;
	printf("\n-----------------------------------------------------------\n");
	printf("\n checkpoint written at: %llu cur_frontier_pba: %lld", ckpt_pba, ckpt->hot_frontier_pba);

	u64 offset = sb->ckpt1_pba * SECTOR_SIZE;

	ret = lseek64(fd, offset, SEEK_SET);
	if (ret < 0) {
		printf("\n write to disk offset: %u, sectornr: %d ret: %d", offset, ckpt_pba, ret);
		perror("!! (before write) Error in lseek64: \n");
		printf("\n");
		exit(errno);
	}
	memset(buffer, 0, BLK_SZ);
	memcpy(buffer, ckpt, sizeof(struct lsdm_ckpt));
	ret = write(fd, buffer, BLK_SZ);
	if (ret < 0) {
		perror("Error while writing ckpt1: ");
		printf("\n");
		exit(errno);
	}

	offset = sb->ckpt2_pba * SECTOR_SIZE;
	ret = lseek64(fd, offset, SEEK_SET);
	if (ret < 0) {
		printf("\n write to disk offset: %u, sectornr: %d ret: %d", offset, ckpt_pba, ret);
		perror("!! (before write) Error in lseek64: \n");
		printf("\n");
		exit(errno);
	}
	ret = write(fd, ckpt, BLK_SZ);
	if (ret < 0) {
		perror("Error while writing ckpt2: ");
		printf("\n");
		exit(errno);
	}

	printf("\n 1) ckpt->nr_free_cache_zones: %llu", ckpt->nr_free_cache_zones);
	printf("\n Checkpoint written at pba: %llu", ckpt_pba);
	printf("\n ckpt->magic: %d ckpt->hot_frontier_pba: %lld", ckpt->magic, ckpt->hot_frontier_pba);
	printf("\n sb->dzone0_pba: %llu", sb->dzone0_pba);
	printf("\n sb->czone0_pba: %llu", sb->czone0_pba);

	ckpt1 = (struct lsdm_ckpt *)malloc(BLK_SZ);
	if(!ckpt1)
		exit(-1);

	ret = lseek64(fd, offset, SEEK_SET);
	if (ret < 0) {
		printf("\n write to disk offset: %u, sectornr: %d ret: %d", offset, ckpt_pba, ret);
		perror("!! (before write) Error in lseek64: \n");
		printf("\n");
		exit(errno);
	}
	ret = read(fd, ckpt1, BLK_SZ);
	if (ret < 0) {
		perror("Error while writing: ");
		printf("\n");
		exit(errno);
	}

	printf("\n Read ckpt, ckpt->magic: %d", ckpt1->magic);
	printf("\n hot_frontier_pba: %llu" , ckpt1->hot_frontier_pba);
	printf("\n ckpt->nr_free_cache_zones: %llu", ckpt1->nr_free_cache_zones);
	printf("\n-----------------------------------------------------------\n");

	if (memcmp(ckpt, ckpt1, sizeof(struct lsdm_ckpt))) {
		printf("\n checkpoint written and read is different!! ");
		if (ckpt->magic != ckpt1->magic) {
			printf("\n MAGIC mismatch!");
		} 
		if (ckpt->hot_frontier_pba != ckpt1->hot_frontier_pba) {
			printf("\n frontier mismatch!");
		}
		if (ckpt->nr_free_cache_zones != ckpt1->nr_free_cache_zones) {
			printf("\n nr_free_cache_zones mismatch!");

		}
		printf("\n");
		exit(-1);
	}
	free(ckpt);
	free(ckpt1);
}


void write_dzone_info_table(int fd, u64 dzit_blk_count, u32 dzit_pba)
{
	write_zeroed_blks(fd, dzit_pba, dzit_blk_count);
}

void write_seg_info_table(int fd, u64 nr_seg_entries, unsigned long seg_entries_pba)
{
	unsigned entries_in_blk = BLK_SZ / sizeof(struct lsdm_seg_entry);
	unsigned int nr_sit_blks = (nr_seg_entries)/entries_in_blk;

	if (nr_seg_entries % entries_in_blk > 0)
		nr_sit_blks = nr_sit_blks + 1;

	printf("\n writing seg info tables at pba: %llu", seg_entries_pba);
	printf("\n zone_count: %d ", nr_seg_entries);
	printf("\n nr of sit blks: %d", nr_sit_blks);
	printf("\n");

	write_zeroed_blks(fd, seg_entries_pba, nr_sit_blks);
}


void read_seg_info_table(int fd, u64 nr_seg_entries, unsigned long seg_entries_pba)
{
	unsigned entries_in_blk = BLK_SZ / sizeof(struct lsdm_seg_entry);
	unsigned int nr_sit_blks = (nr_seg_entries)/entries_in_blk;

	if (nr_seg_entries % entries_in_blk > 0)
		nr_sit_blks = nr_sit_blks + 1;

	read_block(fd, seg_entries_pba, nr_sit_blks);
}


void report_zone(unsigned int fd, unsigned long zonenr, struct blk_zone * bzone)
{
	int ret;
	long i = 0;
	struct blk_zone_report *bzr;


	printf("\n-----------------------------------");
	printf("\n %s Zonenr: %ld ", __func__, zonenr);
	bzr = malloc(sizeof(struct blk_zone_report) + sizeof(struct blk_zone));
	bzr->sector = bzone->start;
	bzr->nr_zones = 1;

	ret = ioctl(fd, BLKREPORTZONE, bzr);
	if (ret) {
		fprintf(stderr, "\n blkreportzone for zonenr: %d ioctl failed, ret: %d ", zonenr, ret);
		perror("\n blkreportzone failed because: ");
		printf("\n");
		return;
	}
	assert(bzr->nr_zones == 1);
	printf("\n start: %ld ", bzr->zones[0].start);
	printf("\n len: %ld ", bzr->zones[0].len);
	printf("\n state: %d ", bzr->zones[0].cond);
	printf("\n reset recommendation: %d ", bzr->zones[0].reset);
	assert(bzr->zones[0].reset == 0);
	printf("\n wp: %llu ", bzr->zones[0].wp);
	assert(bzr->zones[0].wp == bzr->zones[0].start);
	printf("\n non_seq: %d ", bzr->zones[0].non_seq);
	printf("\n-----------------------------------\n"); 
	return;	
}

long reset_shingled_zones(int fd)
{
	int ret;
	long i = 0;
	long zone_count = 0;
	struct blk_zone_report * bzr;
	struct blk_zone_range bz_range;
	long cmr = 0;


	zone_count = get_zone_count(fd);

	printf("\n Nr of zones: %d ", zone_count);

	bzr = malloc(sizeof(struct blk_zone_report) + sizeof(struct blk_zone) * zone_count);

	bzr->sector = 0;
	bzr->nr_zones = zone_count;

	ret = ioctl(fd, BLKREPORTZONE, bzr);
	if (ret) {
		fprintf(stdout, "\n blkreportzone for zonenr: %d ioctl failed, ret: %d ", 0, ret);
		perror("\n blkreportzone failed because: ");
		printf("\n");
		return;
	}

	printf("\n nr of zones reported: %d", bzr->nr_zones);
	assert(bzr->nr_zones == zone_count);

	if (zone_count < 1024) {
		bzr->nr_zones = zone_count;
	} else {
		bzr->nr_zones = 1024;
	}

	printf("\n zone_count: %d ", bzr->nr_zones);
	for (i=0; i<bzr->nr_zones; i++) {
		if ((bzr->zones[i].type == BLK_ZONE_TYPE_SEQWRITE_PREF)  || 
		   (bzr->zones[i].type == BLK_ZONE_TYPE_SEQWRITE_REQ)) {
			bz_range.sector = bzr->zones[i].start;
			bz_range.nr_sectors = bzr->zones[i].len;
			ret = ioctl(fd, BLKRESETZONE, &bz_range); 
			if (ret) {
				fprintf(stdout, "\n Could not reset zonenr with sector: %ld", bz_range.sector);
				perror("\n blkresetzone failed because: ");
				printf("\n");
			}
			//report_zone(fd, i, &bzr->zones[i]);
		} else {
			printf("\n zonenr: %d is a non shingled zone! ", i);
			cmr++;
		}
	}
	return cmr;
}

/*
 *
 * SB1, SB2, Revmap, Translation Map, Revmap Bitmap, CKPT1, CKPT2, SIT, Dataa
 *
 */

int main(int argc, char * argv[])
{
	unsigned int pba = 0;
	struct lsdm_sb *sb1;
	char cmd[256];
	unsigned long nrblks;
	unsigned int ret = 0;
	long cmr;
	char * blkdev;
	int fd;

	printf("\n %s argc: %d \n ", __func__, argc);
	if (argc != 2) {
		fprintf(stderr, "\n Usage: %s device-name \n", argv[0]);
		exit(EXIT_FAILURE);
	}
	blkdev = argv[1];
	fd = open_disk(blkdev);
	cmr = reset_shingled_zones(fd);
	printf("\n Number of cmr zones: %d ", cmr);
	sb1 = write_sb(fd, 0, cmr);
	printf("\n Superblock written at pba: %d", pba);
	printf("\n sizeof sb: %ld", sizeof(struct lsdm_sb));
	read_sb(fd, 0);
	read_sb(fd, 8);
	printf("\n Superblock written at pba: %d", pba + NR_SECTORS_IN_BLK);
	write_ckpt(fd, sb1, sb1->ckpt1_pba);
	printf("\n Checkpoint written at offset: %d", sb1->ckpt1_pba);
	printf("\n Second Checkpoint written at offset: %d", sb1->ckpt2_pba);
	write_dzone_info_table(fd, sb1->blk_count_dzit, sb1->dzit_pba);
	printf("\n Data zone information written ");
	write_seg_info_table(fd, sb1->zone_count_cache, sb1->sit_pba);
	read_seg_info_table(fd, sb1->zone_count_cache, sb1->sit_pba);
	printf("\n Segment Information Table written");
	write_rtm(fd, sb1->rtm_pba, sb1->blk_count_rtm);
	printf("\n Reverse Translation map written");
	nrblks = get_nr_blks(sb1);
	printf("\n Total nr of blks in disk: %lu", nrblks);
	printf("\n sb1->zone_count: %d", sb1->zone_count);
	pba = 0;
	free(sb1);
	/* 0 volume_size: 39321600  lsdm  blkdev: /dev/vdb tgtname: TL1 zone_lbas: 524288 data_end: 41418752 */
	unsigned long zone_lbas = 0;
	char str[SECTOR_SIZE];

	/*
	if (ioctl(fd, BLKGETZONESZ, &zone_lbas) < 0) {
		sprintf(str, "Get zone size failed %d (%s)\n", errno, strerror(errno));
		perror(str);
		printf("\n");
		exit(errno);
	}*/

	zone_lbas = 524288;
	printf("\n # lbas in ZONE: %llu", zone_lbas);
	close(fd);
	unsigned long data_zones = sb1->zone_count_data;
	printf("\n sb1->zone_count_main: %d ", data_zones);
	char * tgtname = "TL1";
	//volume_size = data_zones * zone_lbas;
	unsigned long volume_size = data_zones * zone_lbas;
	unsigned long data_end = volume_size;
	sprintf(cmd, "/sbin/dmsetup create TL1 --table '0 %ld hybrid-stl %s %s %ld %ld'",
            volume_size, blkdev, tgtname, zone_lbas, 
	    data_end);
	printf("\n cmd: %s", cmd);
    	//system(cmd);
	printf("\n \n");
	return(0);
}
