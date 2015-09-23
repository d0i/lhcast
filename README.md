# LHCAST: multicast send/recv over reed-solomon encoding with longhair lib.

BUG: not ready to handle large file (more than 255 blocks in encoded state)

## USAGE

- at server (receiver)
```
% ./lhrcv.py | ./lhjoin -o destination_filename.ext -
```

- at client (sender)

```
% ./lhsplit -o - -b 1464 -r 20 source_filename.ext | ./lhsnd.py
```


