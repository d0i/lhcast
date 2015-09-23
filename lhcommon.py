#!/usr/bin/env python

"""
common definitions in lhsnd/lhrcv.py
"""

import struct 

PORT=12348
TARGET="255.255.255.255"
HTYPE_BLKHDR=1
HTYPE_SEGHDR=2
SEGUID_LEN=30
HASH_LEN=160/8

#excludes type
SEGHDR_FMT="!B%dBBBHH%db"%(SEGUID_LEN, HASH_LEN)
SEGHDR_LEN=struct.calcsize(SEGHDR_FMT)
BLKHDR_FMT="!B%dB"%(SEGUID_LEN)
BLKHDR_LEN=struct.calcsize(BLKHDR_FMT)

