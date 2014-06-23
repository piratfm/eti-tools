/* dvbshout - tune.c

   Copyright (C) Dave Chapman 2001,2002
   Copyright (C) Nicholas J Humfrey 2006
  
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <unistd.h>
#include <error.h>
#include <errno.h>

#include <ost/dmx.h> 
#include <ost/frontend.h> 
#include <ost/sec.h> 

#include "tune.h"



static void
print_status (FILE * fd, FrontendStatus festatus)
{
	fprintf (fd, "Frontend Status:");
	if (festatus & FE_HAS_SIGNAL)	fprintf (fd, " FE_HAS_SIGNAL");
	if (festatus & FE_HAS_LOCK)		fprintf (fd, " FE_HAS_LOCK");
	if (festatus & FE_HAS_CARRIER)	fprintf (fd, " FE_HAS_CARRIER");
	if (festatus & FE_HAS_VITERBI)	fprintf (fd, " FE_HAS_VITERBI");
	if (festatus & FE_HAS_SYNC)		fprintf (fd, " FE_HAS_SYNC");
	fprintf (fd, "\n");
}

/* digital satellite equipment control,
 * specification is available from http://www.eutelsat.com/ 
 */
static int
do_diseqc (int fd_sec, int sat_no, int pol, int hi_lo)
{
	struct secCommand sSCmd;
	struct secCmdSequence sSCmdSeq;
	
	
	
	
	sSCmdSeq.continuousTone = (hi_lo) ? SEC_TONE_OFF : SEC_TONE_ON;
	
	sSCmdSeq.voltage = (pol) ? SEC_VOLTAGE_18 : SEC_VOLTAGE_13;
	
	sSCmd.type = 0;
	sSCmd.u.diseqc.addr = 0x10;
	sSCmd.u.diseqc.cmd = 0x38;
	sSCmd.u.diseqc.numParams = 1;
	sSCmd.u.diseqc.params[0] =   0xF0
	                    | ((sat_no * 4) & 0x0F)
	                    | (sSCmdSeq.continuousTone == SEC_TONE_ON ? 1 : 0)
	                    | (sSCmdSeq.voltage == SEC_VOLTAGE_18 ? 2 : 0);
	
	sSCmdSeq.miniCommand = SEC_MINI_NONE;
	sSCmdSeq.numCommands = 1;
	sSCmdSeq.commands = &sSCmd;
	
	if(ioctl(fd_sec, SEC_SEND_SEQUENCE, &sSCmdSeq) < 0) {
		perror ("ERROR set diseqc\n");
		return -1;
	}
	
	return 1;
}

static int
get_status (int fd_frontend, int *ber, int *str, int *snr, int *status)
{
	int32_t strength;

	  strength = 0;
	  if (ioctl (fd_frontend, FE_READ_BER, &strength) < 0) {
		perror ("ERROR reading ber\n");
		return -1;
	  }
	  *ber = strength;

	  strength = 0;
	  if (ioctl (fd_frontend, FE_READ_SIGNAL_STRENGTH, &strength) < 0) {
		perror ("ERROR reading strength\n");
		return -1;
	  }
	  *str = strength;

	  strength = 0;
	  if (ioctl (fd_frontend, FE_READ_SNR, &strength) < 0) {
		perror ("ERROR reading snr\n");
		return -1;
	  }
	  *snr = strength;

	  strength = 0;
	  if (ioctl (fd_frontend, FE_READ_STATUS, &strength) < 0) {
		perror ("ERROR reading status\n");
		return -1;
	  }
	  *status = strength;
	return 0;
}



