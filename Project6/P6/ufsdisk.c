/* Author: Adem Efe Gencer, November 2015.
 *
 * This code implements a set of virtualized block store on top of another block store.
 * Each virtualized block store is identified by a so-called "inode number", which indexes
 * into an array of inodes.. The interface is as follows:
 *
 *     void ufsdisk_create(block_if below, unsigned int n_inodes, unsigned int magic_number)
 *         Initializes the underlying block store "below" with a file system.
 *         The file system consists of one "superblock", a number inodes, and
 *         the remaining blocks explained below.
 *
 *     block_if ufsdisk_init(block_if below, unsigned int inode_no)
 *         Opens a virtual block store at the given inode number.
 *
 * The layout of the file system is described in the file "ufsdisk.h".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "block_if.h"
#include "ufsdisk.h"

#define NUM_BITS_IN_BYTES				8	/* 8 bits in a byte */
#define NUM_DIRECT_BLOCKS				REFS_PER_INODE - 3 /* # of blocks by direct pointers */
#define NUM_SINGLE_INDIRECT_BLOCKS		NUM_DIRECT_BLOCKS + REFS_PER_BLOCK /* # of blocks by the single indirect pointer*/	
#define NUM_DOUBLE_INDIRECT_BLOCKS		NUM_SINGLE_INDIRECT_BLOCKS + (REFS_PER_BLOCK * REFS_PER_BLOCK) /* # of blocks by the double indirect pointer */	
#define NUM_TRIPLE_INDIRECT_BLOCKS		NUM_DOUBLE_INDIRECT_BLOCKS + (REFS_PER_BLOCK * REFS_PER_BLOCK * REFS_PER_BLOCK) /* # of blocks by the triple pointer */	
#define SINGLE_INDIRECT_BLOCK_INDEX		REFS_PER_INODE - 3		
#define DOUBLE_INDIRECT_BLOCK_INDEX		REFS_PER_INODE - 2		
#define TRIPLE_INDIRECT_BLOCK_INDEX		REFS_PER_INODE - 1	
 // capacity (# of blocks) of an indirect pointer at each level
const int NUM_BLOCKS_LEVEL[3] = { 1, REFS_PER_BLOCK, REFS_PER_BLOCK*REFS_PER_BLOCK };


 /* Temporary information about the file system and a particular inode.
 * Convenient for all operations.
 */
struct ufs_snapshot {
	union ufs_block superblock;
	union ufs_block inodeblock;
	block_no inode_blockno;
	struct ufs_inode *inode;
};

/* The state of a virtual block store, which is identified by an inode number.
*/
struct ufs_state {
	block_if below;			// block store below
	unsigned int inode_no;	// inode number in file system
};

static block_t null_block;			// a block filled with null bytes

/* Get a snapshot of the file system, including the superblock and the block
* containing the inode.
*/
static int ufs_get_snapshot(struct ufs_snapshot *snapshot,
	block_if below, unsigned int inode_no) {

	/* Get the superblock.
	*/
	if ((*below->read)(below, 0, (block_t *)&snapshot->superblock) < 0) {
		return -1;
	}

	/* Check the inode number.
	*/
	if (inode_no >= snapshot->superblock.superblock.n_inodeblocks * INODES_PER_BLOCK) {
		fprintf(stderr, "!!TDERR: inode number too large %u %u\n", inode_no, snapshot->superblock.superblock.n_inodeblocks);
		return -1;
	}

	/* Find the inode.
	*/
	snapshot->inode_blockno = 1 + inode_no / INODES_PER_BLOCK;
	if ((*below->read)(below, snapshot->inode_blockno, (block_t *)&snapshot->inodeblock) < 0) {
		return -1;
	}
	snapshot->inode = &snapshot->inodeblock.inodeblock.inodes[inode_no % INODES_PER_BLOCK];
	return 0;
}

/* Retrieve the number of blocks in the file referenced by 'bi'. This information
 * is maintained in the inode itself.
 */
static int ufsdisk_nblocks(block_if bi){	
	//validate inputs
	if (bi == NULL)
	{
		fprintf(stderr, "!!TDERR: Invalid input in ufsdisk_nblocks\n");
		return -1;
	}
	struct ufs_state *us = bi->state;

	struct ufs_snapshot snapshot;
	if (ufs_get_snapshot(&snapshot, us->below, us->inode_no) < 0) {
		return -1;
	}
	return snapshot.inode->nblocks;
}

