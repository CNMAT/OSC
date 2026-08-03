# Allocator traffic counter

Counts realloc/malloc/free calls and peak bytes while decoding one packet, by
rebuilding the library with `-Drealloc=counted_realloc` and friends. Not part of
`make asan`; run it when changing allocation behaviour, to have a number rather
than an opinion.

```sh
cd test/host/alloc
for f in OSCMessage OSCData OSCBundle; do
  c++ -std=c++11 -I ../shim -I ../../.. -I . -w \
      -Drealloc=counted_realloc -Dmalloc=counted_malloc -Dfree=counted_free \
      -c ../../../$f.cpp -o $f.o
done
c++ -std=c++11 -I ../shim -I ../../.. -I . -w -c ../../../OSCMatch.c -o OSCMatch.o
c++ -std=c++11 -I ../shim -I ../../.. -I . -w bench.cpp count.cpp *.o -o bench && ./bench
```

Measured before and after the change that stopped `clearIncomingBuffer()`
shrinking on every argument and made growth geometric:

| decoding | before | after |
|---|---|---|
| 1 int | 6 reallocs | 1 |
| 4 ints | 9 | 1 |
| 16 ints (the Esplora message) | 24 | 4 |
| 1 string | 7 | 1 |
| 1 blob, 200 B | 18 | 5 |
| mixed i s f h | 10 | 1 |

Peak bytes were unchanged except the 200-byte blob, where doubling overshoots:
459 -> 495 B.
