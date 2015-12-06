/* 
 *
 * Code to check the integrity of a ufsdisk file system
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "block_if.h"
#include "ufsdisk.h"

static const int MAX_LEVEL = 4;
static const int NUM_BLOCKS_IN_LEVEL[4] = { REFS_PER_INODE - 3, REFS_PER_BLOCK, REFS_PER_BLOCK*REFS_PER_BLOCK,
			REFS_PER_BLOCK * REFS_PER_BLOCK * REFS_PER_BLOCK };  // capacity (# of blocks) at each level
static const int INDIRECT_INDEX[4] = { 0, REFS_PER_INODE - 3, REFS_PER_INODE - 2, REFS_PER_INODE - 1 }; // (starting) index of direct and 1-3 indir levels

struct block_info
{
	enum { BI_UNKNOWN, BI_SUPER, BI_INODE, BI_BITMAP, BI_INDIR, BI_DATA, BI_FREE } status;
};

// it checks indirect pointer of an inode and returns 0 if alright or -1 is something error is detected
// node is the index of the entering indir block, maxOffset is the max offset relative to the beginning of the current level
// of indirect pointer. minBlockIndex is the min valid block index, and fs_nblocks is the total number of blocks of 
// the underlying file system pointerd by below.
static int check_inode_indirect(block_if below, block_no node, block_no maxOffset, block_no nlevels,
		block_no minBlockIndex, block_no fs_nblocks, struct block_info *bi)
{
	block_no b;

	assert(nlevels >= 1 && nlevels < MAX_LEVEL); // it must be a valid indirect pointer
	if (node == 0) 	return 0;
	if (node < minBlockIndex || node >= fs_nblocks) {
		fprintf(stderr, "!!UFSCHKERR: remaining block (%u) is beyond its range\n", node);
		return -1;
	}

	if (bi[node].status != BI_UNKNOWN) {
		fprintf(stderr, "!!UFSCHKERR: data block already used\n");
		return -1;
	}
	
	bi[node].status = BI_INDIR;
	union ufs_block indirblock;
	if ((*below->read)(below, node, (block_t *)&indirblock) < 0) {
		fprintf(stderr, "!!UFSCHKERR: failed in reading a block\n");
		return -1;
	}

	nlevels--;
	if (nlevels == 0) { // data pointers
		// change in-range referneces
		for (b = 0; b <= maxOffset; b++) { 
			if (indirblock.indirblock.refs[b] != 0) {
				if (indirblock.indirblock.refs[b] < minBlockIndex || indirblock.indirblock.refs[b] >= fs_nblocks) {
					fprintf(stderr, "!!UFSCHKERR: remaining block (%u) is beyond its range\n", indirblock.indirblock.refs[b]);
					return -1;
				} else if (bi[indirblock.indirblock.refs[b]].status != BI_UNKNOWN) {
					fprintf(stderr, "!!UFSCHKERR: remaining block is already used.\n");
					return -1;
				} else
					bi[indirblock.indirblock.refs[b]].status = BI_DATA;
			}
		}

		// change beyond-range referneces
		for (b = maxOffset + 1; b < REFS_PER_BLOCK; b++) {
			if (indirblock.indirblock.refs[b] != 0) {
				fprintf(stderr, "!!UFSCHKERR: inode has an offset beyond its nblocks\n");
				return -1;
			}
		}
	} else { //still another indirect pointer
		block_no numBlocksPerEntry = NUM_BLOCKS_IN_LEVEL[nlevels]; // # of blocks each indirect pointer can take
		bool done = false;
		for (b = 0; b < REFS_PER_BLOCK; b++) {
			if (indirblock.indirblock.refs[b] != 0) {
				if (done) { // no block is allowed
					fprintf(stderr, "!!UFSCHKERR: inode has an offset beyond its nblocks\n");
					return -1;
				} else if (maxOffset < numBlocksPerEntry) {
					if (check_inode_indirect(below, indirblock.indirblock.refs[b], maxOffset,
						nlevels, minBlockIndex, fs_nblocks, bi) < 0) {
						return -1;
					}

					done = true;
				} else {
					if (check_inode_indirect(below, indirblock.indirblock.refs[b], numBlocksPerEntry - 1,
						nlevels, minBlockIndex, fs_nblocks, bi) < 0) {
						return -1;
					}

					maxOffset -= numBlocksPerEntry;
				}
			}
		}
	}

	return 0;
}

//checks ufsdisk file system. Returns -1 if there is a problem, 0 otherwise.
int ufsdisk_check(block_if below)
{
	if (below == NULL) {
		fprintf(stderr, "!!UFSCHKERR: Invalid input in ufsdisk_check\n");
		return -1;
	}

	block_no b, k;
	union ufs_block superblock, usfblock;

	block_no fs_nblocks = (*below->nblocks)(below); // number of blocks of underlying storage
	if (fs_nblocks == 0) {
		fprintf(stderr, "!!UFSCHKERR: empty underlying storage\n");
		return 0;
	}

	// get superblock
	if ((*below->read)(below, 0, (block_t *)&superblock) < 0) {
		fprintf(stderr, "!!UFSCHKERR: Failed in read a block in ufsdisk_check\n");
		return -1;
	}

	//Check the superblock
	if (superblock.superblock.magic_number != MAGIC_NUMBER) {
		fprintf(stderr, "!!UFSCHKERR: Invalid magic number in ufsdisk_check\n");
		return -1;
	}
	if (1 + superblock.superblock.n_inodeblocks + superblock.superblock.n_freebitmapblocks > fs_nblocks) {
		fprintf(stderr, "!!UFSCHKERR: #inodes: %u #bitmap nodes: %u, total blocks: %u\n", 
			superblock.superblock.n_inodeblocks, superblock.superblock.n_freebitmapblocks, fs_nblocks);
		fprintf(stderr, "!!UFSCHKERR: not enough room for inode blocks and bitmap blocks\n");
		return -1;
	}

	// the index of the first remaining block
	block_no firstRemainingIndex = 1 + superblock.superblock.n_inodeblocks + superblock.superblock.n_freebitmapblocks;

	// Initialie the block info to BI_UNKNOWN
	struct block_info *bi = (struct block_info *) calloc(fs_nblocks, sizeof(*bi)); 

	block_no blockIdx = 0;
	bi[blockIdx++].status = BI_SUPER;	// label superblock

	// Scan the inode blocks.
	for (; blockIdx <= superblock.superblock.n_inodeblocks; blockIdx++) {
		bi[blockIdx].status = BI_INODE;	// label the node as indoe
		// read the inode block
		if ((*below->read)(below, blockIdx, (block_t *)&usfblock) < 0) {
			free(bi);
			fprintf(stderr, "!!UFSCHKERR: Failed in read a block in ufsdisk_check\n");
			return -1;
		}

		// traverse all blocks for each inode
		for (k = 0; k < INODES_PER_BLOCK; k++) {
			struct ufs_inode *inode = &usfblock.inodeblock.inodes[k];
			if (inode->nblocks <= NUM_BLOCKS_IN_LEVEL[0]) { // direct pointers
				b = 0;
				// check offset < inode->nblocks
				for (b = 0; b < inode->nblocks; b++) {
					if (inode->refs[b] != 0) {
						if (inode->refs[b] < firstRemainingIndex || inode->refs[b] >= fs_nblocks) { // block_no is out of range
							free(bi);
							fprintf(stderr, "!!UFSCHKERR: remaining block is beyond its range.\n"); 
							return -1;
						} else if (bi[inode->refs[b]].status != BI_UNKNOWN) {
							free(bi);
							fprintf(stderr, "!!UFSCHKERR: remaining block is already used.\n");
							return -1;
						} else
							bi[inode->refs[b]].status = BI_DATA;
					}
				}

				// check remaining pointers, which must be 0
				for (b = inode->nblocks; b < REFS_PER_INODE; b++) {
					if (inode->refs[b] != 0) {
						free(bi);
						fprintf(stderr, "!!UFSCHKERR: innode <%u, %u> has an offset (%u) >= its nblocks (%u)\n",
										blockIdx - 1, k, b, inode->nblocks);
						return -1;
					}

				}
			} else { // indirect pointers
				// finds its level and max offset related to the begining of the level
				block_no maxOffset = inode->nblocks - 1; // the largest offset allowed
				block_no nlevels = 0;
				while (nlevels < MAX_LEVEL && maxOffset >= NUM_BLOCKS_IN_LEVEL[nlevels]) {
					maxOffset -= NUM_BLOCKS_IN_LEVEL[nlevels];	// adjust offset to be relative to the beginning of the current level
					nlevels++;
				}
				if (maxOffset >= NUM_BLOCKS_IN_LEVEL[nlevels]) {
					free(bi);
					fprintf(stderr, "!!UFSCHKERR: inode's nblocks has overflowed.\n");
					return -1;
				}

				assert(nlevels >= 1); 
				// check indirect levels at and below nlevels
				for (b = 1; b <= nlevels; b++) {
					if (inode->refs[INDIRECT_INDEX[b]] != 0) {
						if (check_inode_indirect(below, inode->refs[INDIRECT_INDEX[b]], (b == nlevels) ? maxOffset : NUM_BLOCKS_IN_LEVEL[b] - 1,
								b, firstRemainingIndex, fs_nblocks, bi) < 0) {
							free(bi);
							return -1;
						}
					}
				}

				// check indirect levels above nlevels, which should be 0
				for (b = nlevels + 1; b < MAX_LEVEL; b++) {
					if (inode->refs[INDIRECT_INDEX[b]] != 0) {
						free(bi);
						fprintf(stderr, "!!UFSCHKERR: innode <%u, %u> has a no empty indirect pointer beyond its nblocks (%u)\n",
										blockIdx - 1, k, inode->nblocks);
						return -1;
					}
				}
			}
		}
	}
	assert(blockIdx == 1 + superblock.superblock.n_inodeblocks);

	// Scan bitmap blocks
	blockIdx = firstRemainingIndex; // first remaining block
	for (b = 1 + superblock.superblock.n_inodeblocks; b < firstRemainingIndex; b++) {
		bi[b].status = BI_BITMAP;	// label the block as bitmap block

		// read in the current bitmap block
		if ((*below->read)(below, b, (block_t *)&usfblock) < 0) {
			fprintf(stderr, "!!UFSCHKERR: Failed in reading in a block.\n");
			return -1;
		}

		int byteIdx;
		for (byteIdx = 0; byteIdx < BLOCK_SIZE; byteIdx++) {
			if (blockIdx >= fs_nblocks) {
				if (usfblock.freebitmapblock.status[byteIdx] != (char)0xFF) {
					free(bi);
					fprintf(stderr, "!!UFSCHKERR: bits beyond underlying file system are not set to unavailable\n");
					return -1;
				}

				blockIdx += NUM_BITS_IN_BYTES;
			} else { // process each bit
				unsigned char currBit = 0x80; // initialize the byte with the leftest bit to be 1
				unsigned char currByte = usfblock.freebitmapblock.status[byteIdx]; // get the current byte
				for (k = 0; k < NUM_BITS_IN_BYTES; k++) { // check each bit of the current byte
					if ((currBit & currByte) == 0) { // the block associated with the bit is available 
						if (blockIdx >= fs_nblocks || bi[blockIdx].status != BI_UNKNOWN) {
							fprintf(stderr, "!!UFSCHKERR: data block marked available has been used or unavailable block marked as available.\n");
							return -1;
						} else {
							bi[blockIdx].status = BI_FREE;
						}
					} else { // the block has been used
						if (blockIdx < fs_nblocks && bi[blockIdx].status != BI_INDIR && bi[blockIdx].status != BI_DATA) {
							fprintf(stderr, "!!UFSCHKERR: data block has been marked wrong\n");
							return -1;
						}
					}

					currBit >>= 1;	// right shift the current bit by 1
					blockIdx++;
				}
			}
		}

	}

	// Check statuses of blocks.
	for (b = 0; b < fs_nblocks; b++) {
		if (bi[b].status == BI_UNKNOWN) {
			free(bi);
			fprintf(stderr, "!!UFSCHKERR: unaccounted for block %u\n", b);
			return -1;
		}
	}

	free(bi);
	return 0;
}