# Stream1090
Mode-S demodulator written in C++ with CRC-based message framing.

Stream1090 is a proof of concept implementation taking a different approach in order to identify mode-s messages in an SDR signal stream.
Most implementations look for the so-called preamble (a sequence of pulses anounncing a message). Stream1090 skips this step and maintains
directly a set of shift registers. Based on the CRC sum and other criteria, messages are being identified. The hope is that in high traffic
situations, a higher overall message rate can be achieved compared to a preamble based approach.

## Features
- CRC-based message framing: Cannot miss a message, because it missed the preamble.
- Error correction: The crc sum is computed regardless of the data, so why not use it for error correction whenever possible.
- Not output sensitive: The majority of the computational work does not dependent on the message rate.
   
## Hardware requirements
- RTL-based SDR dongle with antenna etc (different formats and input sampling speeds will be available soon).
- Optional: RaspberryPi 5.

**WARNING!** This has only been tested on a RaspberryPi 5 which is quite powerful. By design, stream1090 requires more computational power. 
It sieves through 16 million possible messages per second. A RaspberryPi 5 can easily deal with this (<18% single core). 
I do not own an older RaspberryPi and can therefore not say anything about those.
**END OF WARNING.**

# Installation
Stream1090 is written in C++ and self-contained. You will need cmake (3.10 or higher) and some C++ compiler that supports C++20.

## Compiling stream1090
Building stream1090 is straightforward. Unlike other implementations, there are no additional libraries required. Get the source and do the usually cmake thing:

```mkdir build && cd build && cmake ../ && make && cd ..```

This will create the stream1090 executable in the build directory.

## RTL-based SDRs
As a next step, we need to feed stream1090 which expects 8-bit unsigned IQ pairs at a rate of 2.4Mhz at stdin. For this install rtl_sdr

```sudo apt install rtl-sdr```

Note that you will not need to install the dev version. Instead we will pipe the output of rtl_sdr into stream1090.
You can now already test stream1090

``` rtl_sdr -g 49.6 -f 1090000000 -s 2400000 - | ./build/stream1090 > /dev/null ```

This sets the gain to 49.6db (feel free to change this or set it to auto with 0), the sample rate to 2.4Mz and tunes the dongle to 1090Mhz.
The output (8-bit unsigned IQ pairs) is then piped into stream1090. 
Stream1090 will write the resulting messages to stdout while providing statistics
on stderr every 5 seconds. We get rid of the messages for testing purposes. You should see now something like this:
```
-------------------------------------------------------------
|     Type |  #Msgs |  %Total |    Dups |   Fixed |   Msg/s | 
-------------------------------------------------------------
|    ADS-B |    881 |   16.4% |   15.9% |   32.4% |   176.2 | 
|   Comm-B |   1134 |   21.1% |      0% |         |   226.8 | 
|     ACAS |    229 |    4.3% |      0% |         |    45.8 | 
|     Surv |    612 |   11.4% |      0% |         |   122.4 | 
|    DF-11 |   2515 |   46.8% |   18.1% |   11.2% |     503 | 
-------------------------------------------------------------
|  112-bit |   2031 |   37.8% |    7.6% |   15.5% |   406.2 | 
|   56-bit |   3340 |   62.2% |   14.2% |    8.8% |     668 | 
-------------------------------------------------------------
|    Total |   5371 |    100% |   11.8% |   11.2% |    1074 | 
-------------------------------------------------------------
(Max. msgs/s 1074)
Messages Total 114496
```

## Readsb & socat (optional)
Recall that stream1090 is a demodulator and not a decoder. 
In order to decode messages and integrate stream1090 into the stack,
readsb in conjuntion with socat offers a simple effective solution. 
The idea is to pipe the messages from stream1090's stdout into socat which then forwards those to readsb. 
Readsb is able to receive messages in hex format and decode these.
If you haven't already installed readsb, head over to https://github.com/wiedehopf/readsb and follow the instructions there.
For socat, you can simply do

```sudo apt install socat```

For testing you may now proceed in two steps:
1. Start readsb in a minimal configuration in interactive mode with 

```readsb --net-only --net-ri-port 30001 --interactive```

This puts readsb into network only mode so it does not claim the SDR dongle. 
Furthermore, it starts listening on port 30001 for message frames that it will then decode.

2. Once readsb is up and running, we can start stream1090 and send the output to readsb via socat with

```rtl_sdr -g 49.6 -f 1090000000 -s 2400000 - | ./build/stream1090 | socat -u - TCP4:localhost:30001```

You should now see the table of readsb filling up with plane data.

## Integration

If you want to integrate stream1090 into an existing stack, readsb is probably the easiest way to do this. 
Make sure that ```--net-ri-port [FILL_IN_YOUR_PORT]``` is set.
Then start feeding it with socat either locally or from a different machine. 
If you want to disable the statistics completely, rebuild the project and set the corresponding option for cmake:

```cmake ../ -DENABLE_STATS=OFF```


# Frequently asked questions

- Why is the signal level not shown?

Stream1090 simply does not know the signal level of a message. 
This would require the concept of noise, high and low levels which is not present in stream1090.

- What about MLAT?

MLAT has not been a priority. Besides providing a proof of concept, stream1090 was written in the first place to feed a custom stack 
for an augmented reality application. For this, very precise real-time positions are required, produced by a single local receiver setup.
