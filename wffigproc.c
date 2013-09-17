/*
  wffigproc.c

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
** Extract useful information from FIGs
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "figs.h"
#include "wfbyteops.h"
#include "wfficproc.h"
#include "prot.h"

/* DEBUG:
** 0 - no info
** 1 - print fields for extensions
** 2 - ...and FIG type handler info
** 3 - ...and dump raw bytes
*/
#define DEBUG 0
#define DEBUGd 0

extern struct ens_info einf;

int eid;

/* FIG type handlers */
int fig_0(int, unsigned char*);
int fig_1(int, unsigned char*);
int fig_5(int, unsigned char*);

/* FIG 0 extension handlers */
int fig_0_0(int, int, int, int, unsigned char*);
int fig_0_1(int, int, int, int, unsigned char*);
int fig_0_2(int, int, int, int, unsigned char*);
int fig_0_3(int, int, int, int, unsigned char*);
int fig_0_10(int, int, int, int, unsigned char*);
int fig_0_20(int, int, int, int, unsigned char*);
int fig_0_21(int, int, int, int, unsigned char*);

/* FIG 1 extension handlers */
int fig_1_0(int, int, int, unsigned char*);
int fig_1_1(int, int, int, unsigned char*);
int fig_1_4(int, int, int, unsigned char*);
int fig_1_5(int, int, int, unsigned char*);

/* Null function for types and extensions
   which are currently unimplemented */
int fig_ign(int, unsigned char*);

/* FIG type jump table: pointers to functions handling FIG types */
int (*fig_jtab[])() = { fig_0,   fig_1, fig_ign, fig_ign,
			fig_ign, fig_5, fig_ign, fig_ign
};

/* Jump table: pointers to functions handling FIG Type 0 extensions */
int (*fig_0_jtab[])() = { fig_0_0,  fig_0_1,  fig_0_2,  fig_0_3,
			  fig_ign,  fig_ign,  fig_ign,  fig_ign,
			  fig_ign,  fig_ign,  fig_0_10, fig_ign,
			  fig_ign,  fig_ign,  fig_ign,  fig_ign,
			  fig_ign,  fig_ign,  fig_ign,  fig_ign,
			  fig_0_20, fig_0_21, fig_ign,  fig_ign,
			  fig_ign,  fig_ign,  fig_ign,  fig_ign,
			  fig_ign,  fig_ign,  fig_ign,  fig_ign
};

/* Jump table: pointers to functions handling FIG Type 1 extensions */
int (*fig_1_jtab[])() = { fig_1_0, fig_1_1, fig_ign, fig_ign,
			  fig_1_4, fig_1_5, fig_ign, fig_ign
};

/*
** Ignore this FIG - do nothing
*/
int fig_ign(int figlen, unsigned char *fig)
{
	return 0;
}

/*
** Extract type 0 fields and call function based on extension
*/
int fig_0(int figlen, unsigned char *fig)
{
	struct fig_0 *f;

	f = (struct fig_0*)fig;
#if DEBUG > 2
	int i;
	fprintf(stderr, "fig_0: ");
	for (i=0; i < figlen; i++)
		fprintf(stderr, "%#02x ",*(fig + i));
	fprintf(stderr, "\n");
#endif
#if DEBUG > 0
	fprintf(stderr, "fig_0: figlen=%d extn = %d pd = %d oe = %d cn = %d\n",figlen,f->extn, f->pd, f->oe, f->cn);
#endif
	(*fig_0_jtab[f->extn])(figlen-1, f->pd, f->oe, f->cn, fig + 1);

	return 0;
}

/*
** Extract type 1 fields and call function based on extension
*/
int fig_1(int figlen, unsigned char *fig)
{
	struct fig_1 *f;

	f = (struct fig_1*)fig;

#if DEBUG > 1
	fprintf(stderr, "Type 1 header = %#02x\n",*fig);
	fprintf(stderr, "fig_1 extn = %d oe = %d charset = %d\n",f->extn, f->oe, f->charset);
#endif
	(*fig_1_jtab[f->extn])(figlen-1, f->oe, f->charset, fig + 1);

	return 0;
}

