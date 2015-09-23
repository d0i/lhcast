# LHCAST: multicast send/recv over reed-solomon encoding with longhair lib.

MAJOR BUGS:
- not ready to handle large file (more than 255 blocks in encoded state)
- not ready for send/receive files over lossy network 
- see also: BUGS, header of lhsnd.py

## USAGE

- at server (receiver)
```
% ./lhrcv.py | ./lhjoin -o destination_filename.ext -
```

- at client (sender)

```
% ./lhsplit -o - -b 1464 -r 20 source_filename.ext | ./lhsnd.py
```


