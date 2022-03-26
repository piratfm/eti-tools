eti-tools
============

This is a software collection for converting [Ensemble Transport Interface](http://wiki.opendigitalradio.org/Ensemble_Transport_Interface) used in terrestrial Digital Audio Broadcasting (DAB/DAB+/T-DMB).

The main purpose of these apps is to convert/manipulate ETI-NA/ETI-NI streams (by using pipelines). With these software tools you can create your own IceCast2 internet-radio server which will use your local DAB/DAB+ transmitter as source for the stations streams. You also can re-multiplex some (needed) stations from one ETI-stream to another by using ZeroMQ feature of the ni2http application and [ODR-DabMUX](https://github.com/Opendigitalradio/ODR-DabMux). 

This software also allows to receive and convert special formatted [Satellite DAB(+) streams](#satellite-dab-feeds) (so-called feeds) into regular ETI-NI which then can be used to play in [dablin](https://github.com/Opendigitalradio/dablin) or even feed modulator software/hardware (check local laws!) or to create internet-station from that source.


Table of Content
----------------

* [Prerequisitions](#prerequisitions)
* [Installation](#installation)
* Tools
  * [ts2na](#ts2na)
  * [na2ni](#eti-na2ni)
  * [edi2eti](#eti-edi2eti)
  * [fedi2eti](#eti-fedi2eti)
  * [bbfedi2eti](#eti-bbfedi2eti)
  * [mpe2ts, mpe2aac, mpe2mpa](#mpe2ts-mpe2aac-mpe2mpa)
  * [eti2zmq](#eti-eti2zmq)
  * [ni2out](#eti-ni2out)
* [Satellite DAB(+) feeds](#satellite-dab-feeds)
  * [Guide](#guide)
    * [dvbstream](#dvbstream)
    * [dvblast](#dvblast)
    * [tune-s2 and dvbstream](#tune-s2-and-dvbstream)
    * [tsduck](#tsduck)

    * [Sat>IP](#alternative) (unmaintained)


Prerequisitions
----------------

Additional libraries which are needed:

* git

  For Debian (incl. Ubuntu and derivates)
  
      sudo apt-get install git

* cmake (needed for libfec)

  For Debian (incl. Ubuntu and derivates)

      sudo apt-get install cmake


* [libfec](https://github.com/Opendigitalradio/ka9q-fec) - for Reed-Solomon FEC, may be enabled/disabled.

      git clone https://github.com/Opendigitalradio/ka9q-fec.git
      cd ka9q-fec/
      mkdir build
      cd build/
      cmake ..
      make
      sudo make install
      
   To clean the build directory type
       
      rm -rf *

* [libshout](http://www.icecast.org/download.php) - for ni2out converter (included in this package library is modified to support aac and raw streaming).
  
  For Debian (incl. Ubuntu and derivates)

      sudo apt-get install icecast2

  and follow the instructions

* [libzmq](http://zeromq.org) - optional: for ZeroMQ output of ni2out-converter (possible re-mux of ETI-streams containing DAB/DAB+ streams). Uncomment it in `Makefile` if this option is needed (see below).

  For Debian (incl. Ubuntu and derivates)

      sudo apt-get install libzmq3-dev


Installation
----------------
    
    git clone https://github.com/piratfm/eti-tools.git
    cd eti-tools/

If you need to enable ZeroMQ (see above): Make sure you have installed ZeroMQ and edit `Makefile` and uncomment (= remove`#` in front of) lines 23 and 24, then

    make
    sudo make install

In case you get this error 

> error while loading shared libraries: libfec.so.3

then refresh the library cache:

    sudo ldconfig

ts2na
----------------

**ts2na** is an MPEG-TS to ETI-NA converter for satellite DAB(+) feeds.

**ts2na_dreambox.c** is a special version for Dreambox DM-500S which can be used to tune the frontend to a specific frequency. On a regular PC use [dvbstream](https://www.linuxtv.org/wiki/index.php/Dvbstream) or [MuMuDVB](https://www.linuxtv.org/wiki/index.php/Mumudvb) application to dump to `ts2na`.

    usage: ./ts2na [-p pid] [-s offset] [-i <inputfile>] [-o <outputfile>]

Default for `offset` is 12 bytes. If you get 

    ERROR: Can't find sync

try one of these (currently in Europe used) values for `offset`: 0, 12 or -3. 

Default for `pid` is 1062. Values for `pid` can be any other PID carrying an ETI-NA stream (e.g. 1061). In case of negative offset (`-s -3`, see above) this `pid` argument will be ignored as the DVB-S stream itself is no valid transport stream.

The output stream will be raw PID content = ETI-NA (G.704). The parameter [-s offset] must be seen in an MPEG-TS dump, in most cases that is unused 0xFF at the beginning of the each TS-packet's payload.


ETI na2ni
----------------

**na2ni** is an ETI-NA (G.704) to ETI-NI (G.703) converter. This tool automatically detects E1-sync bits in bitstream and inversion flag. Also it extracts ETI-LI content of the stream and encapsulate it into ETI-NI frames. It is possible to disable Reed-Solomon error correction, then the conversion speed will be dramatically increased.

    usage: ./na2ni [--no-fec] [-i <inputfile>] [-o <outputfile>]

The output stream will be 6144-bytes aligned raw ETI-NI stream (G.703)


ETI edi2eti
----------------

**edi2eti** is an EDI-AF or EDI-PF to ETI-NI converter. This tool automatically detects the AF/PF packet type. Maximal deinterleaving depth is set to 192ms in order to prevent high memory usage. It also extracts an ETI-LI content of the stream and encapsulate it into ETI-NI frames.

The tool is able to receive multicast data and save the converted stream into an ETI-file or publish it by ZeroMQ protcol. It is written to convert microwave links (WiFi or raw packet stream) to tcp-ZeroMQ stream useable by [EasyDABv2 module](http://tipok.org.ua/node/46). It can also be used for satellite feeds on Eutelsat 7° East.

    usage: ./edi2eti [-o <outputfile|zeromq-uri>] [ip:port]

The output stream will be 6144-bytes aligned raw ETI-NI stream or local-port published ZeroMQ packet.

Sample of receiving multicast stream and convert it to ZeroMQ:

    ./edi2eti -o "zmq+tcp://*:18982"  232.20.10.1:12000

Sample of receiving multicast stream and save it to file:

    ./edi2eti -o "out.eti"  232.20.10.1:12000
    
ETI fedi2eti
----------------
**fedi2eti** is similar to edi2eti (see above), but it reads the input from a recorded transport stream file or stream and does not require a dvbnet connection.

    usage: [input from file or stream] | ./fedi2eti 101 239.16.242.17 60017 | [output to dablin or ODR-DabMux]

If you want to input from a recorded file (for instance with the PID 101 from the German EDI transponder) use

    cat foo.ts

ETI bbfedi2eti
----------------
**bbfedi2eti** is similar to edi2eti (see above), but it reads the input from a recorded baseband stream or stream (each bbframe starting with an added 0xB8 byte) that contain GSE UDP ipv4 data.

    usage: [input from file or stream] | ./bbfedi2eti -dst-ip 239.199.2.8 -dst-port 60017 | [output to dablin or ODR-DabMux]

If you want to input from a recorded file use

    cat foo.bbf

mpe2ts, mpe2aac, mpe2mpa
------------------------

These are small tools to extract UDP radio streams via satellite. 

     usage: [input from file or stream] | ./mpe2aac [PID] [IP] [Port] | [output to e.g. vlc or mplayer] 

ETI eti2zmq
----------------

**eti2zmq** is an ETI-NI to ZeroMQ converter. This tool plays a ETI-file and publishes it as server, just like [ODR-DabMux](https://github.com/Opendigitalradio/ODR-DabMux). It simulates pseudo-realtime streaming by adding a proper delay between sent frames. It also allows to play files in a loop.

    usage: ./eti2zmq [-i <input-file.eti>] -o <zeromq-uri>

The input stream must be 6144-bytes aligned raw ETI-NI.

Sample of playing "foo.eti" file in-a-loop with pseudo-realtime streaming and app's activity indication:

    ./eti2zmq -i foo.eti -a -l -d -o "zmq+tcp://*:18982"


ETI ni2out
----------------------

**ni2out** (formerly called **ni2http**) is an ETI-NI converter. This tool converts an eti-stream to mp2 resp. AAC.

ETI-NI streams from terrestrial DAB(+) ensembles can also be created by [eti-stuff](https://github.com/JvanKatwijk/eti-stuff) or [dabtools](https://github.com/Opendigitalradio/dabtools). For satellite feeds see above.

    usage: ./ni2out [--list] [--delay] [-i <inputfile>] [-s <SID>]

Use `--list` option to find SIDs and station names inside the ETI stream. If you wish to write the stream to stdout, then use `ni2out --sid <SID>`. 
The `--delay` option has to be used for offline-relaying (from the file, not from the stream). So in that case the application will wait 24ms after each ETI frame in order to make pseudo-realtime streaming.

To get a list of audio service IDs, use `ni2out --list -i <inputfile>`


Satellite DAB(+) feeds
----------------

This is an exclusive list of satellite feeds that you can use with `ts2na`, `edi2eti` (both from these `eti-tools`) or with [tsniv2ni](https://github.com/newspaperman/tsniv2ni) which works for ETI-NA(V.11).

Please note that you need `eti-tools` from June 2018 or later for EDI.

### DAB-Ensembles working with eti-tools

The format is MPEG-TS, which you have to convert into ETI-NA and then to ETI-NI.

Ensemble | Country | Sat | Freq | SR/FEC | Modulation | PID | SID | Offset
-- | -- | -- | -- | -- | -- | -- | -- | -- 
WDR 11D | Germany | 23.5ºE | 12645V | 1489 3/4 | QPSK/DVB-S | 8192 | -- | -3
BBC DAB | UK | 28.2ºE | 11425H | 27500 2/3 | QPSK/DVB-S | 1061 | 10580 | 12
D1 DAB | UK | 28.2ºE | 11425H | 27500 2/3 | QPSK/DVB-S | 1062 | 10585 | 12
SDL NATL | UK  | 28.2ºE | 11425H | 27500 2/3 | QPSK/DVB-S | 1063 | 10590 | 12
D1 DAB | UK | 9.0ºE | 12092H | 27500 3/4 | 8PSK/DVB-S2 | 1062 | 1165 | 12
North Yorkshire | UK | 9.0ºE | 12092H | 27500 3/4 | 8PSK/DVB-S2 | 1065 | 1215 | 0
SDL NATL | UK  | 9.0ºE | 12092H | 27500 3/4 | 8PSK/DVB-S2 | 1063 | 1170 | 12
RAI DAB+ | Italy | 5.0°W | 11013V | 35291 2/3 | 8PSK/DVB-S2 ACM Multistream 11 PLS: Root/16416 or PLS: Gold/131070 | 1000 | -- | 0
TRT DAB+ | Turkey | 42.0E | 10953V | 1800 3/4 | QPSK/DVB-S | 1068 | -- | 0 

Notes: 

- TRT DAB+ is already an NI stream! There is no need to use `na2ni` in this case! 
- For WDR you need to stream/save the complete transponder (PID 8192) as they don't have a transport stream. 
- The RAI DAB+ only can received with a receiver/DVB card supporting ACM Multistream and higher SR. 
- The mentioned transponder on Astra 28.2 East is the UK Spotbeam.
- The UK streams on 4.8E have been switched off by 1st of Jan 2021.
  

### DAB-Ensembles working with eti-tools

The format is EDI. 

#### Germany ####
7.0ºE, 12567V, Symbol rate 17015, FEC 2/3 in QPSK/DVB-S2 with PID 101 (which contains 15 ensembles in total).

Remark: The symbol rate and the frequency has changed over the past months after adding additional muxes.

Ensemble | IP-Address:Port
-- | --
SWR BW S (Baden Württemberg South, 8A and 8D) | 239.132.1.50:5004
SWR BW N (Baden Württemberg North, 9D) | 239.132.1.51:5004
Rheinland-Pfalz (11A) | 239.132.1.52:5004
Oberfranken (10B)  | 239.16.242.11:60011
Unterfranken (10A) | 239.16.242.13:60013
Oberpfalz (6C) | 239.16.242.14:60014
Niederbayern (7D) | 239.16.242.16:60016
Bayern (11D) | 239.16.242.17:60017
Obb/Schw (Oberbayern and Schwaben, 10A) | 239.16.242.15:60015
hr radio Hessen (7B) | 239.192.254.200:10000
NDR NDS HAN (Niedersachsen, Hannover, 7D) | 239.229.96.38:50000
NDR NDS BS (Niedersachsen, Braunschweig 11B) | 239.229.96.42:50000
NDR MV SN (Mecklenburg-Vorpommern, Schwerin 12B) | 239.229.96.43:50000
Allgäu (8B) | 239.128.57.20:50020
Voralpen (7A) | 239.128.58.20:50020

:information_source: Hint: If you get regular error like that

`[date and time] EDI: Unknown TAG Fptt`

on Allgäumux or Voralpenmux, then you can avoid this by redirecting the output to the null device:

`fedi2eti 101 239.128.57.20 50020 2> /dev/null | dablin_gtk `

#### France ####

5°W, 11461H Symbol rate 5780 FEC 2/3 in QPSK/DVB-S2 with PID 301

Ensemble | IP-Address:Port
-- | --
Métropolitain 1 | 239.0.1.11:5001
Métropolitain 2 | 239.0.1.12:5002

#### UK ####

10°E, 11221V Symbol rate 30000 FEC 5/6 in QPSK/DVB-S2 with PID 701

Ensemble | IP-Address:Port
-- | --
Salisbury | 239.232.1.201:4000


### DAB-Ensembles in DVB-GSE

The format is EDI, but the reception is limited to very few (professional) equipment containing an STiD135 chip, like TBS 6903-X or Digital Devices Cine S2 V7A (both for PCIe only), as this is [DVB-GSE](https://www.dvb.org/standards/dvb-gse). 

Note: For TBS 6903-X you need to tune the signal in Linux as the Windows driver is buggy.

| :warning: All other cards using a different chip (including the popular TBS 5927) **cannot** handle the bbframes at all and will **not** work for GSE streams. You might only get some fragments, but not a continuous data stream. |
| --- |

For processing you need **pts2bbf** from https://github.com/newspaperman/bbframe-tools (see Readme there) and **bbfedi2eti** from this repository.

#### Norway ####
1.0ºW, 10720V, SR 5400, FEC 3/4 in DVB-S2/8PSK, MIS=171 DVB-GSE 

Ensemble|IP-Address:Port
--|--
NRK Reg1 OsloVik | 239.199.2.1:1234
NRK Reg2 VeTeVik | 239.199.2.2:1234
NRK Reg3 SørRog | 239.199.2.3:1234
NRK Reg4 Vest | 239.199.2.4:1234
NRK Reg5 Innland | 239.199.2.5:1234
NRK Reg6 TrøMøRo | 239.199.2.6:1234
NRK Reg7 NoTrFi | 239.199.2.7:1234
Riks (12D) | 239.199.2.8:1234


#### Germany ####
23.5°E, 12641V, SR 1500, FEC 2/3 in DVB-S2/8PSK, multistream, DVB-GSE

Ensemble| MIS | IP-Address:Port
--|--|--
Bundesmux | MIS 1 | 239.128.43.43:50043
Bundesmux 2 | MIS 2 | 239.128.72.10:50010

:information_source: Hint: If you get regular error like that

`[date and time] EDI: Unknown TAG Fptt`

on Bundesmux 2, then you can avoid this by redirecting the output to the null device:

`bbfedi2eti -dst-ip 239.128.72.10 -dst-port 50010 2> /dev/null | dablin_gtk`

### Ensembles working with tsniv2ni

The format is ETI-NA(V.11)

Ensemble | Country | Sat | Freq | SR/FEC | Modulation | PID 
-- | -- | -- | -- | -- | -- | -- 
ERT DAB | Greece | 39ºE | 12242H | 13380 3/4 | QPSK/DVB-S2 | 1010
DAB Italia | Italy | 9ºE | 12034V | 27500 3/4 | 8PSK/DVB-S2 | 777 
EuroDAB Italia | Italy | 9ºE | 12034V | 27500 3/4 | 8PSK/DVB-S2 | 1025 


## Guide

If you want to listen to one of these feeds, here's a guide how to do it (see below for some examples):

### dvbstream 

    dvbstream -f 12092000 -s 27500 1063 -p H -o | ts2na -s 12 -p 1063 | na2ni | ni2out --list
    
for UK's SDL National Mux on 9°E or

    dvbstream -f 12242000 -s 13380 1010 -p H -o | tsniv2ni 1010 | ni2out --list
    
for the Greek Mux on 39°E.

Please consider to add `-D x` (which stands for DiSEqC) if you have more than one LNB.

You can hear the German EDI streams even without setting up a DVB network connection with `fedi2eti`:

    dvbstream -f 12567000 -s 17015 101 -p V -o | fedi2eti 101 239.16.242.17 60017 | dablin_gtk

for the Bayern Mux and output it to [dablin_gtk](https://github.com/Opendigitalradio/dablin).

### dvblast

    dvblast -s 5400000 -v 13 -f 10720000 -m psk_8 -3 -a 0 -1 171 -u > /tmp/nrk.ts

for the NRK Transponder using a suitable card (like Cine V7A or TBS6903x) and save the output into a file.

    dvblast -s 35291000 -v 13 -f 11013000 -m psk_8 -3 -a 0 --multistream-id-is-id 11 -u --multistream-id-pls-mode GOLD --multistream-id-pls-code 131070 | dd if=/dev/stdin skip=188 | ts2na -p 1000 -s 0 | na2ni | dablin_gtk -L

for the multistream transponder of RAI using Adapter 0 and piping to `dablin_gtk` with option `-L`. 

Remark: The first TS frame (188 bytes) is skipped in this example as the header might be corrupt otherwise.

### tune-s2 and dvbstream

    tune-s2 10720 V 5400 -system DVB-S2 -modulation 8PSK -fec 3/4 -lnb UNIVERSAL -mis 171
    
and in a second console  

    dvbstream -o 8192 > /tmp/nrk.ts
    
for the NRK transponder and save its output to a file.


    tune-s2 11461 H 5780 -fec 2/3 -modulation QPSK S2 -lnb UNIVERSAL
    
and in a second console

    dvbstream -o 301 | fedi2eti 301 239.0.1.11 5001 | dablin_gtk 
    
to tune to the French transponder on 5°W, then stream PID 301, extract one DAB mux (which is in UDP) with `fedi2eti` and listen to it in `dablin_gtk`.
    

### tsduck

    tsp -I dvb -a 2 --delivery-system DVB-S2 --fec-inner 2/3 --frequency 11013000000 --isi 11 --modulation 8-PSK  --pls-code 131070 --pls-mode GOLD --polarity vertical --symbol-rate 35291000 | dd if=/dev/stdin skip=188 | ts2na -p 1000 -s 0 | na2ni | dablin_gtk

for the multistream transponder of RAI on 5°W using Adapter 2 and listen in `dablin_gtk`. 

Remark: The first TS frame (188 bytes) is skipped in this example as the header might be corrupt otherwise.

### Alternative

- **Source**: You need to _capture_ the feed with a SAT tuner. Our recomendation is to use one of them to stream the feed to a multicast address. Then you can use this stream from any computer on your network (not only the one with the SAT tuner).
  - If your SAT tuner is a SAT>IP server, then you can use this URI for getting an MPEG-TS with the three DAB bitstreams present in the MUX of 4.9ºE:
    - `satip://server:554/?src=1&freq=12303&pol=h&msys=dvbs&mtype=qpsk&sr=25546&fec=78 &pids=0,1,16,17,18,20,1061,1062,1063,5060,5070,5080"`
- **Unpacking**: You need to _process_ the multicast stream to obtain the ETI DAB ensemble. You can do this on any computer on your network. The input must be the MPEG-TS and the output will be the ETI-NI bitstream. You can save it in a file, FIFO or PIPE. Required tools: `SOCAT`, `ts2na` & `na2ni`.
- **Play**: Finally you need to _reproduce_ the DAB ensemble. You can use any DAB player with ETI-NI input support (for example DABlin).

Here an "all-in-one" example:

- **Producer**: Using the DVBlast tool in a computer with a DVB-S tuner, for streaming all three DAB ensembles from 4.9ºE in 12303-H, to the multicast address udp://@239.1.1.10:1234 from the source address 192.168.1.33 (IP of this computer):
  - `dvblast -f 12303000 -s 25546000 -v 18 -S 1 -d "239.1.1.10:5018@192.168.1.33/udp 1 0 0,1,16,17,18,20,1061,1062,1063,5060,5070,5080"`
- **Consumer**: Use DABlin to consume the MPEG-TS from udp://@239.1.1.10:1234 and tune the ensemble "BBC DAB" at pid 1061:
  - `socat UDP4-RECV:5018,bind=239.1.1.10,ip-add-membership=239.1.1.10:eth0,reuseaddr - | ts2na -p 1061 -s 12 | na2ni | dablin -p`

