rtl_sdr -g 0.0 -f 1090000000 -s 2400000 - | ./build/stream1090 -s 2.4 -u 8 | socat -u - TCP4:localhost:30001 


