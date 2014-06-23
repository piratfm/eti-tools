/*
  wfficproc.c

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
** FIC decoding.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
//#include "wfic/wfic.h"
#include "figs.h"
#include "prot.h"

/* DEBUG:
** 0 - no info
** 1 - print some info
** 2 - ...and dump raw bytes
*/
#define DEBUG 0

int unpickfig(unsigned char*, int);
void msc_assemble(char*, unsigned char*, unsigned char*, struct subch*, struct symrange*, FILE*);

int wfgetnum(char *);
struct ens_info einf;


unsigned char selstr[10];
int fibcnt = 0;


int ficinit(struct ens_info *einf)
{
	int j;

	einf->num_schans = 0;
	einf->num_srvs = 0;
	einf->srv = NULL;
	for (j=0; j < 64; j++)
		einf->schan[j].subchid = 0xffff; /* Mark unused entries */

	return 0;
}

/*
** Run through list and try to find entry with given service id
*/
struct service* find_service(struct ens_info *e, int sid)
{
	char found = 0;
	struct service *s;
	s = e->srv;
	while (s != NULL) {
		if (s->sid == sid) {
			found = 1;
			break;
		} else
			s = s->next;
	};
	if (found)
		return s;
	else
		return (struct service*)NULL;
}

/*
** Append an audio service to the list if it isn't already known
*/
int add_service(struct ens_info *e, struct mscstau *ac, int sid)
{
	struct service *s, *ptr;

	if ((s = find_service(e, sid)) == (struct service*)NULL) {
		if ((s = (struct service*)malloc(sizeof(struct service))) == NULL)
			perror("add_service: malloc failed");
		s->label[0] = '\0';
		s->sid = sid;
		s->next = NULL;
		s->pa = NULL;
		s->sa = NULL;
		if ((ptr = e->srv) == NULL) {
			e->srv = s;
			s->next = NULL;
			s->prev = NULL;
		} else
			while (ptr != NULL)
				if (ptr->next == NULL) {
					ptr->next = s;
					s->prev = ptr;
					break;
				} else
					ptr = ptr->next;
	}
	if (ac->ASCTy == 0x3f)
		e->schan[ac->SubChId].dabplus = 1;
	else
		e->schan[ac->SubChId].dabplus = 0;

	if ((ac->Primary && (s->pa == NULL)) || (!(ac->Primary) && (s->sa == NULL))) {
		if (ac->Primary && (s->pa == NULL)) {
			s->pa = &e->schan[ac->SubChId];
			e->num_srvs++;
		} else
			if (!(ac->Primary) && (s->sa == NULL)) {
				s->sa = &e->schan[ac->SubChId];
				e->num_srvs++;
			}
#if DEBUG > 0
		fprintf(stderr,"add_service: sid=%#08x subchan=%d pri=%d\n",sid,ac->SubChId,ac->Primary);
#endif
	}
#if DEBUG > 0
	else
		fprintf(stderr,"add_service: Service %#08x already known\n",sid);
#endif
	return 0;
}

/*
** Add the subchannel if it isn't already known
*/
int add_subchannel(struct ens_info *e, struct subch *s)
{
	if (e->schan[s->subchid].subchid > 64) {
#if DEBUG > 0
		fprintf(stderr,"add_subchannel: subchid=%d\n",s->subchid);
#endif
		memcpy(&e->schan[s->subchid], s, sizeof(struct subch));
	}
	return 0;
}

/*
** Check all programme services have been labelled
*/
int labelled(struct ens_info* e)
{
	struct service *p;
	int labelled = 1;

	p = e->srv;
	if (p == NULL)
		return 0;    /* No services yet */

	while (p != NULL) {
		if (strlen(p->label) == 0) {
#if DEBUG > 0
			fprintf(stderr,"labelled: sid %#04x has no label yet\n",p->sid);
#endif
			labelled = 0;
			break;
		}
		p = p->next;
	}
	return labelled;
}


/*
** Display list of services
*/
int disp_ensemble(struct ens_info* e)
{
	struct service *p;
	int i = 0;
	char *eeptype[] = {"eep-1a","eep-2a","eep-3a","eep-4a",
			   "eep-1b","eep-2b","eep-3b","eep-4b"};

	fprintf(stderr,"%s (%#04hx)\n",e->label,e->eid);
	p = e->srv;

	while (p != NULL) {
		fprintf(stderr,"%2d : ",i++);
		if (strlen(p->label) != 0)
			fprintf(stderr,"%16s (%#04x) ",p->label,p->sid);
		else
			fprintf(stderr,"<No label>       (%#04x) ",p->sid);
		if (p->pa != NULL) {
			fprintf(stderr,"Pri subch=%2d start=%3d CUs=%3d ",p->pa->subchid,p->pa->startaddr,p->pa->subchsz);
			if (p->pa->eepprot)
				fprintf(stderr,"PL=%s bitrate=%d",eeptype[p->pa->protlvl],p->pa->bitrate);
			else
				fprintf(stderr,"PL=uep %d bitrate=%d",p->pa->protlvl,p->pa->bitrate);
			if (p->pa->dabplus)
				fprintf(stderr," DAB+");
			if (p->sa != NULL) {
				fprintf(stderr,"\n");
				fprintf(stderr,"%2d :                           ",i++);
				fprintf(stderr,"Sec subch=%2d start=%2d CUs=%3d ",p->sa->subchid,p->sa->startaddr,p->sa->subchsz);
				if (p->sa->eepprot)
					fprintf(stderr,"PL=%s bitrate=%d",eeptype[p->sa->protlvl],p->sa->bitrate);
				else
					fprintf(stderr,"PL=uep %d bitrate=%d",p->sa->protlvl,p->sa->bitrate);
				if (p->sa->dabplus)
					fprintf(stderr," DAB+");
			}
		}
		p = p->next;
		fprintf(stderr,"\n");
		fflush(stderr);
	}
	return 0;
}

