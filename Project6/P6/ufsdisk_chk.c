/* Author: Robbert van Renesse, August 2015
 *
 * Code to check the integrity of a treedisk file system.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "block_if.h"
#include "ufsdisk.h"
#include <math.h>

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

static void ufs_setbit(struct ufs_snapshot *snapshot, block_if below, block_no blockNum, char* freebitmaparray) {
	//figure out what index the block num is stored at
	int index = blockNum / NUM_BITS_IN_BYTES; //which byte in our block our block is represented in
	int bitToReset = blockNum % NUM_BITS_IN_BYTES; //which bit in our byte our block is represented as

	unsigned char uChar = 0x1;
	freebitmaparray[index] |= ~(uChar <<= abs(bitToReset - 7));

	return 0;
}

static int ufsdisk_traverseufstree(struct ufs_snapshot *snapshot, block_if below, int nlevels, block_no blocknum, char* freebitmaparray)
{
	if (nlevels == 0) ufs_setbit(snapshot, below, blocknum, freebitmaparray);
	else { //we have to traverse the indirect blocks to get to our data blocks
		nlevels--;
		struct ufs_indirblock indirblock;
		if ((*below->read)(below, blocknum, (block_t *)&indirblock) < 0) {
			fprintf(stderr, "!!TDERR: Failure to read data block in ufsdisk_updatefreebits\n");
			return -1;
		}
		int k;
		for (k = 0; k < REFS_PER_BLOCK; k++)
		{
			if (indirblock.refs[k] != 0) { //if it is not a hole
				ufsdisk_traverseindblocks(snapshot, below, nlevels, indirblock.refs[k]);
				indirblock.refs[k] = 0;
			}
		}
		ufs_freebit(snapshot, below, blocknum);
		if ((*below->write)(below, snapshot->inode_blockno, (block_t *)&snapshot->inodeblock) < 0) {
			fprintf(stderr, "!!TDERR: Failed to write inode back to disk in ufsdisk_setsize\n");
			return -1;
		}
	}
	return 0;
}

static int ufsdisk_chk_validate_freebitblocks(struct ufs_snapshot *snapshot, block_if bi, block_no ndatablocks, block_no nfreebitmapblocks)
{
	//create an array that stores a bit to represent each free block
	size_t len = ceil(ndatablocks / NUM_BITS_IN_BYTES);
	char *freebitmapblockcheck = malloc(sizeof(len));
	memset(freebitmapblockcheck, 0, len);

	//if there is a partial byte, set the extra bits to 1
	unsigned int leftoverbits = ndatablocks % NUM_BITS_IN_BYTES;
	if (leftoverbits > 0)
	{
		unsigned char uChar = 0xFF;
		uChar >>= leftoverbits;
		freebitmapblockcheck[len-1] = (char)uChar;
	}

	//iterate through all inodes to update this freebitmapblockcheck array
	union ufs_block inodeblock;
	int inodeblockindex;
	int nlevels = -1;
	int index;
	struct ufs_state *us = bi->state;
	for (inodeblockindex = 1; inodeblockindex <= snapshot->superblock.superblock.n_inodeblocks; inodeblockindex++)
	{
		if ((*us->below->read)(us->below, inodeblockindex, (block_t *)&inodeblock) < 0) {
			fprintf(stderr, "!!TDERR: Failure to read inode block in ufsdisk_chk_validate_freebitblocks\n");
			return -1;
		}
		for (index = 0; index < REFS_PER_INODE; index++)
		{
			if (index < NUM_DIRECT_BLOCKS && snapshot->inode->refs[index] != 0) {
				nlevels = 0;
				int success = ufsdisk_traverseufstree(snapshot, us->below, nlevels, snapshot->inode->refs[index], freebitmapblockcheck);
				if (success == -1) 	return -1;
				snapshot->inode->refs[index] = 0;
			}
			else if (index == SINGLE_INDIRECT_BLOCK_INDEX && snapshot->inode->refs[SINGLE_INDIRECT_BLOCK_INDEX] != 0) { //block being read is pointed to by the single indirect block
				nlevels = 1;
				int success = ufsdisk_traverseufstree(snapshot, us->below, nlevels, snapshot->inode->refs[index], freebitmapblockcheck);
				if (success == -1) 	return -1;
				snapshot->inode->refs[index] = 0;
			}
			else if (index == DOUBLE_INDIRECT_BLOCK_INDEX && snapshot->inode->refs[DOUBLE_INDIRECT_BLOCK_INDEX] != 0) { //block being read is pointed to by the double indirect block
				nlevels = 2;
				int success = ufsdisk_traverseufstree(snapshot, us->below, nlevels, snapshot->inode->refs[index], freebitmapblockcheck);
				if (success == -1) 	return -1;
				snapshot->inode->refs[index] = 0;
			}
			else if (snapshot->inode->refs[NUM_TRIPLE_INDIRECT_BLOCKS] != 0) { //block being read is pointed to by the triple indirect block
				nlevels = 3;
				int success = ufsdisk_traverseufstree(snapshot, us->below, nlevels, snapshot->inode->refs[index], freebitmapblockcheck);
				if (success == -1) 	return -1;
				snapshot->inode->refs[index] = 0;
			}
		}
	}

	//once we've iterated through all our inodes and updated our free bit map array, check against the freebitmapblocks stored on disk
	int bitmapindex = inodeblockindex + 1;
	union ufs_block freebitmapblock;
	unsigned int freebitmapcheckarrayindex = 0;
	unsigned int freebitmapblockindex;
	for (bitmapindex; bitmapindex < bitmapindex + snapshot->superblock.superblock.n_freebitmapblocks; bitmapindex++)
	{
		for (freebitmapcheckarrayindex = 0; freebitmapcheckarrayindex < REFS_PER_BLOCK && freebitmapcheckarrayindex < len; freebitmapcheckarrayindex++)
		{
			if (freebitmapblockcheck[freebitmapcheckarrayindex] != freebitmapblock.freebitmapblock.status[freebitmapblockindex]) return -1; //if there is a mismatch, return -1
			freebitmapcheckarrayindex++;
		}
	}

	return 0;
}

//checks our file system. Returns -1 if there is a problem, 0 otherwise.
static int ufsdisk_chk_validate_filesystem(struct ufs_snapshot *snapshot, block_if bi, block_no ndatablocks, block_no nfreebitmapblocks)
{
	//validate magic number
	if (snapshot->superblock.superblock.magic_number != MAGIC_NUMBER)
	{
		return -1;
	}

	//check freebitmapblock bits
	return ufsdisk_chk_validate_freebitblocks(snapshot, bi, ndatablocks, nfreebitmapblocks);
}