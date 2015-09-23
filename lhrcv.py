#!/usr/bin/env python

"""
receives udp datagram from lhsnd.py and send it to stdout (expected to be received by lhjoin)

This software is part of lhcast
Copyright 2015 Yusuke DOI <doi@wide.ad.jp>
"""


import struct
import sys
import socket
import pdb

from lhcommon import *

if __name__ == '__main__':
    outf = sys.stdout
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind((TARGET, PORT))

    k=None
    m=None
    blksz=None

    sys.stderr.write('lhrcv: waiting at port %d\n'%(PORT,))
    try:
        while True:
            (pkt, addr) = sock.recvfrom(65535)
            if pkt[0] == HTYPE_SEGHDR:
                sys.stderr.write('lhrcv: found segment header\n')
            else:
                sys.stderr.write('lhrcv: found segment block %d\n'%(ord(pkt[1])))
                outf.write(pkt)
                outf.flush()
    except IOError:
        sys.stderr.write('lhrcv: output stream is closed.\n')
