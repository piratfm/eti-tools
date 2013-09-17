/*
    prot.h

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
** Structures holding UEP and EEP
** profiles.
*/

#ifndef PROT_H
#define PROT_H    1

struct uepprof {
	unsigned int bitrate;
	unsigned int subchsz;
	unsigned int protlvl;
	int l[4];
	int pi[4];
	int padbits;
};

struct eepprof {
	int sizemul;
	int ratemul;
	struct {
		int mul;
		int offset;
	} l[2];
	int pi[2];
};

extern const struct uepprof ueptable[];
extern const struct eepprof eeptable[];
extern const struct eepprof eep2a8kbps;
extern const char pvec[][32];
#endif
