#include <list.h>
#include "devices/block.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "devices/timer.h"

#define MAX_CACHE_SIZE 48
/* initialize a newly allocated block */
static void block_initalize(struct cache_block*, block_sector_t, int, int);
/* cache write back every "period" time */ 
void periodically_cache_write_back(int);
struct cache_block* insert_cache_block(block_sector_t , int );

void 
cache_init(void){
    lock_init(&cache_lock);
    list_init(&cache);
    cache_size = 0;
    thread_create("filesys_cache_writeback", 0, periodically_cache_write_back, NULL); //???
}

/* initalize cache block sector, getting data from the file disk */
static void 
block_initalize(struct cache_block* block, block_sector_t sector, int dirty, int reference_bit){
    block->sector = sector;
    block_read(fs_device,block->sector, &block->block_data);
    block->dirty = dirty;
    block->reference_bit= reference_bit;
    block->occupied = 1;
}

/* return the cache_block with sector = sector, return null if not in cache */
struct cache_block* 
seek_cache_block(block_sector_t sector){
    struct cache_block* block;
    struct list_elem* e;
    for(e = list_begin(&cache); e!=list_end(&cache); e = list_next(e)){
        block = list_entry(e, struct cache_block, elem);
        if (block->sector == sector) return block;
    }
    return NULL; /* no cache block found */
} 

/* UI to un-occupied */
void release_cache_block(struct cache_block* cb){
    lock_acquire(&cache_lock);
    cb->occupied = (cb->occupied == 0 ? 0 : (cb->occupied - 1));
    lock_release(&cache_lock);
}

/* UI for access to block sector, 
   dirty = 0 -> read; dirty = 1 -> write 
   returns the pointer to the cache block for operation */
struct cache_block* 
get_cache_block(block_sector_t sector, int dirty){
    lock_acquire(&cache_lock);
    struct cache_block* block = seek_cache_block(sector);
    if (block!=NULL){
        /* sector already cached */
        block->occupied++;
        if (block->dirty == 0){
            block->dirty = dirty;
        }
        block->reference_bit = 1;
        lock_release(&cache_lock);
        return block;
    }
    else{
        /* sector not cached */
        /* we need to cache this sector, evict old cache if need to*/
        block = insert_cache_block(sector, dirty);
        if (block!=NULL){
            lock_release(&cache_lock);
            return block;
        }
    }
    lock_release(&cache_lock);
    return block;
}
/* insert a new cache block with "sector" and "dirty" */
/* WARN: this has problem if all occupied, need to rethink*/
struct cache_block* 
insert_cache_block(block_sector_t sector, int dirty){
    struct cache_block* insert_block = NULL;
    struct cache_block* replace_block;
    struct list_elem* e;
    if (cache_size < MAX_CACHE_SIZE){
        /*if cache not full place the new sector directly */
        insert_block = malloc(sizeof(struct cache_block));
        list_push_back(&cache, &insert_block->elem);
        cache_size ++;
    }
    else{
        while (!insert_block)
        {  
            // choose a block to replace
            for(e = list_begin(&cache); e!=list_end(&cache); e = list_next(e)){
                replace_block = list_entry(e, struct cache_block, elem);
                if (replace_block->occupied == 0){
                    /* noone is using this block */
                    if (replace_block->reference_bit == 0){

                        if (replace_block->dirty == 1){
                            /*write back to disk if dirty */
                            block_write(fs_device, replace_block->sector, &replace_block->block_data);
                        }
                        insert_block = replace_block;
                        break;
                    }
                    else{
                        replace_block->reference_bit = 0;
                    }
                }
            }
        }
    }
    block_initalize(insert_block, sector, dirty, 1);
    return insert_block;
}

/* scan the whole cache, and write back dirty block to disk. 
if if_flushed = 1, flush the whole cache, i.e. write back dirty + empty the cache */
void 
scan_cache_write_back(int if_flushed){    
    lock_acquire(&cache_lock);
    struct list_elem* e;
    struct cache_block* block;
    for(e = list_begin(&cache); e!=list_end(&cache); e = list_next(e)){
        block = list_entry(e, struct cache_block, elem);
        if ((block->dirty == 1) && if_flushed){
            block_write(fs_device, block->sector, &block->block_data);
            block->dirty = 0;
        }
    }
    lock_release(&cache_lock);
}

/* cache write back every "period" time */ 
/* WARN: period is not right */
void 
periodically_cache_write_back(int period UNUSED){
    while (true)
    {
        timer_sleep(INT32_MAX);
        scan_cache_write_back(true);
    }
}

/* reading from sector to buffer */
void
cache_block_read (struct block *block, block_sector_t sector, void *buffer)
{
    struct cache_block *_cache_block = get_cache_block(sector,0);
    memcpy(buffer, _cache_block->block_data, BLOCK_SECTOR_SIZE);
    release_cache_block(_cache_block);
}

void
cache_block_write (struct block *block, block_sector_t sector, const void *buffer)
{
    struct cache_block *_cache_block = get_cache_block(sector,1);
    memcpy(_cache_block->block_data, buffer, BLOCK_SECTOR_SIZE);
    release_cache_block(_cache_block);
}