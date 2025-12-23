# example for recording some samples in int16 iq format with 6Msps
airspy_rx -g 16 -f 1090.0 -a 6000000 -n 1000000 -r - > samples.raw

# and run it through stream1090
cat samples.raw | ../build/stream1090_6M