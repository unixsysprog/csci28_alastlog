/*
 * lllib.h - header file with functions located in lllib.c
 */

int ll_open(char *);
int ll_seek(int);
struct lastlog *ll_read();
int ll_close();
