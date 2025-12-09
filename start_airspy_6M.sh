# Example for airspy_rx and stream1090 @6 MHz using the default output of airspy_rx
# Do not forget to add -b 1 if you are using bias-t
airspy_rx -g 16 -f 1090.0 -a 6000000 -r - | ./build/stream1090_6M -u 12 | socat -u - TCP4:localhost:30001 
