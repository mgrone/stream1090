rtl_sdr -g 0.0 -f 1090000000 -s 2400000 - | ./build/stream1090 | socat -u - TCP4:localhost:30001 


