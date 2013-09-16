eti-tools
=========

ETI conversion software

Additional libraries are needed:
* biTStream - for transport stream handling
* libfec - for Reed-Solomon FEC.

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