int fig_5(int figlen, unsigned char *fig)
{
#if DEBUG > 0
	fprintf(stderr, "fig_5\n");
#endif
	return 0;
}

/*
** Ensemble information ETSI EN 300 401 V1.3.3 (2001-05), 6.4, P.55
*/
int fig_0_0(int figlen, int pd, int oe, int cn, unsigned char* fig)
{
	unsigned int OccChg;  /* : 8; Value doesn't fit into bitfield */
	short f;
	struct ensinf ei;
	unsigned short EId;

#if DEBUG > 2
	{ int i;
		fprintf(stderr, "fig_0_0: ");
		for (i=0; i < figlen; i++)
			fprintf(stderr, "%#02x ",*(fig + i));
		fprintf(stderr, "\n");
	}
#endif
	EId = spack(fig);
	f = spack(fig + 2);
	memcpy(&ei, &f, sizeof(struct ensinf));

#if DEBUG > 0
	fprintf(stderr, "fig_0_0: EId=%#04x ChgFlg=%d AlrmFlg=%d CIFCntH=%d CIFCntL=%d",
		EId, ei.ChgFlg, ei.AlrmFlg, ei.CIFCntH, ei.CIFCntL);
#endif
	if (ei.ChgFlg > 0) {
		OccChg = *(fig + 4);
#if DEBUG > 0
		fprintf(stderr, " OccChg=%d",OccChg);
#endif
	}
#if DEBUG > 0
	fprintf(stderr, "\n");
#endif
	einf.eid = EId;
	return 0;
}

/*
** Sub-channel organization ETSI EN 300 401 V1.3.3 (2001-05), 6.2, P.43
*/
int fig_0_1(int figlen, int pd, int oe, int cn, unsigned char* fig)
{
	union {
		struct subchorg_l lf;
		struct subchorg_s sf;
	} sl;

	struct subch s;
	int f, i, j;

	i = einf.num_schans;
	j = figlen;
	while (j > 0) {
		f = ipack(fig);
		memcpy(&sl.lf, &f, sizeof(int));
		s.subchid = sl.lf.SubChId;
		s.startaddr = sl.lf.StartAddr;
		s.eepprot = sl.lf.LongForm;
#if DEBUG > 0
		fprintf(stderr, "fig_0_1: lf=%d subchid = %d startaddr = %d ",
			sl.lf.LongForm,s.subchid,s.startaddr);
#endif
		if (sl.lf.LongForm) {
			/* EEP */
			s.subchsz = sl.lf.SubChSz;
			s.protlvl = sl.lf.Opt * 4 + sl.lf.ProtLvl;
			s.bitrate = s.subchsz/eeptable[s.protlvl].sizemul * eeptable[s.protlvl].ratemul;
#if DEBUG > 0
			fprintf(stderr, "subchsz = %d protlvl = %d bitrate = %d\n",
				s.subchsz,s.protlvl,s.bitrate);
#endif
			fig += 4;
			j -= 4;
		} else {
			/* UEP */
			s.subchsz = ueptable[sl.sf.TabIndx].subchsz;
			s.protlvl = ueptable[sl.sf.TabIndx].protlvl;
			s.bitrate = ueptable[sl.sf.TabIndx].bitrate;
			s.uep_indx = sl.sf.TabIndx;
#if DEBUG > 0
			fprintf(stderr, "TabIndx = %d subchsz = %d protlvl = %d bitrate = %d\n",
				sl.sf.TabIndx,s.subchsz,s.protlvl,s.bitrate);
#endif
			fig += 3;
			j -= 3;
		}
/***********************************************************
 * TODO: Subchannel decoded
 ***********************************************************/
		add_subchannel(&einf,&s);
		i++;
	}
	return 0;
}