static int
check_status (int fd_frontend, FrontendParameters *feparams,
	      int tone)
{
	int32_t strength;
	FrontendStatus festatus;
	FrontendInfo fe_info;
	FrontendEvent event;
	struct pollfd pfd[1];
	int status;
	
	if (ioctl (fd_frontend, FE_SET_FRONTEND, feparams) < 0)
	{
		perror ("ERROR tuning channel\n");
		return -1;
	}
	
	pfd[0].fd = fd_frontend;
	pfd[0].events = POLLIN;
	
	if ((status = ioctl (fd_frontend, FE_GET_INFO, &fe_info) < 0))
	{
		perror ("FE_GET_INFO: ");
		return -1;
	}
	
	event.type = 0;
	int polls=0;
	while ((event.type & FE_COMPLETION_EV) == 0)
	{
		fprintf (stderr, "Polling [%d]....\n", polls);
		if (poll (pfd, 1, 10000))
		{
			if (pfd[0].revents & POLLIN)
			{
				fprintf (stderr, "Getting frontend event\n");
				if ((status = ioctl (fd_frontend, FE_GET_EVENT, &event)) < 0)
				{
					if (errno != EBUFFEROVERFLOW) {
						perror ("FE_GET_EVENT");
						fprintf (stderr, "  status = %d\n", status);
						fprintf (stderr, "  errno = %d\n", errno);
						return -1;
					} else {
						fprintf (stderr, 
						"Overflow error, trying again (status = %d, errno = %d)\n",
						status, errno);
					}
				}
				
			}
			
			//int ber=-1,str=-1,snr=-1,status=-1;
			//get_status (fd_frontend, &ber, &str, &snr, &status);
			print_status (stderr, event.u.failureEvent);
		}

		if(polls > 4)
			return -1;
		polls++;
	}
	

	if (event.type & FE_COMPLETION_EV)
	{

		fprintf (stderr, "Gained lock:\n");

		switch (fe_info.type) {
			// DVB-T
			case FE_OFDM:
				fprintf (stderr, "  Frontend Type: OFDM\n");
				fprintf (stderr, "  Frequency: %d Hz\n", event.u.completionEvent.Frequency);
				break;
			
			// DVB-S
			case FE_QPSK:
				fprintf (stderr, "  Frontend Type: QPSK\n");
				fprintf (stderr, "  Frequency: %d kHz\n",
				   (unsigned int) ((event.u.completionEvent.Frequency) +
						   (tone == SEC_TONE_OFF ? LOF1 : LOF2)));
				fprintf (stderr, "  SymbolRate: %d\n", event.u.completionEvent.u.qpsk.SymbolRate);
				fprintf (stderr, "  FEC Inner: %d\n", event.u.completionEvent.u.qpsk.FEC_inner);
				break;
				
			// DVB-C
			case FE_QAM:
				fprintf (stderr, "  Frontend Type: QAM\n");
				fprintf (stderr, "  Frequency: %d Hz\n", event.u.completionEvent.Frequency);
				fprintf (stderr, "  SymbolRate: %d\n", event.u.completionEvent.u.qam.SymbolRate);
				fprintf (stderr, "  FEC Inner: %d\n", event.u.completionEvent.u.qam.FEC_inner);
				break;
			default:
				fprintf (stderr, "  Frontend Type: Unknown\n");
				break;
			}

      //print status here
	int ber=-1,str=-1,snr=-1,status=-1;
	get_status (fd_frontend, &ber, &str, &snr, &status);


	  fprintf (stderr, "  Bit error rate: %d\n", ber);
	  fprintf (stderr, "  Signal strength: %d\n", str);
	  fprintf (stderr, "  SNR: %d\n", snr);
	  print_status (stderr, (FrontendStatus) status);

    } else {
		fprintf(stderr, "Not able to lock to the signal on the given frequency\n");
		return -1;
	}
    
    fprintf(stderr, "\n");
    
	return 0;
}



