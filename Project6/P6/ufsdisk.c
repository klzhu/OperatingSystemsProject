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
#include <stdbool.h>
#include "block_if.h"
#include "ufsdisk.h"

const int NUM_BITS_IN_BYTES = 8;	// # of bits in a byte
const int MAX_LEVEL = 4;
const int NUM_BLOCKS_IN_LEVEL[4] = { REFS_PER_INODE - 3, REFS_PER_BLOCK, REFS_PER_BLOCK*REFS_PER_BLOCK, 
				REFS_PER_BLOCK * REFS_PER_BLOCK * REFS_PER_BLOCK };  // capacity (# of blocks) at each level
const int INDIRECT_INDEX[4] = {0, REFS_PER_INODE - 3, REFS_PER_INODE - 2, REFS_PER_INODE - 1 }; // (starting) index of direct and 1-3 indir levels

static block_t null_block;			// a block filled with null bytes

/* The state of a virtual block store, which is identified by an inode number.
*/
struct ufs_state {
	block_if below;		// block store below
	union ufs_block superblock;
	union ufs_block inodeblock;	// the inodeblock that contains the inode
	block_no inode_blockno;		// the index of the inodeblock 
	struct ufs_inode *inode;	// It points to the inode
};

/* Retrieve the number of blocks in the file referenced by 'bi'. This information
 * is maintained in the inode itself.
 */
static int ufsdisk_nblocks(block_if bi){	
	//validate inputs
	if (bi == NULL)
	{
		fprintf(stderr, "!!UFSERR: Invalid input in ufsdisk_nblocks\n");
		return -1;
	}
	struct ufs_state *us = bi->state;
	return us->inode->nblocks;
}

// It allocates a block with its block index in allocatedBlockIndex. 
// It return 0 if successful or -1 is failed.
static int ufsdisk_alloc_block(block_if bi, block_no *allocatedBlockIndex)
{
	int i, k, byteIdx;
	struct ufs_state *us = bi->state;
	
	// initialize to the index of the first remaining block
	*allocatedBlockIndex = 1 + us->superblock.superblock.n_inodeblocks + us->superblock.superblock.n_freebitmapblocks; 

	// Read the freelist block and scan for a free block.
	block_no firstBitmapIndex = 1 + us->superblock.superblock.n_inodeblocks; 
	for (k = 0; k < us->superblock.superblock.n_freebitmapblocks; k++) {
		union ufs_block freebitmapblock;
		if ((*us->below->read)(us->below, k + firstBitmapIndex, (block_t *)&freebitmapblock) < 0) {
			panic("ufsdisk_alloc_block()");
		}

		for (byteIdx = 0; byteIdx < BLOCK_SIZE; byteIdx++) {
			unsigned char currBit = 0x8F; // initialize the byte with the leftest bit to be 1
			unsigned char currByte = freebitmapblock.freebitmapblock.status[byteIdx]; // get the current byte
			for (i = 0; i < NUM_BITS_IN_BYTES; i++) { // check each bit of the current byte
				if ((currBit & currByte) == 0) { // the block associated with the bit is available 
					currByte |= currBit; // set the bit to 1 to indicate its block is assigned
					freebitmapblock.freebitmapblock.status[byteIdx] = currByte;
					if ((*us->below->write)(us->below, k + firstBitmapIndex, (block_t *)&freebitmapblock) < 0) {
						panic("ufsdisk_alloc_block()");
					}

					return 0;
				} else {
					currBit >>= 1;	// right shift the current bit by 1
					(*allocatedBlockIndex)++;	// increase the block index by 1
				}
			}
		}
	}

	return -1;	// no free block available
}

