/* dvb_defaults.h

   Copyright (C) Nicholas Humfrey 2006, Dave Chapman 2002

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
   Or, point your browser to http://www.gnu.org/copyleft/gpl.html

*/

#ifndef _DVB_SHOUT_H
#define _DVB_SHOUT_H

#include <inttypes.h>
#include <netinet/in.h>

// DVB includes:
#include <ost/dmx.h>
#include <ost/frontend.h>
#include <ost/audio.h>

/* DVB-S */

// With a diseqc system you may need different values per LNB.  I hope
// no-one ever asks for that :-)

#define SLOF (11700*1000UL)
#define LOF1 (9750*1000UL)
#define LOF2 (10600*1000UL)

#define SYMBOLRATE_DEFAULT 			(27500)
#define DISEQC_DEFAULT 				(0)
#define SEC_TONE_DEFAULT 			(-1)


/* DVB-T */

/* Defaults are for the United Kingdom */
#define INVERSION_DEFAULT			INVERSION_AUTO
#define BANDWIDTH_DEFAULT           BANDWIDTH_8_MHZ
#define CODERATE_HP_DEFAULT        	FEC_3_4
#define CODERATE_LP_DEFAULT        	FEC_3_4
#define FEC_INNER_DEFAULT	        FEC_AUTO
#define MODULATON_DEFAULT     		QAM_16
#define HIERARCHY_DEFAULT           HIERARCHY_NONE
#define TRANSMISSION_MODE_DEFAULT   TRANSMISSION_MODE_2K
#define GUARD_INTERVAL_DEFAULT      GUARD_INTERVAL_1_32

/* Structure containing tuning settings 
   - not all fields are used for every interface
*/
typedef struct dvbshout_tuning_s {

	unsigned char card;			// Card number
	unsigned char type;			// Card type (s/c/t)
	
	unsigned int frequency;		// Frequency (Hz) (kHz for QPSK)
	unsigned char polarity;		// Polarity
	unsigned int symbol_rate;	// Symbols per Second (Hz)
	unsigned int diseqc;
	int tone;					// 22kHz tone (-1 = auto)
	
	SpectralInversion  inversion;
	BandWidth  bandwidth;
	CodeRate  code_rate_hp;
	CodeRate  code_rate_lp;
	CodeRate  fec_inner;
	Modulation  modulation;
	Hierarchy  hierarchy;
	TransmitMode  transmission_mode;
	GuardInterval  guard_interval;
} dvbshout_tuning_t;

extern dvbshout_tuning_t *dvbshout_tuning;
/* In tune.c */
int tune_it(int fd_frontend, int fd_sec, dvbshout_tuning_t *set);
dvbshout_tuning_t * init_tuning_defaults();

#endif