int
tune_it (int fd_frontend, int fd_sec, dvbshout_tuning_t * set)
{
	int res;
	FrontendParameters feparams;
	FrontendInfo fe_info;
	secVoltage voltage;

	if ((res = ioctl (fd_frontend, FE_GET_INFO, &fe_info) < 0))
	{
		perror ("FE_GET_INFO: ");
		return -1;
	}
	

	fprintf (stderr, "DVB card: hwType=0x%04x hwVersion=0x%04x\n", fe_info.hwType, fe_info.hwVersion);


	switch (fe_info.type)
	{
		// DVB-T
		case FE_OFDM:
			feparams.Frequency = set->frequency;
			feparams.Inversion =  set->inversion;
			feparams.u.ofdm.bandWidth = set->bandwidth;
			feparams.u.ofdm.HP_CodeRate = set->code_rate_hp;
			feparams.u.ofdm.LP_CodeRate =  set->code_rate_lp;
			feparams.u.ofdm.Constellation = set->modulation;
			feparams.u.ofdm.TransmissionMode = set->transmission_mode;
			feparams.u.ofdm.guardInterval = set->guard_interval;
			feparams.u.ofdm.HierarchyInformation = set->hierarchy;
			fprintf (stderr, "Tuning DVB-T to %d Hz\n", set->frequency);
		break;


		// DVB-S
		case FE_QPSK:
			set->frequency *= 1000;
			set->symbol_rate *= 1000;
			
			
			fprintf (stderr, "Tuning DVB-S to %d kHz, Pol:%c Srate=%d, 22kHz=%s\n",
			   set->frequency, set->polarity, set->symbol_rate,
			   set->tone == SEC_TONE_ON ? "on" : "off");
			   
			   
			if ((set->polarity == 'h') || (set->polarity == 'H'))
					voltage = SEC_VOLTAGE_18;
			else	voltage = SEC_VOLTAGE_13;
			
			if (set->diseqc == 0) {
				if (ioctl (fd_sec, SEC_SET_VOLTAGE, voltage) < 0) {
					perror ("ERROR setting voltage\n");
				}
			}

			if (set->frequency > 2200000) {
				// this must be an absolute frequency
				if (set->frequency < SLOF) {
					feparams.Frequency = (set->frequency - LOF1);
					if (set->tone < 0) set->tone = SEC_TONE_OFF;
				} else {
					feparams.Frequency = (set->frequency - LOF2);
					if (set->tone < 0) set->tone = SEC_TONE_ON;
				}
			} else {
				// this is an L-Band frequency
				feparams.Frequency = set->frequency;
			}

			feparams.Inversion = set->inversion;
			feparams.u.qpsk.SymbolRate = set->symbol_rate;
			feparams.u.qpsk.FEC_inner = set->fec_inner;

			if (set->diseqc == 0)
			{
				if (ioctl (fd_sec, SEC_SET_TONE, set->tone) < 0)
					perror ("ERROR setting tone\n");
			}

			if (set->diseqc > 0)
			{
				do_diseqc (fd_sec, set->diseqc - 1, voltage, set->tone);
				sleep (1);
			}
		break;
		
		
		// DVB-C
		case FE_QAM:
			fprintf (stderr, "Tuning DVB-C to %d Hz, srate=%d\n", set->frequency, set->symbol_rate);
			feparams.Frequency = set->frequency;
			feparams.Inversion = set->inversion;
			feparams.u.qam.SymbolRate = set->symbol_rate;
			feparams.u.qam.FEC_inner = set->fec_inner;
			feparams.u.qam.QAM = set->modulation;
		break;
		
		default:
			fprintf (stderr, "Unknown frontend type. Aborting.\n");
			exit (-1);
		break;
    }
    
	usleep (100000);

	return (check_status(fd_frontend, &feparams, set->tone));
}


dvbshout_tuning_t * init_tuning_defaults()
{
	dvbshout_tuning_t *set = malloc( sizeof(dvbshout_tuning_t) );
	
	set->card = 0;
	set->type = 's';
	set->frequency = 0;
	set->polarity = 'v';
	set->symbol_rate = SYMBOLRATE_DEFAULT;
	set->diseqc = DISEQC_DEFAULT;
	set->tone = SEC_TONE_DEFAULT;
	
	set->inversion = INVERSION_DEFAULT;
	set->bandwidth = BANDWIDTH_DEFAULT;
	set->code_rate_hp = CODERATE_HP_DEFAULT;
	set->code_rate_lp = CODERATE_LP_DEFAULT;
	set->fec_inner = FEC_INNER_DEFAULT;
	set->modulation = MODULATON_DEFAULT;
	set->hierarchy = HIERARCHY_DEFAULT;
	set->transmission_mode = TRANSMISSION_MODE_DEFAULT;
	set->guard_interval = GUARD_INTERVAL_DEFAULT;
	
	return set;
}


