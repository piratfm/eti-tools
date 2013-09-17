/*
    wfbyteops.h

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
#ifndef WFBYTEOPS_H
#define WFBYTEOPS_H    1

extern int ipack(unsigned char *);
extern short spack(unsigned char *);
extern void iswab(int *);
extern void sswab(short *);
extern void bigend(unsigned int *);
#endif