/*
** Basic service organization ETSI EN 300 401 V1.3.3 (2001-05), 6.3.1, P.47
*/
int fig_0_2(int figlen, int pd, int oe, int cn, unsigned char* fig)
{
	union {
		struct mscstau maup;
		struct mscstdat mdtp;
		struct fidc fp;
		struct mscpktdat mpkp;
	} scdp;

	struct ssid ssidp;
	struct lsid lsidp;

	short s, scmp;
	int f, i, j, k, sid;

#if DEBUG > 2
	fprintf(stderr, "fig_0_2: ");
	for (i=0; i < figlen; i++)
		fprintf(stderr, "%#02x ",*(fig + i));
	fprintf(stderr, "\n");
#endif

	i = einf.num_srvs;
	j = figlen;
	while (j > 0) {
#if DEBUG > 1
		fprintf(stderr, "fig_0_2: j = %d\n",j);
#endif
		if (pd) {
			f = ipack(fig);
			memcpy(&lsidp, &f, sizeof(struct lsid));
#if DEBUG > 0
			fprintf(stderr, "fig_0_2: long SId = %#08x",f);
#endif
			sid = f;
			fig += 4;
			j -= 4;
		} else {
			s = spack(fig);
			memcpy(&ssidp, &s, sizeof(struct ssid));
#if DEBUG > 0
			fprintf(stderr, "fig_0_2: short SId = %#04hx",s);
#endif
			sid = (unsigned short)s;
			fig += 2;
			j -= 2;
		}
#if DEBUG > 0
		fprintf(stderr, " NumSCmp = %d\n",((struct lcn*)fig)->NumSCmp);
#endif
		scmp = ((struct lcn*)fig)->NumSCmp;
		fig++;
		j--;
		for (k=0; k < scmp; k++) {
			s = spack(fig);
			memcpy(&scdp.maup, &s, sizeof(struct mscstau));
			/* Can this be speeded up ? */
			switch (scdp.maup.TMId) {
			case 0:
#if DEBUGd > 0
				fprintf(stderr, "fig_0_2: TMId=%d, ASCTy=%d, SubChId=%d, Primary=%d, CAFlag=%d\n",
					scdp.maup.TMId,scdp.maup.ASCTy,scdp.maup.SubChId,
					scdp.maup.Primary,scdp.maup.CAFlag);
#endif
				/***********************************************************
				 * TODO: Service decoded
				 ***********************************************************/
				add_service(&einf, &scdp.maup, sid);
				break;
			case 1:
#if DEBUGd > 0
				fprintf(stderr, "fig_0_2: TMId=%d, DSCTy=%d, SubChId=%d, Primary=%d, CAFlag=%d\n",
					scdp.mdtp.TMId,scdp.mdtp.DSCTy,scdp.mdtp.SubChId,
					scdp.mdtp.Primary,scdp.mdtp.CAFlag);
#endif
				break;
			case 2:
#if DEBUGd > 0
				fprintf(stderr, "fig_0_2: TMId=%d, DSCTy=%d, FIDCId=%d, Primary=%d, CAFlag=%d\n",
					scdp.fp.TMId,scdp.fp.DSCTy,scdp.fp.FIDCId,
					scdp.fp.Primary,scdp.fp.CAFlag);
#endif
				break;
			case 3:
#if DEBUGd > 0
				fprintf(stderr, "fig_0_2: TMId=%d, SCId=%d, Primary=%d, CAFlag=%d\n",
					scdp.mpkp.TMId,scdp.mpkp.SCId,scdp.mpkp.Primary,
					scdp.mpkp.CAFlag);
#endif
				break;
			default:
				fprintf(stderr, "fig_0_2: error out of range TMId = %d\n",scdp.maup.TMId);
				break;
			}
			fig += 2;
			j -= 2;
		}
		i++;
	}
	return 0;
}

