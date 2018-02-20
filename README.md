eti-tools
============

This is a software collection for converting [Ensemble Transport Interface](http://wiki.opendigitalradio.org/Ensemble_Transport_Interface) used in terrestrial Digital Audio Broadcasting (DAB/DAB+/T-DMB).

The main purpose of these apps is to convert/manupulate ETI-NA/ETI-NI streams (by using pipelines). With these software tools you can create your own IceCast2 internet-radio server which will use your local DAB/DAB+ transmitter as source for the stations streams. You also can re-multiplex some (needed) stations from one ETI-stream to another by using ZeroMQ feature of the ni2http application and [ODR-DabMUX](https://github.com/Opendigitalradio/ODR-DabMux). 

This software also allows to receive and convert special formatted satellite DAB(+) streams (so-called feeds) into regular ETI-NI which then can be used to play in [dablin](https://github.com/Opendigitalradio/dablin) or even feed modulator software/hardware (check local laws!) or to create internet-station from that source.


Table of Content
----------------

* [Prerequisitions](#prerequisitions)
* Tools
  * [ts2na](#ts2na)
  * [na2ni](#eti-na2ni)
  * [edi2eti](#eti-edi2eti)
  * [ni2http](#eti-ni2http)


Prerequisitions
----------------

Additional libraries are needed:
* [libfec](https://github.com/Opendigitalradio/ka9q-fec) - for Reed-Solomon FEC, may be enabled/disabled.
* [libshout](http://www.icecast.org/download.php) - for NI-to-HTTP converter (included in this package library is modified to support aac and raw streaming).
* [libzmq](http://zeromq.org) - optional: for ZeroMQ output of NI-to-HTTP converter (possible re-mux of ETI-streams containing DAB/DAB+ streams). Uncomment it in Makefile if this option is needed.


ts2na
----------------

**ts2na** is a MPEG-TS to ETI-NA converter for satellite DAB(+) feeds.

**ts2na_dreambox.c** is a special version for Dreambox DM-500S which can be used to tune frontend to specific frequency. On regular PC use [dvbstream](https://www.linuxtv.org/wiki/index.php/Dvbstream) or [MuMuDVB](https://www.linuxtv.org/wiki/index.php/Mumudvb) application to dump to ts2na.

    usage: ./ts2na [-p pid] [-s offset] [-i <inputfile>] [-o <outputfile>]

Default for `offset` is 12 bytes. If you get 

    ERROR: Can't find sync

try one of these (currently in Europe used) values for `offset`: 0, 12 or -3. 

Default for `pid` is 1062. Values for `pid` can be any other PID carrying an ETI-NA stream (e.g. 1061) . In case of negative offset (`-s -3`, see above) this `pid` argument will be ignored as the DVB-S stream itself is no valid transport stream.

The output stream will be raw PID content = ETI-NA (G.704). The parameter [-s offset] must be seen in MPEG-TS dump, in most cases that is unused 0xFF at the beginning of the each TS-packet's payload.


ETI na2ni
----------------

**na2ni** is a ETI-NA (G.704) to ETI-NI (G.703) converter. This tool automatically detects E1-sync bits in bitstream and inversion flag. Also it extracts ETI-LI content of the stream and incapsulate it into ETI-NI frames. It is possible to disable Reed-Solomon error correction, then conversion speed will be dramatically increased.

    usage: ./na2ni [--no-fec] [-i <inputfile>] [-o <outputfile>]

The output stream will be 6144-bytes aligned raw ETI-NI stream (G.703)


ETI edi2eti
----------------

**edi2eti** is a EDI-AF or EDI-PF to ETI-NI converter. This tool automatically detects type of AF/PF packet type. Maximal deinterleaving depth is set to 192ms to prevent high memory usage. Also it extracts ETI-LI content of the stream and incapsulate it into ETI-NI frames.

The tool is able to receive multicast data and save converted stream ETI-file or publish it by ZeroMQ protcol. It is written to convert microwave links (WiFi or raw packet stream) to tcp-zeromq stream useable by EasyDABv2 module.

    usage: ./edi2eti [-o <outputfile|zeromq-uri>] [ip:port]

The output stream will be 6144-bytes aligned raw ETI-NI stream or local-port published ZeroMQ packet.

Sample of receiving multicast stream and convert it to ZeroMQ:

    ./edi2eti -o "zmq+tcp://*:18982"  232.20.10.1:12000

Sample of receiving multicast stream and save it to file:

    ./edi2eti -o "out.eti"  232.20.10.1:12000


ETI ni2http
----------------

**ni2http** is a ETI-NI to HTTP or ZeroMQ converter. This tool converts eti-stream to mp2 resp. aac and relays it to icecast2 or ODR-DabMUX server.

ETI-NI streams from terrestrial DAB ensembles can also be created by [eti-stuff](https://github.com/JvanKatwijk/eti-stuff) or [dabtools](https://github.com/Opendigitalradio/dabtools). For satellite feeds see above.

    usage: ./ni2http [--list] [--delay] [-i <inputfile>] [-c <config_file>]

Use `--list` option to find SIDs and station names of the streams inside ETI.
The `--delay` option has to be used for offline-relaying (from the file, not from the stream). So in that case application will wait 24ms after each eti frame in order to make pseudo-realtime streaming.

The application is also able to parse FIC for auto-detecting of station name and X-PAD of DAB and DAB+ for setting current DLS (song titles).

Config sample:

    [server]
    host:       localhost
    port:       8000
    user:       source
    password:   hackme
    
    [channel]
    mount:      r5_live
    sid:        0xc228
    
    [channel]
    name:       Custom channel name2
    mount:      r1
    sid:        0xc221
    
    [channel]
    #stream name will be auto-detected
    #name:       Custom channel name3
    mount:      r6music
    sid:        0xc22b
    # extract_pad - use DLS info as icecast metadata, enabled by default
    extract_pad: 1
    #extract_dabplus - converts DAB+ stream into AAC-ADTS,
    #which is playable by the internet-radio players, enabled by default
    #If this option is disabled, this stream can be directly passed to ODR-DabMod
    extract_dabplus: 1
    
    [channel]
    # just write to file, no streaming to server.
    sid:        0xc223
    extract_pad: 0
    file:       /run/station7.fifo

    [channel]
    # pass DAB/DAB+ stream to ODR-DabMUX.
    sid:        0xc224
    extract_pad: 0
    zmq:       tcp://127.0.0.1:9001

In `[server]` section the parameters of Icecast2 server must be set.
In `[channel]` sections at least service ID of the channel must be presented.

If you wish to write stream to a file, then use `file` to specify its location. If stream to Icecast2 server is needed, then specify mount-point on the icecast server.

If you wish to re-stream to ODR-DabMUX then set destination of the ZeroMQ URI to muxing server. To get list of service IDs, use `ni2http --list -i <inputfile>`

Satellite DAB(+) feeds
----------------

This is a list of satellite feeds that you can use with the tool `ts2na`:

Stream Name | Sat | Freq | SR/FEC | Modulation | PID | SID | Check
-- | -- | -- | -- | -- | -- | -- | --
D1 DAB | 9.0ºE | 11727V | 27500 3/4 | QPSK/DVB-S | 1062 | ?? | Pending
SDL nATL | 9.0ºE | 11727V | 27500 3/4 | QPSK/DVB-S | 1063 | ?? | Pending
BBC DAB | 4.5ºE | 12303H | 25546 7/8 | QPSK/DVB-S | 1061 | 70 | OK
D1 DAB | 4.5ºE | 12303H | 25546 7/8 | QPSK/DVB-S | 1062 | 60 | OK
SDL NATL | 4.5ºE | 12303H | 25546 7/8 | QPSK/DVB-S | 1063 | 80 | OK
  |   |   |   |   |   |   |  

If you want to use one of these feeds, here's an example of how to do it:

- **Source**: Use one SAT tuner to stream the feed to some multicast address.
- - Example with SAT>IP for getting an MPEG-TS with the three DAB bitstreams present in the MUX:
- - `satip://server:554/?src=1&freq=12303&pol=h&msys=dvbs&mtype=qpsk&sr=25546&fec=78 &pids=0,1,16,17,18,20,1061,1062,1063,5060,5070,5080"`
- - Destination: udp://@239.1.1.1:1234
- **Unpacking**: Get the BBC DAB bitstream with tools "ts2na"+"na2ni" and generate one output ETI-NI file:
- - `socat UDP4-RECV:1234,bind=239.1.1.1,ip-add-membership=239.1.1.1:eth0,reuseaddr - | ts2na -p 1061 -s 12 | na2ni -o bbc.eti-ni`
- **Play**: You need to use some DAB player with ETI-NI support (for example DABlin).