// It frees the block at blockIndex, and returns 0 or -1 if successful or failure
static int ufsdisk_free_block(block_if bi, block_no blockIndex)
{
	struct ufs_state *us = bi->state;

	block_no firstBlockIdx = 1 + us->superblock.superblock.n_inodeblocks + us->superblock.superblock.n_freebitmapblocks; // index of first remaining block
	if (blockIndex < firstBlockIdx)  return -1; // error if the block index is not a remaining block

	//figure out the freebitmap block where the block's bit is stored at, and the offset of the block index to the bitmap block
	block_no freebitmapBlockIndex = (blockIndex - firstBlockIdx) / (BLOCK_SIZE * NUM_BITS_IN_BYTES);	// index relative to the first bitmap block for now
	block_no offset = (blockIndex - firstBlockIdx) % (BLOCK_SIZE * NUM_BITS_IN_BYTES); // the block's index offset to the beginning of its bit map block
	if (freebitmapBlockIndex >= us->superblock.superblock.n_freebitmapblocks) return -1; // error if the bitmap block index is too large

	freebitmapBlockIndex += us->superblock.superblock.n_inodeblocks + 1; // now it is the absolate index 

	//retrieve the block from memory, reset the bit for our block, write the block back
	union ufs_block freebitmapblock;
	if ((*us->below->read)(us->below, freebitmapBlockIndex, (block_t *)&freebitmapblock) < 0) {
		fprintf(stderr, "!!UFSERR: Failure to read data block in ufsdisk_updatefreebits.\n");
		return -1;
	}

	block_no byteIndex = offset / NUM_BITS_IN_BYTES; //which byte in our block our block is represented in
	block_no bitToReset = offset % NUM_BITS_IN_BYTES;  //which bit in our byte our block is represented as

	freebitmapblock.freebitmapblock.status[byteIndex] &= ~(0x1 << (7 - bitToReset));

	if ((*us->below->write)(us->below, freebitmapBlockIndex, (block_t *)&freebitmapblock) < 0) {
		fprintf(stderr, "!!UFSERR: Failure to write data block in ufsdisk_updatefreebits.\n");
		return -1;
	}

	return 0;
}

// It frees all blocks references by indir block at blockIndex
// nlevels is the level of the indir block's reference
static int ufsdisk_traverseIndBlocks(block_if bi, int nlevels, block_no blockIndex)
{
	struct ufs_state *us = bi->state;

	// read in indir block
	struct ufs_indirblock indirBlock;
	if ((*us->below->read)(us->below, blockIndex, (block_t *)&indirBlock) < 0) {
		fprintf(stderr, "!!UFSERR: Failure to read data block in ufsdisk_traverseIndBlocks.\n");
		return -1;
	}

	int k;
	if (nlevels == 0) { // it points to data blocks
		for (k = 0; k < REFS_PER_BLOCK; k++) {
			if (indirBlock.refs[k] != 0) {
				if (ufsdisk_free_block(bi, indirBlock.refs[k]) < 0) {
					fprintf(stderr, "!!UFSERR: Failure to read data block in ufsdisk_traverseIndBlocks.\n");
					return -1;
				}
			}
		}
	} else { // it points to indir blocks
		for (k = 0; k < REFS_PER_BLOCK; k++) {
			if (indirBlock.refs[k] != 0) {
				if (ufsdisk_traverseIndBlocks(bi, nlevels - 1, indirBlock.refs[k]) < 0) {
					fprintf(stderr, "!!UFSERR: Failure to read data block in ufsdisk_traverseIndBlocks.\n");
					return -1;
				}
			}
		}
	}

	// free teh indir block
	if (ufsdisk_free_block(bi, blockIndex) < 0) {
		fprintf(stderr, "!!UFSERR: Failure to read data block in ufsdisk_traverseIndBlocks.\n");
		return -1;
	}

	return 0;
}

/* Set the size of the file 'bi' to 'nblocks'.
 */
