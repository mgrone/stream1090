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
- Support for Airspy and RTL-based SDR dongles (via stdin or native optional)

## Hardware requirements
- RTLSDR-based dongle or Airspy with antenna etc.
- Optional: RaspberryPi 5 or 4 should work for most settings. 
  This depends on your settings
- Optional: For rtl_sdr (not airspy), a RaspberryPi 3B and Zero 2 W works without cooling.

# Installation
Stream1090 is written in C++ and self-contained. You will need cmake (3.10 or higher) and some C++ compiler that supports C++20 (gcc or clang for example)
to get the basic version working. Additional libs which depend on your SDR hardware will be discussed later.

# Installation
Before getting into building stream1090, you have to understand how stream1090 works.
The rough principle is this:
```
Input sample data -->  stream1090 --> messages --> decoder(like readsb or dump1090-fa)                                                                                                
```
Important is that stream1090 is a demodulator, **NOT** a decoder. It looks for messages in the input sample stream, it does **NOT** extract information.
You will need a decoder like readsb or dump1090-fa. More on that later.
So the question now is, where is the input data coming from? There are two ways to feed data into stream1090. 

## Basic (stdin)
The basic default way is to read from stdin:
```
Input sample data (stdin) --> stream1090 --> output messages (stdout)                                                                                                
```
Here stream1090 does not rely on anything. It is just pure C++ reading samples from stdin at a given sampling rate in a given format, demodulates, and writes the resulting messages to stdout. 
No need for any additional libs when compiling stream1090. Why is this not a bad idea? It offers plenty of flexibility. 

## Native device (optional)
The second option is to use the built-in native device support for Airspy and RTL-based SDR dongles. 
```
Native device --> stream1090 --> output messages (stdout)                                                                                                
```
You can still use the basic stdin with these builds.

## Compiling stream1090
By know you should have made up your mind if you want native device support or not.
The libs you will need: 
| Version  | RTL-based | Airspy |
|------------|-------------|-------------|
| Basic (stdin only)    | ```sudo apt install rtl-sdr```    | ```sudo apt install airspy```   |
| Native support    | ```sudo apt install librtlsdr-dev```    | ```sudo apt install libairspy-dev```  |

## Compiling stream1090
Building stream1090 is straightforward. Unlike other implementations, there are no additional libraries required unless you want native device support.
Get the source code from this git and do the usually cmake thing:

```mkdir build && cd build && cmake ../ && make && cd ..```

If you are compiling and you decided to go with native device support, cmake should report something like this:

``` -- [stream1090] Airspy support enabled ``` and/or ```-- [stream1090] RTL-SDR support enabled```

## Running stream1090
In the ```build``` directory you can now run ```./stream1090 -h``` which should get you the command line options help.
For now we consider only the important stuff.
```
Stream1090 build 260207
Native device support: Airspy RTL-SDR
Options:
  -s <rate>            Input sample rate in MHz (required)
  -u <rate>            Output/upsample rate in MHz
  -d <file.ini>        Device configuration INI file for native devices
                       See config/airspy.ini or config/rtlsdr.ini
                       Note that native device support requires librtlsdr-dev
                       and/or libairspy-dev to be installed.

```
Supported sample rate combinations (in a nicer table):
| Input | Upsample | Input type | Device |
|------|--------|------| ------|
|  2.4  |  8 | uint8 IQ | rtlsdr |
|  6  |  6 | uint16 IQ | airspy |
|  6  |  12 | uint16 IQ | airspy |
|  6  |  24 | uint16 IQ | airspy |
|  10  |  10 | uint16 IQ | airspy |
|  10  |  24 | uint16 IQ | airspy |

Let us go through the options. First of all, if you decided to go for native device support, 
make sure that ```Native device support:``` matches your choice.

Central to everything is the input sample rate ```-s <rate>```. It will determine
- How input is interpreted in terms of sample rate
- The format of the input. If the input sample rate is 6 Msps or higher, it is assumed that the data is given in uint16 (airspy).
Otherwise it will be assumed that it is an RTLSDR-based dongle which outputs uint8. 

Note that currently stream1090 only supports 3 different input sample rates.
2.4 Msps (rtlsdr) and for airspy there is 6 and 10 Msps available.

Internally, stream1090 will take the input stream and usually upsample it to a higher frequency. You can choose here a value, but it has to be a
valid configuration from the table above. You can also omit this parameter and it will default to the first entry of the table matching the
input sample rate you provided with ```-s```. So for 2.4, 6, 10 that would be 8, 6, 10, respectively.