/*
** Service component in packet mode ETSI EN 300 401 V1.3.3 (2001-05), 6.3.2, P.51
*/
int fig_0_3(int figlen, int pd, int oe, int cn, unsigned char* fig)
{
	struct servcomp1 sc1;
	struct servcomp2 sc2;
	int f;

#if DEBUGd > 2
	fprintf(stderr, "fig_0_3: ");
	for (f=0; f < figlen; f++)
		fprintf(stderr, "%#02x ",*(fig + f));
	fprintf(stderr, "\n");
#endif

	f = ipack(fig);
	memcpy(&sc2, &f, sizeof(struct servcomp2));

	fig += 3;
	f = ipack(fig);
	memcpy(&sc1, &f, sizeof(struct servcomp1));

#if DEBUGd > 0
	fprintf(stderr, "fig_0_3: SCId=%d,Rfa=%d,SCCAFlg=%d,DGFlag=%d,Rfu=%d,DSCTy=%d,SubChId=%d,PktAddr=%d,SCCA=%d\n",sc2.SCId,sc2.Rfa,sc2.SCCAFlg,sc2.DGFlag,sc2.Rfu,sc2.DSCTy,sc1.SubChId,sc1.PktAddr,sc1.SCCA);
#endif

	return 0;
}

/*
** Date and time ETSI EN 300 401 V1.3.3 (2001-05), 8.1.3.1, P.96
** Also Conversion between time and date conventions EN 50067:1998 ANNEX G P.81
*/
int fig_0_10(int figlen, int pd, int oe, int cn, unsigned char* fig)
{
	int f, y, m, d, wd;
	short g;
	struct datim dt;
	struct utcl ut;

	f = ipack(fig);
	memcpy(&dt, &f, sizeof(struct datim));

	y = (dt.MJD - 15078.2) / 365.25;
	m = (dt.MJD - 14956.1 - (int)(y * 365.25)) / 30,6001;
	d = dt.MJD - 14956 - (int)(y * 365.25) - (int)(m * 30.6001);
	if ((m == 14)||(m == 15)) {
		y++;
		m -= 13;
	} else
		m--;
	wd = ((dt.MJD + 2) % 7) + 1;
	y += 1900;
#if DEBUG > 0
	fprintf(stderr, "fig_0_10: Rfu=%d MJD=%d(y=%d m=%d d=%d wd=%d) LSI=%d ConfInd=%d UTCFlg=%d UTCHour=%d UTCMin=%d",
		dt.Rfu,dt.MJD,y,m,d,wd,dt.LSI,dt.ConfInd,dt.UTCFlg,dt.UTCHour,dt.UTCMin);
#endif
	if (dt.UTCFlg) {
		g = spack(fig+2);
		memcpy(&ut, &g, sizeof(struct utcl));
#if DEBUG > 0
		fprintf(stderr, " UTCSec=%d UTCmsec=%d",ut.UTCSec,ut.UTCmsec);
#endif
	}
#if DEBUG > 0
	fprintf(stderr, "\n");
#endif
	return 0;
}

/*
** Service component trigger ETSI EN 300 401 V1.3.3 (2001-05), 8.1.7, P.107
*/
int fig_0_20(int figlen, int pd, int oe, int cn, unsigned char* fig)
{
#if DEBUG > 0
	fprintf(stderr, "fig_0_20\n");
#endif
	return 0;
}

/*
** Frequency information  ETSI EN 300 401 V1.3.3 (2001-05), 8.1.8, P.109
*/
int fig_0_21(int figlen, int pd, int oe, int cn, unsigned char* fig)
{
#if DEBUG > 0
	fprintf(stderr, "fig_0_21\n");
#endif
	return 0;
}

/*
** Ensemble label ETSI EN 300 401 V1.3.3 (2001-05), 8.1.13, P.121
*/
int fig_1_0(int figlen,  int oe, int charset, unsigned char* fig)
{
	struct el *f;

	f = (struct el*)fig;

	memcpy(einf.label,f->label, 16 * sizeof(char));
	einf.label[16] = '\0';
	einf.eid = f->eid;
	sswab((short*)&(einf.eid));
	/* fprintf(stderr,"fig_1_0 einf.eid = %#04hx\n",einf.eid); */
#if DEBUG > 0
	fprintf(stderr, "fig_1_0: oe=%d, charset=%d eid=%#04hx label=%s chflag=%#04hx\n",
		oe,charset,einf.eid,einf.label,f->chflag);
#endif
	return 0;
}