static int ufsdisk_setsize(block_if bi, block_no nblocks){
	//validate inputs
	if (bi == NULL) {
		fprintf(stderr, "!!UFSERR: Invalid inputs in ufsdisk_setsize\n");
		return -1;
	}

	struct ufs_state *us = bi->state;
	if (us->inode->nblocks == nblocks) return nblocks;

	if (nblocks > 0) {
		fprintf(stderr, "!!UFSERR: nblocks > 0 not supported\n");
		return -1;
	}

	// Release all the blocks used by this inode
	block_no nblocksOrig = us->inode->nblocks;
	block_no maxIndex = nblocksOrig - 1;
	
	// figure out maxIndex's levels and offset to the begining index of the pointer's range of indexes
	int nlevels = 0;
	while (nlevels < MAX_LEVEL && maxIndex >= NUM_BLOCKS_IN_LEVEL[nlevels]) {
		maxIndex -= NUM_BLOCKS_IN_LEVEL[nlevels];
		nlevels++;
	}
	if (maxIndex >= NUM_BLOCKS_IN_LEVEL[nlevels]) {
		fprintf(stderr, "!!UFSERR: inode's nblocks has overflowed in ufsdisk_setsize.\n");
		return -1;
	}

	block_no pointerIndex = (nlevels == 0) ? maxIndex : INDIRECT_INDEX[nlevels];
	if (nlevels > 0) maxIndex = NUM_BLOCKS_IN_LEVEL[0] - 1; // max index for direct references

	while (pointerIndex >= INDIRECT_INDEX[1]) { // when it is an indirect pointer
		if (us->inode->refs[pointerIndex] != 0) {
			if (ufsdisk_traverseIndBlocks(bi, nlevels - 1, us->inode->refs[pointerIndex]) < 0) {
				fprintf(stderr, "!!UFSERR: Failure to free an indir block in ufsdisk_setsize.\n");
				return -1;
			}

			us->inode->refs[pointerIndex] = 0;
		}

		pointerIndex--;
	}

	// for nlevels == 0
	block_no k;
	for (k = 0; k <= maxIndex; k++) {
		if (us->inode->refs[k] != 0) {
			if (ufsdisk_free_block(bi, us->inode->refs[k]) < 0) {
				fprintf(stderr, "!!UFSERR: Failure to free a block in ufsdisk_setsize\n");
				return -1;
			}

			us->inode->refs[k] = 0;
		}
	}
	
	//set size to 0, write back t disk
	us->inode->nblocks = nblocks;
	if ((*us->below->write)(us->below, us->inode_blockno, (block_t *)&us->inodeblock) < 0) {
		panic("ufsdisk_setsize");
	}

	return nblocksOrig;
}

/* Read a block at the given block number 'offset' and return in *block.
 */
static int ufsdisk_read(block_if bi, block_no offset, block_t *block){
	//validate inputs
	if (bi == NULL || block == NULL) {
		fprintf(stderr, "!!UFSERR: Invalid inputs in ufsdisk_read\n");
		return -1;
	}

	struct ufs_state *us = bi->state;

	// See if the offset is too big.
	if (offset >= us->inode->nblocks) {
		fprintf(stderr, "!!UFSERR: offset too large\n");
		return -1;
	}

	// Figure out indirection levels, first-level block index, and make offset relative to the beginning of its indirection level
	int nlevels = 0; 
	while (nlevels < MAX_LEVEL && offset >= NUM_BLOCKS_IN_LEVEL[nlevels]) {
		offset -= NUM_BLOCKS_IN_LEVEL[nlevels];
		nlevels++;
	}
	if (offset >= NUM_BLOCKS_IN_LEVEL[nlevels]) {
		fprintf(stderr, "!!UFSERR: inode's nblocks has overflowed in ufsdisk_read.\n");
		return -1;
	}

	block_no blockToRead = us->inode->refs[(nlevels == 0) ? offset : INDIRECT_INDEX[nlevels]];
	while (true) {
		// If there's a hole, return the null block.
		if (blockToRead == 0) {
			memset(block, 0, BLOCK_SIZE);
			break;
		}

		if ((*us->below->read)(us->below, blockToRead, block) < 0) {
			fprintf(stderr, "!!UFSERR: Failure to read data block\n");
			return -1;
		} 
			
		//if we're on the last level, we're done
		if (nlevels == 0) break;

		nlevels--;
		struct ufs_indirblock *tib = (struct ufs_indirblock *) block;
		if (nlevels == 0) 
			blockToRead = tib->refs[offset];
		else {
			blockToRead = tib->refs[offset / NUM_BLOCKS_IN_LEVEL[nlevels]];
			offset = offset % NUM_BLOCKS_IN_LEVEL[nlevels];
		}
	}

	return 0;
}


/* Write *block at the given block number 'offset'.
 */