### First steps with stdin
Depending on your hardware you have at least ```rtl-sdr``` or ```airspy``` installed. Both come with their command line utils (```rtl_sdr``` and ```airspy_rx```) that enables
you to write the device data stream to stdout. It is now straightforward to get things going. We will do something like this
```
rtl-sdr/airspy --> stream1090 --> /dev/null
```
So we will pipe stdout of those into stdin of stream1090. Here are two minimal examples.
1. The RTLSDR way:
```
rtl_sdr -f 1090000000 -s 2400000 - | ./build/stream1090 -s 2.4 > /dev/null
```
Dial in on 1090 MHz, set the sample rate to 2.4 Msps. Pipe the output into stream1090 and tell it that the input sample rate is 2.4 Msps (for ```-u``` it will default to 8)

2. Similarly, for Airspy we do
```
airspy_rx -t 4 -g 20 -f 1090.000 -a 12000000 -r - | ./build/stream1090 -s 6 > /dev/null
```
**Important:** We use here ```-t 4``` which tells ```airspy_rx``` to output uint16 REAL (raw). Stream1090 works on an IQ pair sample base. So an input sample rate of ```-s 6``` means, it is expecting pairs not single values. That is why we use ```-a 12000000```, because 2 x 6 Msps = 12 Msps.  

### Native device config
If you do not want to read from stdin, but instead want stream1090 to directly deal with the device, that is what option ```-d <ini file>``` is for.
You have to provide a device specific ini file with parameters. There are two example ini files with documentation in [```./configs```](configs/).
So how do these work? Each config file starts with ```[rtlsdr]``` or ```[airspy]```. This tells stream1090 the device type it should be using.
This is followed by some properties which are device specific. Refer to the documentation in [```rtlsdr.ini```](configs/rtlsdr.ini) and [```airspy.ini```](configs/airspy.ini). Note that the sample rate is not part of the device options. This is being set by ```-s```.



UNDER CONSTRUCTION

## !Attention! (regarding this README)
Stream1090 is undergoing many changes right now. While the instructions below are still working, there are many undocumented features and upcoming changes. Please excuse me for not updating this README. For example, there is no point in writing an extensive guide on how to get airspy_rx going with stream1090 while implementing native support for airspy. You will find plenty of stuff over here: https://discussions.flightaware.com/t/stream1090/99603



 
## Updating from a previous version
If you have compiled a previous version, it is important that you either get rid of the build folder completely, or do a 
```make clean && cmake ../ --fresh```
in the build folder. There are several changes from version to version that may require an update of the Makefile. 
 
## RTL_SDR-based SDRs
As a next step, we need to feed stream1090 which expects 8-bit unsigned IQ pairs at a rate of 2.4Mhz at stdin. For this install rtl_sdr

```sudo apt install rtl-sdr```

Note that you will not need to install the dev version. Instead we will pipe the output of rtl_sdr into stream1090.
You can now already test stream1090

``` rtl_sdr -g 0 -f 1090000000 -s 2400000 - | ./build/stream1090 -s 2.4 -u 8 > /dev/null ```

This sets the gain to 49.6db (feel free to change this or set it to auto with 0), the sample rate to 2.4Msps and tunes the dongle to 1090MHz.
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

```rtl_sdr -g 0 -f 1090000000 -s 2400000 - | ./build/stream1090 -s 2.4 -u 8 | socat -u - TCP4:localhost:30001```

You should now see the table of readsb filling up with plane data.

## Integration

If you want to integrate stream1090 into an existing stack, readsb is probably the easiest way to do this. 
Make sure that ```--net-ri-port [FILL_IN_YOUR_PORT]``` is set.
Then start feeding it with socat either locally or from a different machine. 
If you want to disable the statistics completely, rebuild the project and set the corresponding option for cmake:

```cmake ../ -DENABLE_STATS=OFF```

You can find a more well described manual over here: 

## Notes on dump1090.
I have not tried it, but others did and it seems to work just fine. Just make sure to open the raw input port.

# Airspy
The overall setup with airspy is similar. Use airspy_rx and pipe it into stream1090. 
For example

```airspy_rx -t 4 -g 20 -f 1090.000 -a 12000000 -r - | ./build/stream1090 -s 6 -u 24 > /dev/null```

sets airspy_rx to sample at 6MHz IQ pairs (= 12MHz uint16_REAL). Stream1090 is configured for 6MHz input via -u 24 to upsample internally to 24 MHz.

# Frequently asked questions & troubleshooting
- Why is my message rate 0-2 messages per second?

You most likely feed in the wrong format or at a wrong sampling speed.

- Why is my message rate not that good? 

Make sure your gain setting is right and matches your setup. 
Stream1090 has no control over the hardware and therefore also has no auto gain.

- Why is the signal level not shown?

Stream1090 simply does not know the signal level of a message. 
This would require the concept of noise, high and low levels which is not present in stream1090.

- What about MLAT?

MLAT timestamp is part of the output.

**Important Update** I would like to thank several people that provided sample data, tried it in their setup and also did some tests with airspy.
rhodan76, wiedehopf, caius, abcd567. Thank you very much!
