#ifndef ARUCO_PATTERNS_4X4_H
#define ARUCO_PATTERNS_4X4_H

/*
 * OpenCV DICT_4X4_50 marker inner 4x4 bit patterns.
 * 1 means dark cell, 0 means white cell.
 *
 * Final project ID rule:
 * id 0,1,2,3 = delivery target markers
 * id 4       = final goal marker
 */

#define ARUCO_NUM_PATTERNS 5

static const int ARUCO_IDS[ARUCO_NUM_PATTERNS] = {
    0, 1, 2, 3, 4
};

static const unsigned char ARUCO_PATTERNS[ARUCO_NUM_PATTERNS][16] = {
    {0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1}, /* id=0 */
    {1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1}, /* id=1 */
    {1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0}, /* id=2 */
    {0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0, 1}, /* id=3 */
    {1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 0, 0, 0, 1}  /* id=4 */
};

#endif
