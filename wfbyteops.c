/*
    wfbyteops.c

    Copyright (C) 2007 David Crawley

    This file is part of OpenDAB.

    OpenDAB is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenDAB is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with OpenDAB.  If not, see <http://www.gnu.org/licenses/>.
*/
/*
** Byte manipulation functions
*/

#define _XOPEN_SOURCE 1
#include <unistd.h>
#include <endian.h>

/* 
** Pack 4 bytes into an int in usable order
*/
int ipack(unsigned char *f)
{
	int i, j;

	i = *f;
	i = i << 24;
	j = *(f+1); 
	i |= j << 16;
	j = *(f+2);
	i |= j << 8;
	j = *(f+3);

	return (i|j);
}

/* 
** Pack 2 bytes into a short in usable order
*/
short spack(unsigned char *f)
{
	short i;

	i = *f;
	i = i << 8;
	i |= *(f+1);

	return (i);
}

/*
** Swap bytes in an integer
*/
void iswab(int *i)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
	int j;

	swab(i, &j, sizeof(int));
	*i = j;
	return;
#endif
}

/*
** Swap bytes in a short
*/
void sswab(short *s)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
	short j;

	swab(s, &j, sizeof(short));
	*s = j;
	return;
#endif
}

/*
** Make the contents of an unsigned int
** into a big endian representation
*/
void bigend(unsigned int *i)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
	unsigned int t;

    ((unsigned char*)&t)[0] = ((unsigned char*)i)[3];
    ((unsigned char*)&t)[1] = ((unsigned char*)i)[2];
    ((unsigned char*)&t)[2] = ((unsigned char*)i)[1];
    ((unsigned char*)&t)[3] = ((unsigned char*)i)[0];

    *i = t;
#endif
}
	