/* Set the size of the file 'bi' to 'nblocks'.
 */
static int ufsdisk_setsize(block_if bi, block_no nblocks){
	// TODO.
}

/* Read a block at the given block number 'offset' and return in *block.
 */
static int ufsdisk_read(block_if bi, block_no offset, block_t *block){
	//validate inputs
	if (bi == NULL || offset < 0 || block == NULL) {
		fprintf(stderr, "!!TDERR: Invalid inputs in ufsdisk_read\n");
		return -1;
	}

	struct ufs_state *us = bi->state;
	block_if below = us->below;
	block_no blockToRead;
	int nlevels = -1; //number of levels of the tree we need to traverse

	/* Get info from underlying file system.
	*/
	struct ufs_snapshot snapshot;
	if (ufs_get_snapshot(&snapshot, us->below, us->inode_no) < 0) {
		return -1;
	}

	/* See if the offset is too big.
	*/
	if (offset >= snapshot.inode->nblocks) {
		fprintf(stderr, "!!TDERR: offset too large\n");
		return -1;
	}

	if (offset < NUM_DIRECT_BLOCKS) { //the block being read is pointed to be a direct pointer
		//get the block no to read
		blockToRead = snapshot.inode->refs[offset];
		nlevels = 0;
	} else if (offset < NUM_SINGLE_INDIRECT_BLOCKS) { //block being read is pointed to by the single indirect block
		//get the indirect block no to read
		blockToRead = snapshot.inode->refs[SINGLE_INDIRECT_BLOCK_INDEX];
		nlevels = 1;
		offset -= NUM_DIRECT_BLOCKS;
	} else if (offset < NUM_DOUBLE_INDIRECT_BLOCKS) { //block being read is pointed to by the double indirect block
		//get the indirect block no to read
		blockToRead = snapshot.inode->refs[DOUBLE_INDIRECT_BLOCK_INDEX];
		nlevels = 2;
		offset -= NUM_SINGLE_INDIRECT_BLOCKS;
	} else { //block being read is pointed to by the triple indirect block
		//get the indirect block no to read
		blockToRead = snapshot.inode->refs[TRIPLE_INDIRECT_BLOCK_INDEX];
		nlevels = 3;
		offset -= NUM_DOUBLE_INDIRECT_BLOCKS;
	}

	for (;;) {
		/* If there's a hole, return the null block.
		*/
		if (blockToRead == 0) {
			memset(block, 0, BLOCK_SIZE);
			return 0;
		}

		if ((*below->read)(below, blockToRead, block) < 0) {
			fprintf(stderr, "!!TDERR: Failure to read data block\n");
			return -1;
		}

		//if we're on the last level, we're done
		if (nlevels == 0) break;

		/* The block is an indirect block.  Figure out the index into this
		* block and get the block number.
		*/
		nlevels--;
		struct ufs_indirblock *tib = (struct ufs_indirblock *) block;
		if (nlevels == 0) blockToRead = tib->refs[offset];
		else {
			blockToRead = tib->refs[offset / NUM_BLOCKS_LEVEL[nlevels]];
			offset = offset % NUM_BLOCKS_LEVEL[nlevels];
		}
	}

	return 0;
}

/* Write *block at the given block number 'offset'.
 */
static int ufsdisk_write(block_if bi, block_no offset, block_t *block){
	// TODO.
}

static void ufsdisk_destroy(block_if bi){
	free(bi->state);
	free(bi);
}

/* Create or open a new virtual block store at the given inode number.
 */
block_if ufsdisk_init(block_if below, unsigned int inode_no){
	//validate inputs
	if (below == NULL || inode_no < 0)
	{
		fprintf(stderr, "!!TDERR: Invalid inputs in ufsdisk_init\n");
		return NULL;
	}

	/* Get info from underlying file system.
	*/
	struct ufs_snapshot snapshot;
	if (ufs_get_snapshot(&snapshot, below, inode_no) < 0) {
		return 0;
	}

	/* Create the block store state structure.
	*/
	struct ufs_state *us = calloc(1, sizeof(*us));
	us->below = below;
	us->inode_no = inode_no;

	/* Return a block interface to this inode.
	*/
	block_if bi = calloc(1, sizeof(*bi));
	bi->state = us;
	bi->nblocks = ufsdisk_nblocks;
	bi->setsize = ufsdisk_setsize;
	bi->read = ufsdisk_read;
	bi->write = ufsdisk_write;
	bi->destroy = ufsdisk_destroy;
	return bi;
}

