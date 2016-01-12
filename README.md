eti-tools
=========

ETI conversion software

Additional libraries are needed:
* [libfec](http://mmbtools.crc.ca/content/view/39/65/) - for Reed-Solomon FEC, may be enabled/disabled.
* [libshout](http://www.icecast.org/download.php) - for NI-to-HTTP converter (used modified one that supports aac and raw streaming).
* [libzmq](http://zeromq.org) - optional: for ZeroMQ output of NI-to-HTTP converter (possible re-mux of ETI-streams containing DAB/DAB+ streams). Uncomment it in Makefile if this option is needed.


ts2na
===============
ts2na - is MPEG-TS to ETI-NA converter. To use this tool, you have to provide pid to use, and optional offset size.

    usage: ./ts2na [-p pid] [-s offset] [-i <inputfile>] [-o <outputfile>]

The output stream will be raw pid content. If negative offset is setted, then "pid" field will be used as part of the ETI stream.

ETI na2ni
===============
na2ni - is ETI-NA to ETI-NI converter. This tool automatically detects E1-sync bits in bitstream and inversion flag. Also it extracts ETI-LI content of the stream and incapsulate it into ETI-NI frames. It is possible to disable Reed-Solomon error correction, then conversion speed will be dramatically increased.

    usage: ./na2ni [--no-fec] [-i <inputfile>] [-o <outputfile>]

The output stream will be 6144-bytes aligned raw ETI-NI stream.

ETI ni2http
===============
ni2http - is ETI-NI to http converter. This tool converts eti-stream to mp2 and relay it to icecast2 server.

    usage: ./ni2http [--list] [--delay] [-i <inputfile>] [-c <config_file>]

Use "--list" option to find SID's and station names of the streams inside ETI.
The "--delay" option have to be used when you are doing offline-relaying (from the file, not from the stream). So in that case application will wait 24ms after each eti frame, to make pseudo-realtime streaming.

The application also parses FIC for auto-detecting of station name and X-PAD of DAB and DAB+ for setting current song titles.

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
    #which playable by the internet-radio players, enabled by default
    #If this option disabled, this stream can be directly passed to ODR-DabMod
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

In [server] section the parameters of Icecast2 server must be setted.
In [channel] sections at least service-id of the channel must be presented. If u wish to write stream to file, then use "file" to specify it's location. If stream to Icecast2 server is needed, then specify mount-point on the icecast server. If you wish to re-stream to ODR-DabMUX then set destination of the ZeroMQ URI to muxing server. To get list of service-id's, use "ni2http --list -i <inputfile>"