static int ufsdisk_write(block_if bi, block_no offset, block_t *block){
	//validate inputs
	if (bi == NULL || block == NULL) {
		fprintf(stderr, "!!UFSERR: Invalid inputs in ufsdisk_write.\n");
		return -1;
	}

	struct ufs_state *us = bi->state;
	bool dirtyNode = false;	// is the current inode or indir block updated?

	// if offset is beyond inode's size, expand to cover the offset
	if (offset >= us->inode->nblocks) {
		us->inode->nblocks = offset + 1;
		dirtyNode = true;
	}

	// Figure out indirection levels, index in the inode, and make offset relative to the beginning of its indirection level
	int nlevels = 0;	// # of indirection levels
	while (nlevels < MAX_LEVEL && offset >= NUM_BLOCKS_IN_LEVEL[nlevels]) {
		offset -= NUM_BLOCKS_IN_LEVEL[nlevels];
		nlevels++;
	}
	if (offset >= NUM_BLOCKS_IN_LEVEL[nlevels]) {
		fprintf(stderr, "!!UFSERR: inode's nblocks has overflowed in ufsdisk_write.\n");
		return -1;
	}

	block_no blockIdx = (nlevels == 0) ? offset : INDIRECT_INDEX[nlevels]; // index in the inode's array
	bool newIndirBlock = false; // if the next level's indir block updated?
	struct ufs_indirblock indirBlock;
	if (us->inode->refs[blockIdx] == 0) { // if the innode pointer does not point to a block, allocate a block & point to it
		if (ufsdisk_alloc_block(bi, &us->inode->refs[blockIdx]) < 0) {
			fprintf(stderr, "!!UFSERR: Failure to allocate a block in ufsdisk_write.\n");
			return -1;
		}

		dirtyNode = true;
		if (nlevels > 0) newIndirBlock = true; // the block is an indir block
	} 

	if (dirtyNode) { // If the inode block was updated, write it back now.
		if ((*us->below->write)(us->below, us->inode_blockno, (block_t *)&us->inodeblock) < 0) {
			fprintf(stderr, "!!UFSERR: Failure to write a block in ufsdisk_write.\n");
			return -1;
		}
	}

	dirtyNode = false;
	blockIdx = us->inode->refs[blockIdx]; // the index of the current block
	if (nlevels > 0) { // the block is an indir block
		if (newIndirBlock) {
			memset(&indirBlock, 0, BLOCK_SIZE);	// reset new indir block
			dirtyNode = true;
		} else { // read in the next indir block
			if ((*us->below->read)(us->below, blockIdx, (block_t *)&indirBlock) < 0) {
				fprintf(stderr, "!!UFSERR: Failure to read a block in ufsdisk_write.\n");
				return -1;
			}
		}
	}

	if (newIndirBlock) { // if the current block is a new indir block
		memset(&indirBlock, 0, BLOCK_SIZE);	// reset new indir block
		dirtyNode = true;
	}

	while (true) {
		if (nlevels == 0) { // write to the current block
			if ((*us->below->write)(us->below, blockIdx, block) < 0) {
				fprintf(stderr, "!!UFSERR: Failure to write data block in ufsdisk_write.\n");
				return -1;
			}

			break;
		} 
		
		// if not break yet, go to next level
		nlevels--;

		// find the index in the indir block and update offset to be relative to the next indir block
		int blockOffset;	
		if (nlevels == 0)
			blockOffset = offset;
		else {
			blockOffset = offset / NUM_BLOCKS_IN_LEVEL[nlevels];
			offset = offset % NUM_BLOCKS_IN_LEVEL[nlevels];
		}

		newIndirBlock = false;
		if (indirBlock.refs[blockOffset] == 0) {
			if (ufsdisk_alloc_block(bi, &indirBlock.refs[blockOffset]) < 0) { // allocate a block
				fprintf(stderr, "!!UFSERR: Failure to allocate a block in ufsdisk_write.\n");
				return -1;
			}

			dirtyNode = true;
			if (nlevels > 0) newIndirBlock = true; // the block is an indir block
		} 

		if (dirtyNode) { // If the indir block was updated, write it back now.
			if ((*us->below->write)(us->below, blockIdx, (block_t *)&indirBlock) < 0) {
				fprintf(stderr, "!!UFSERR: Failure to write a block in ufsdisk_write.\n");
				return -1;
			}
		}

		dirtyNode = false;
		blockIdx = indirBlock.refs[blockOffset];
		if (nlevels > 0) { // the block is an indir block
			if (newIndirBlock) {
				memset(&indirBlock, 0, BLOCK_SIZE);	// reset new indir block
				dirtyNode = true;
			} else { // read in the next indir block
				if ((*us->below->read)(us->below, blockIdx, (block_t *)&indirBlock) < 0) {
					fprintf(stderr, "!!UFSERR: Failure to read a block in ufsdisk_write.\n");
					return -1;
				}
			}
		}
	}

	return 0;
}