/*
** Determine which service to decode
*/
int user_select_service(struct ens_info* e, struct selsrv *sel_srv)
{
	struct service *p;
	int srvs = einf.num_srvs-1, i = -1, j = 0;
	static int rq = 0;

	if (!rq) {
		fprintf(stderr,"Please select a service (0 to %d): ",srvs);
		rq = 1;
	}

	/* selected services */
	//static char lbuf[80];
	i = 1;//wfgetnum(lbuf);

	if (i != -1) {
		if ((i < einf.num_srvs) && (i >= 0)) {
			p = einf.srv;
			while (p != NULL) {
				if ((j == i) || ((j == i - 1) && (p->sa != NULL))) {
					sel_srv->sid = p->sid;
					if ((j == i - 1) && (p->sa != NULL))
						sel_srv->sch = p->sa;
					else
						sel_srv->sch = p->pa;
				}
				if (p->sa == NULL)
					j += 1;
				else
					j += 2;
				p = p->next;
			}
		} else {
			rq = 0;
			sel_srv->sch = NULL;
		}
	}
	return 0;
}
#if 0
int process_fic(unsigned char* ficsyms, unsigned char* rawfibs, FILE *ofp)
{
	int fibs, i, j;
	unsigned char *fig;
	int figlen;

	fibs = fic_decode(ficsyms, ficsyms+384, ficsyms+768, rawfibs);
	fibcnt += fibs;

	if (ofp != NULL)
		fwrite(rawfibs, fibs, 30*sizeof(unsigned char), ofp);

	if (fibs > 0) {
		for (i=0; i < fibs; i++) {
			fig = rawfibs + 30*i;
#if DEBUG > 1
			{
				int k;
				fprintf(stderr,"fib = %d ",i);
				for (k=0; k < 30; k++)
					fprintf(stderr,"%02x ",*(fig+k));
				fprintf(stderr,"\n");
			}
#endif
			j = 0;
			while ((j < 30) && (*fig != 0xff)) {
				figlen = ((struct fig_hdr*)fig)->flen;
				unpickfig(fig, figlen);
				j += figlen + 1;
				fig += figlen + 1;
			}
		}
	}
	return 0;
}
#else

int process_fic2(unsigned char* rawfibs, int ficl, int mode_id)
{
	unsigned char *fig = rawfibs;
	int i,j,figlen;
	if(!ficl) return 0;

	for (i=0; i < ficl/32; i++) {
		fig = rawfibs + 32*i;
#if DEBUG > 1
		int k;
		fprintf(stderr,"fib[%d]: ",i);
		for (k=0; k < 30; k++)
			fprintf(stderr,"%02x ",*(fig+k));
		fprintf(stderr,"|%02x ",*(fig+k));
		fprintf(stderr,"%02x\n",*(fig+k+1));
#endif

		j = 0;
		while ((j < 30) && (*fig != 0xff)) {
			figlen = ((struct fig_hdr*)fig)->flen;
			unpickfig(fig, figlen);
			j += figlen + 1;
			fig += figlen + 1;
		}
	}

	return 0;
}
#endif

#if 0
int fic_assemble(unsigned char* rdbuf, unsigned char* ficsyms, unsigned char* rawfibs, FILE *ofp)
{
	unsigned char sym, frame;
	static unsigned char sym_flags, cur_frame;

	sym = *(rdbuf+2);
	frame = *(rdbuf+3);

	if (sym == 2) {
		cur_frame = frame;
		sym_flags = 8;
		memcpy(ficsyms, rdbuf+12, 384 * sizeof(unsigned char));
	} else {
		if (frame == cur_frame) {
			sym_flags |= sym;
			memcpy(ficsyms+(384*(sym-2)),rdbuf+12,384*sizeof(unsigned char));
		} else
			sym_flags = 0;      /* Reset flags - symbols have different frame nos. */

		if (sym_flags == 15) {
			sym_flags = 0;
#if DEBUG > 0
			fprintf(stderr,"Frame = %d\n",frame++);
#endif
			process_fic(ficsyms, rawfibs, ofp);
		}
	}
	return 0;
}
#endif
