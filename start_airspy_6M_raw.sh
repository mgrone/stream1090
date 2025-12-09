# Example for using the raw mode -r of stream1090
# Do not forget to add -b 1 if you are using bias-t
# Notice that for 6MHz, we have to tell airspy_rx to output at 12MHz
airspy_rx -t 5 -g 16 -f 1090.0 -a 12000000 -r - | ./build/stream1090_6M -r -u 12 | socat -u - TCP4:localhost:30001 