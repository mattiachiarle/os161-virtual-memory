#include "coremap.h"
#include "vm.h"
#include "mainbus.h"
#include "lib.h"
#include "cpu.h"
#include "types.h"
#include "spinlock.h"

static int numFrames = 0;
static int bitmapactive = 0; // bitmap non active

void bitmap_init(void)
{
    numFrames = mainbus_ramsize() / PAGE_SIZE;
    bitmap = kmalloc(sizeof(int) * numFrames);
    if (bitmap == NULL) // alloc bitmap
    {
        panic("error allocating bitmap");
    }

    for (int s = 0; s < numFrames; s++)
    { // init bitmap
        bitmap[s] = 0;
    }

    //acquire some protection like spin
        bitmapactive = 1;
    //release protection

}

int is_bitmap_active(void){
    return bitmapactive;
}

void destroy_bitmap(void){  //destroys bitmap structure
    
    //acquire protection
    bitmapactive=0;
    //release

    kfree(bitmap);
}