static void ufsdisk_destroy(block_if bi){
	free(bi->state);
	free(bi);
}

/* Create or open a new virtual block store at the given inode number.
 * It returns block_if of ufsdisk if succeeded or NULL if failed.
 */
block_if ufsdisk_init(block_if below, unsigned int inode_no){
	//validate inputs
	if (below == NULL) {
		fprintf(stderr, "!!UFSERR: Invalid inputs in ufsdisk_init()\n");
		return NULL;
	}

	// Create the block store state structure.
	struct ufs_state *us = calloc(1, sizeof(*us));
	us->below = below;

	// get super block
	if ((*below->read)(below, 0, (block_t *)&us->superblock) < 0) {
		free(us);
		return NULL;
	}

	// Check the inode number.
	if (inode_no >= us->superblock.superblock.n_inodeblocks * INODES_PER_BLOCK) {
		fprintf(stderr, "!!UFSERR: inode number too large %u %u\n", inode_no, us->superblock.superblock.n_inodeblocks);
		free(us);
		return NULL;
	}

	// Find the inode.
	us->inode_blockno = 1 + inode_no / INODES_PER_BLOCK;
	if ((*below->read)(below, us->inode_blockno, (block_t *)&us->inodeblock) < 0) {
		free(us);
		return NULL;
	}
	us->inode = &us->inodeblock.inodeblock.inodes[inode_no % INODES_PER_BLOCK];

	// Return a block interface to this inode.
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
static block_no setup_freebitmapblocks(block_if below, block_no next_free, block_no nblocks){
	//estimate the num of free bit blocks we need
	unsigned int n_inodeblocks = next_free - 1; //the num of inode blocks
	unsigned int K = nblocks - 1 - n_inodeblocks;
	//block_no n_freebitmapblocks = (block_no)ceil(K / (1 + BLOCK_SIZE * NUM_BITS_IN_BYTES)); // estimate # of freebitmap blocks
	block_no n_freebitmapblocks = 1;
	block_no n_remaining_blocks = K - n_freebitmapblocks;
	// correct the estimated freebitmap blocks if needed
	while (n_remaining_blocks > n_freebitmapblocks * BLOCK_SIZE * NUM_BITS_IN_BYTES) { // iterate to get # of freebitmap blocks
		n_freebitmapblocks++;
		n_remaining_blocks--;
	}

	block_no leftoverblocks = n_remaining_blocks % (BLOCK_SIZE * NUM_BITS_IN_BYTES); //num blocks that will use a partial bitmap block
	block_no n_fullfreebitmapblocks = (leftoverblocks == 0) ? n_freebitmapblocks : n_freebitmapblocks - 1; // # bitmap blocks where each bit corresponds to a free block
	int k;
	for (k = 0; k < n_fullfreebitmapblocks; k++) //set all blocks for full free bitmap blocks
	{
		if ((*below->write)(below, next_free + k, (block_t *)&null_block) < 0) {
			panic("ufs_setup_freebitmapblocks");
		}
	}


	//if we have any leftover block, set bits associated with block to 0 and remaining bits to 1 to indicate it is unavailable
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
			freebitmapblock.freebitmapblock.status[fullBytes] = (char)uChar;
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

	unsigned int n_inodeblocks = (n_inodes + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK;
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
	superblock.superblock.n_freebitmapblocks = setup_freebitmapblocks(below, n_inodeblocks + 1, nblocks);
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