/*************************************************************************
 * The code below is for creating new ufs-like file systems.  This should
 * only be invoked once per underlying block store.
 ************************************************************************/

/* Create the freebitmap blocks adjacent to ufs_inodeblocks, and return the number of 
 * freebitmap blocks created.
 *
 * The number of ufs_freebitmap blocks (f) can be estimated using:
 *
 * f <= K / (1 + BLOCK_SIZE * 2^3), where K is as follows:
 * K = nblocks - 1 - ceil(n_inodes/INODES_PER_BLOCK)
 */
block_no setup_freebitmapblocks(block_if below, block_no next_free, block_no nblocks){
	//estimate the num of free bit blocks we need
	unsigned int n_inodeblocks = next_free - 1; //the num of inode blocks
	unsigned int K = nblocks - 1 - n_inodeblocks;
	block_no n_freebitmapblocks = (block_no)ceil(K / (1 + BLOCK_SIZE * NUM_BITS_IN_BYTES)); // estimated # freebitmap blocks

	block_no n_remaining_blocks = K - n_freebitmapblocks;
	block_no leftoverblocks = n_remaining_blocks % (BLOCK_SIZE * NUM_BITS_IN_BYTES); //num blocks that will use a partial bitmap block
	block_no n_fullfreebitmapblocks = (leftoverblocks == 0) ? n_freebitmapblocks : n_freebitmapblocks - 1; // # bitmap blocks where each bit corresponds to a free block
	block_no index_lastfullbitmapblock = next_free + n_fullfreebitmapblocks; //index to our last free bitmap block that will be full

	int k;
	for (k = 0; k < n_fullfreebitmapblocks; k++) //set all blocks for full free bitmap blocks
	{
		if ((*below->write)(below, next_free + k, (block_t *)&null_block) < 0) {
			panic("ufs_setup_freebitmapblocks");
		}
	}

	//if we have any leftover blocks, set bits associated with block to 0 and remaining bits to 1 to indicate it is unavailable
	if (leftoverblocks > 0)
	{
		union ufs_block freebitmapblock;
		memset(&freebitmapblock, 0xFF, BLOCK_SIZE); //set all block bits to 1

		int fullBytes = leftoverblocks / NUM_BITS_IN_BYTES; //num of bytes to fully set to 0
		int leftoverBits = leftoverblocks % NUM_BITS_IN_BYTES; //num of leftover bits to set to 0

		for (k = 0; k < fullBytes; k++)
			freebitmapblock.freebitmapblock.status[k] = 0;

		if (leftoverBits != 0) { //set partial byte's first leftoverBits to 0 and remaining bits to 1
			unsigned char uChar = 0xFF;
			uChar >>= leftoverBits;
			freebitmapblock.freebitmapblock.status[fullBytes] = (char) uChar;
		}

		if ((*below->write)(below, next_free + n_fullfreebitmapblocks, (block_t *)&freebitmapblock) < 0) {
			panic("ufs_setup_freebitmapblocks");
		}
	}

	return n_freebitmapblocks;
}

/* Create a new file system on the block store below.
 */
int ufsdisk_create(block_if below, unsigned int n_inodes, unsigned int magic_number){
	if (sizeof(union ufs_block) != BLOCK_SIZE) {
		panic("ufsdisk_create: block has wrong size");
	}

	unsigned int n_inodeblocks =
					(n_inodes + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK;
	int nblocks = (*below->nblocks)(below);
	if (nblocks < n_inodeblocks + 2) {
		fprintf(stderr, "ufsdisk_create: too few blocks\n");
		return -1;
	}

	/* Initialize the superblock.
	 */
	union ufs_block superblock;
	memset(&superblock, 0, BLOCK_SIZE);
	superblock.superblock.magic_number = magic_number;
	superblock.superblock.n_inodeblocks = n_inodeblocks;
	superblock.superblock.n_freebitmapblocks =
				setup_freebitmapblocks(below, n_inodeblocks + 1, nblocks);
	if ((*below->write)(below, 0, (block_t *) &superblock) < 0) {
		return -1;
	}

	/* The inodes all start out empty.
	 */
	int i;
	for (i = 1; i <= n_inodeblocks; i++) {
		if ((*below->write)(below, i, &null_block) < 0) {
			return -1;
		}
	}

	return 0;
}
