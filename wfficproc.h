/*
    wfficproc.h

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

#include "figs.h"

int process_fic2(unsigned char *msc_ptr, int ficl, int mode_id);

extern struct service* find_service(struct ens_info*, int);
extern int add_service(struct ens_info*, struct mscstau*, int);
extern int add_subchannel(struct ens_info*, struct subch *);

int ficinit(struct ens_info *);
int labelled(struct ens_info*);
int disp_ensemble(struct ens_info* e);
