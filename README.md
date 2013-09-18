eti-tools
=========

ETI conversion software

Additional libraries are needed:
* [biTStream](http://www.videolan.org/developers/bitstream.html) - for transport stream handling
* [libfec](http://mmbtools.crc.ca/content/view/39/65/) - for Reed-Solomon FEC.
* [libshout](http://www.icecast.org/download.php) - for NI-to-HTTP converter.


ts2na
===============
ts2na - is MPEG-TS to ETI-NA converter. To use this tool, you have to provide pid to use, and optional offset size.

    usage: ./ts2na [-p pid] [-s offset] [-i <inputfile>] [-o <outputfile>]

The output stream will be raw pid content.

eti_na2ni
===============
eti_na2ni - is ETI-NA to ETI-NI converter. This tool automatically detects E1-sync bits in bitstream and inversion flag. Also it extracts ETI-LI content of the stream and incapsulate it into ETI-NA frames. It is possible to disable Reed-Solomon error correction, then conversion speed will be dramatically increased.

    usage: ./eti_na2ni [--no-fec] [-i <inputfile>] [-o <outputfile>]

The output stream will be 6144-bytes aligned raw ETI-NI stream.

eti_ni2http
===============
eti_ni2http - is ETI-NI to http converter. This tool converts eti-stream to mp2 and relay it to icecast2 server.

    usage: ./eti_ni2http [--list] [--delay] [-i <inputfile>] [-c <config_file>]

Use "--list" option to find SID's and station names of the streams inside ETI.
The "--delay" option have to be used when you are doing offline-relaying (from the file, not from the stream). So in that case application will wait 24ms after each eti frame, to make pseudo-realtime streaming.

The application also parses FIC for auto-detecting of station name and X-PAD for setting current song titles.

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

In [server] section the parameters of icecast server must be setted.
In [channel] sections at least service-id of the channel and mount-point on the icecast server must be provided. To get list of service-id's, use "eti_ni2http --list -i <inputfile>"
