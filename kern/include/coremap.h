/*
 * Bitmap to keep track of free frames ----> ADD MAYBE 0 not used, 1 in PT, 2 in swap file
 */
int *bitmap;
int bitmapactive = 0; // bitmap non active

/*
 * This function is used to get a free frame
 *
 * @return: index of the free frame
 */
int get_frame(void);

/*
 * This function is used to release a frame
 *
 * @param: index of the frame
 */
void free_frame(int);

/*
 *  initialize bitmap
 */
void bitmap_init(void);

/*
 *  bitmap destroy, deallocate bitmap
 */
void bitmap_destroy(void);

/*
 * return if bitmap active or not, if 1 active, if 0 non active
 */
int is_bitmap_active(void);