/*
**  Programme service label ETSI EN 300 401 V1.3.3 (2001-05), 8.1.14.1, P.121
*/
int fig_1_1(int figlen,  int oe, int charset, unsigned char* fig)
{
	struct {
		short SId;
		char label[17];
		short CharFlgFld;
	} psl;

	struct service *s;

	psl.SId = *(unsigned short*)fig;
	sswab(&psl.SId);
	fig += 2;
	strncpy(psl.label, (char*)fig, 16);
	psl.label[16] = '\0';
	fig += 16;
	psl.CharFlgFld = *(short*)fig; /* Pointless, really */
	sswab(&(psl.CharFlgFld));
#if DEBUG > 0
	fprintf(stderr, "fig_1_1: SId=%#04hx, label=%s chflag=%#04hx\n",psl.SId,psl.label,psl.CharFlgFld);
#endif
	if ((s = find_service(&einf, (unsigned short)psl.SId)) != NULL)
		strncpy(s->label, psl.label, 17);
	return 0;
}

/*
** Service component label ETSI EN 300 401 V1.3.3 (2001-05), 8.1.14.3, P.122
*/
int fig_1_4(int figlen,  int oe, int charset, unsigned char* fig)
{
	struct scl s;
	struct sclf *p;

#if DEBUG > 2
	{ int i;
		fprintf(stderr, "fig_1_4: ");
		for (i=0; i < (figlen+1); i++)
			fprintf(stderr, "%02x ",*(fig + i));
		fprintf(stderr, "\n");
	}
#endif

	p = (struct sclf*)fig;
	s.SCIds = p->SCIds;
	s.Rfa = p->Rfa;
	s.PD = p->PD;
	fig++;
	if (p->PD) {
		s.SId = *(int*)fig;
		fig += 4;
	} else {
		s.SId = *(short*)fig;
		fig += 2;
	}
	iswab(&(s.SId));
	strncpy(s.label, (char*)fig, 16);
	s.label[16] = '\0';
	fig += 16;
	s.CharFlgFld = *(short*)fig;
	sswab(&(s.CharFlgFld));
#if DEBUG > 0
	fprintf(stderr, "fig_1_4: PD=%d Rfa=%d SCIds=%d SId=%#04hx label=%s chflag=%#04hx\n",
		s.PD,s.Rfa,s.SCIds,s.SId,s.label,s.CharFlgFld);
#endif
	return 0;
}

/*
** Data service label ETSI EN 300 401 V1.3.3 (2001-05), 8.1.14.2, P.122
*/
int fig_1_5(int figlen,  int oe, int charset, unsigned char* fig)
{
	struct dsl *f;

	f = (struct dsl*)fig;
	f->label[16] = '\0';
#if DEBUG > 0
	fprintf(stderr, "fig_1_5: SId=%#08hx, label=%s chflag=%#04hx\n",f->SId,f->label,f->CharFlgFld);
#endif
	return 0;
}

/*
** I think 'parse' isn't quite the word
** so here FIGs are 'unpicked'
*/
int unpickfig(unsigned char* fig, int figlen)
{
	int len, typ;
	struct fig_hdr *f;
#if DEBUG > 1
	int i;
	fprintf(stderr, "unpickfig: ");
	for (i=0; i < (figlen+1); i++)
		fprintf(stderr, "%02x ",*(fig + i));
	fprintf(stderr, "\n");
#endif
	typ = (*fig & 0xe0) >> 5;
	len = *fig & 0x1f;
	f = (struct fig_hdr*)fig;

	/* Check if using bitfields to extract values is ok */
	if ((len != figlen) || (typ != ((struct fig_hdr*)fig)->ftype)) {
		fprintf(stderr, "Bitfield extracted values and masked/shifted values differ - fix\n");
		fprintf(stderr, "Bitfield: type = %d length = %d Mask: type = %d length = %d\n",
			f->ftype,f->flen,typ,len);
	}
#if DEBUG > 1
	fprintf(stderr, "unpickfig: fig type = %d fig length = %d\n",f->ftype,f->flen);
#endif
	(*fig_jtab[f->ftype])(figlen, fig + 1);

	return 0;
}
