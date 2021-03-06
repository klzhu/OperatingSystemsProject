/* Author: Robbert van Renesse, August 2015
 *
 * Uses the trace disk to apply a trace to a cached disk.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include "block_if.h"
#include "ufsdisk.h"

#define DISK_SIZE		(16 * 1024)		// size of "physical" disk
#define MAX_INODES		128

static block_t blocks[DISK_SIZE];		// blocks for ram_disk

static void sigalrm(int s){
	fprintf(stderr, "test ran for too long\n");
	exit(1);
}

int main(int argc, char **argv){
	char *trace = argc == 1 ? "trace2.txt" : argv[1];
	int cache_size = argc > 2 ? atoi(argv[2]) : 16;
	int ramdisk = 1;

	printf("blocksize:  %u\n", BLOCK_SIZE);
	printf("refs/block: %u\n", (unsigned int) (BLOCK_SIZE / sizeof(block_no)));

	/* First create the lowest level "store".
	 */
	block_if disk;
	if (ramdisk) {
		disk = ramdisk_init(blocks, DISK_SIZE);
	}
	else {
		disk = disk_init("disk.dev", DISK_SIZE);
	}

	/* Start a timer to try to detect infinite loops or just insanely slow code.
	 */
	signal(SIGALRM, sigalrm);
	alarm(5);
	/* Virtualize the store, creating a collection of 64 virtual stores.
	 */
	if (ufsdisk_create(disk, MAX_INODES, MAGIC_NUMBER) < 0) {
		panic("trace: can't create nfsdisk file system");
	}

	/* Add a disk to keep track of statistics.
	 */
	block_if sdisk = statdisk_init(disk);

	/* Add a layer of caching.
	 */
	block_t *cache = malloc(cache_size * BLOCK_SIZE);
	block_if cdisk = cachedisk_init(sdisk, cache, cache_size);

	/* Add a layer of checking to make sure the cache layer works.
	 */
	block_if xdisk = checkdisk_init(cdisk, "cache");

	/* Run a trace.
	 */
	block_if tdisk = tracedisk_init(xdisk, trace, MAX_INODES);

	/* Clean up.
	 */
	(*tdisk->destroy)(tdisk);
	(*xdisk->destroy)(xdisk);
	(*cdisk->destroy)(cdisk);

	/* No longer running nfsdisk or cachedisk code.
	 */
	alarm(0);

	/* Print stats.
	 */
	statdisk_dump_stats(sdisk);

	(*sdisk->destroy)(sdisk);

	/* Check that disk just one more time for good measure.
	 */
	if (ufsdisk_check(disk) < 0) {
		fprintf(stderr, "!!ERROR: nfsdisk_check has failed.\n");
	} else {
		fprintf(stderr, "GREAT!! nfsdisk_check has succeeded.\n");
	}

	(*disk->destroy)(disk);

	free(cache);

	return 0;
}
