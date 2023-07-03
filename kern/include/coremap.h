/*
 * Bitmap to keep track of free frames
*/
char *bitmap;

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