#!/usr/bin/env python

"""
receives output of lhsplit from stdin and send the segments with broadcast.
(initial header segment will be sent multiple times to avoid packet loss)

WISH:
- sort of reliable protocol for headers
  hdr-ack shall start body block transfer
- sort of reliable data-ack/nack from receivers
  (nack shall have bitmap of unseen blocks for re-send)

NOW(initial test implementation:
- just find block boundary and send each block in UDP!
"""

import struct
import sys
import socket
import pdb

from lhcommon import *

if __name__ == '__main__':
    inf = sys.stdin

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    k=None
    m=None
    blksz=None
    while True:
        pkt = []
        # read a byte
        ts = inf.read(1)
        if ts == '':
            break;
        pkt.append(ts)
        t = ord(ts)
        if (t == HTYPE_SEGHDR):
            # if the byte is HTYPE_SEGHDR, read the header and find the block/segment size.
            hdr_s = inf.read(SEGHDR_LEN)
            hdr_t = list(struct.unpack(SEGHDR_FMT, hdr_s))
            hdr_t.pop(0)#padding
            seguid = hdr_t[:SEGUID_LEN]
            hdr_t = hdr_t[SEGUID_LEN:]
            k = hdr_t.pop(0)
            m = hdr_t.pop(0)
            blksz = hdr_t.pop(0)
            print 'HDR(k: %d, m: %d, blksz: %d)'%(k, m, blksz)
            pkt.append(hdr_s)
        elif (t == HTYPE_BLKHDR):
            # if the byte is HTYPE_BLKHDR and I already know the segment parameters, read a block
            hdr_s = inf.read(BLKHDR_LEN)
            hdr_t = struct.unpack(BLKHDR_FMT, hdr_s)
            blkid = hdr_t[0]
            print 'BLK(id: %d)'%(blkid,)
            pkt.append(hdr_s)
            pkt.append(inf.read(blksz))
        else:
            raise RuntimeError, "unknown blktype %d"%(t)
        # then, send the block to the dgram sock.
        pkt_bytes = ''.join(pkt)
        sock.sendto(pkt_bytes, (TARGET, PORT))
    #
    print 'Done.'

        



