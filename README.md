# Stream1090
Mode-S demodulator written in C++ with CRC-based message framing.

Stream1090 is a proof of concept implementation taking a different approach in order to identify mode-s messages in an SDR signal stream.
Most implementations look for the so-called preamble (a sequence of pulses anounncing a message). Stream1090 skips this step and maintains
directly a set of shift registers. Based on the CRC sum and other criteria, messages are being identified. The hope is that in high traffic
situations, a higher overall message rate can be achieved compared to a preamble based approach.

## Features
- CRC-based message framing: Cannot miss a message, because it missed the preamble.
- Error correction: The CRC sum is computed regardless of the data, so why not use it for error correction whenever possible.
- Not output sensitive: The majority of the computational work does not depend on the message rate.
- MLAT support.
- Support for Airspy and RTL-SDR dongles.
- Support for 6 or 10 Msps for Airspy and 2.4 or 2.56 Msps for RTL-SDR devices.
- IQ Low-pass filtering including customization and optimization (optional).
- Seamless integration into readsb/dump1090-fa based stacks.

## Requirements
- RTL-SDR based dongle or Airspy with antenna etc.
- Debian-based Linux (Ubuntu, Raspberry Pi OS, ...)
- Optional: RaspberryPi 5 or 4 should work for most settings. 
  This depends on your settings
- Optional: For RTL-SDR (not airspy), a RaspberryPi 3B and Zero 2 W seems to work without cooling.


## Table of Contents
- [Compiling](#compiling-stream1090)
- [First Steps](#first-steps)
- [Upsampling](#Upsampling)
- [Low-Pass Filter](#low-pass-filter)
- [Stack Integration](#stack-integration)
- [Experimental Features](#experimental-features)

This is a first draft of the new README. The old complicated one is [here](OLD_README.md)

## Compiling Stream1090
Regardless of your hardware, you will need
- cmake (3.10 or higher)
- C++ compiler that supports C++20

Stream1090 has native device support for Airspy and RTL-SDR based dongles.
For both, you will need the dev version of the corresponding libraries. 

- For Airspy ```sudo apt install libairspy-dev``` 
- For RTL-SDR ```sudo apt install librtlsdr-dev``` 

We are ready to compile. Switch to the stream1090 folder and do the usual cmake thing.

1. Create a build folder ```mkdir build && cd build```
2. Run CMake there with ```cmake ../``` 
3. Build the project using ```make```

Run ```./stream1090 -h``` in the build directory to bring up the help screen. Verify that the second line starting with ```Native device support``` has your device type listed (```Airspy``` and/or ```RTL-SDR```). 

## First Steps
Before we start, you have to understand how stream1090 works. The rough principle is:
```
    stream1090
        |
        v
     decoder 
(readsb or dump1090-fa)
```
**Important:** Stream1090 is a demodulator, **NOT** a decoder. It looks for messages in the signal stream, it does **NOT** extract information.
You will need a decoder like readsb or dump1090-fa. More on this later.

First thing to do is to make sure that no other software is using your SDR hardware. Stream1090 requires exclusive access to the SDR device.

As a next step, we configure stream1090 for a test run without any decoder. The configuration is split into two parts. 

- Device specific parameters
- General parameters

The latter ones are passed via command line, while the device specific ones are located in a config file. 

### Device specific configuration
The stream1090 directory contains a folder named ```./configs```. There you can find two device specific files, ```rtlsdr.ini``` and ```airspy.ini```. 
Edit the corresponding file for your device and read the comments. For now you may only want to adjust the gain settings with one exception:

**Important:** If you are powering an LNA via bias-tee, you have to turn that on by setting ```bias_tee = true```. It is off by default.

### Minimal running example
For the sake of a first try, we will focus only on parameters that are necessary to get things up and running. You may have noticed that the sample rate is not part of the ini file. There are reasons for that. 

So we have to tell stream1090 two things to get going:

- The sample rate via ```-s <rate>``` in MHz
- The location of the device configuration file via ```-d <file.ini>```

However, all messages found by stream1090 will be written to stdout. Since we cannot use them right now, we will suppress them by sending them to ```/dev/null```. 

Switch to the stream1090 directory. In case of RTL-SDR, we will select 2.4 MHz as sampling rate.

```./build/stream1090 -s 2.4 -d ./configs/rtlsdr.ini > /dev/null```

For Airspy the minimum supported sample rate is 6 MHz

```./build/stream1090 -s 6 -d ./configs/airspy.ini > /dev/null```

In both cases you should see stream1090 starting up and after around 5 seconds some statistics similar to this.
```
-------------------------------------------------------------
|     Type |  #Msgs |  %Total |    Dups |   Fixed |   Msg/s | 
-------------------------------------------------------------
|    ADS-B |    875 |   16.4% |   20.9% |   36.3% |     175 | 
|   Comm-B |   1112 |   20.9% |      0% |         |   222.4 | 
|     ACAS |    289 |    5.4% |      0% |         |    57.8 | 
|     Surv |    757 |   14.2% |      0% |         |   151.4 | 
|    DF-11 |   2298 |   43.1% |   23.7% |   15.9% |   459.6 | 
-------------------------------------------------------------
|  112-bit |   2005 |   37.6% |   10.3% |   17.9% |     401 | 
|   56-bit |   3326 |   62.4% |   17.7% |   11.9% |   665.2 | 
-------------------------------------------------------------
|    Total |   5331 |    100% |   15.1% |     14% |    1066 | 
-------------------------------------------------------------
(Max. msgs/s 1066)
Messages Total 30483
DF 0 : 271
DF 4 : 642
DF 5 : 115
DF 11 : 2298
DF 16 : 18
DF 17 : 875
DF 20 : 861
DF 21 : 251
5000000 iterations @1MHz
```
If stream1090 does not start up, you may want to add the ```-v``` flag which enables verbose output.
If it works, your statistics will probably show a much lower message rate. However, the goal was to get stream1090 up and running. Now it is time to make use of its features.

## Upsampling
Stream1090 is build around the idea to take the samples from the SDR that come in at a rate specified via
```
-s <rate>   Input sample rate in MHz
```
and upsample them to a higher rate before processing them. This upsample rate can be specified via
```
-u <rate>   Upsample rate in MHz
```

However, this rate cannot be chosen arbitrarily. There is a certain set of allowed combinations which can be obtained by running ```./stream1090 -h```.
In a much nicer table, they look like this:
| Input | Upsample | Input type | Device |
|------|--------|------| ------|
|  2.4  |  8 | uint8 IQ | RTL-SDR |
|  2.4  |  12 | uint8 IQ | RTL-SDR |
|  2.56  |  8 | uint8 IQ | RTL-SDR |
|  2.56  |  12 | uint8 IQ | RTL-SDR |
|  6  |  6 | uint16 IQ | Airspy |
|  6  |  12 | uint16 IQ | Airspy |
|  6  |  24 | uint16 IQ | Airspy |
|  10  |  10 | uint16 IQ | Airspy |
|  10  |  24 | uint16 IQ | Airspy |

The rule of thumb here is simple: The higher the upsample rate, the more messages will be found, but at the cost of higher CPU usage.
If you do not care about CPU usage, then use the highest upsample rate. For RTL-SDR this would be something like
```
./build/stream1090 -s 2.56 -u 12 -d ./configs/rtlsdr.ini > /dev/null
```
There is no higher upsampling rate in this case. For Airspy you can do
```
./build/stream1090 -s 6 -u 24 -d ./configs/airspy.ini  > /dev/null
```
or if your hardware supports 10 Msps sample rate
```
./build/stream1090 -s 10 -u 24 -d ./configs/airspy.ini > /dev/null
```

#### A note on RTL-SDR devices and 2.56 MHz
In general it is assumed that 2.4 MHz is the highest reliable sample rate for an RTL-SDR device. However, after experiments with different sticks, it turned out that at 2.56 MHz, samples are dropped only once at the beginning. Afterwards, no sample loss has been observed.


## Low-Pass Filter
Stream1090 offers the option to apply a low-pass filter to the IQ-pairs coming from the SDR device. It turned out that this increases the message output significantly in many cases at the cost of higher CPU usage.
To enable filtering use
```
-q    Enables IQ FIR filter with built-in taps
```
Stream1090 comes with a single filter for each sample rate combination. These have been optimized for a set of pre-recorded sample data provided by people from around the world with different setups.


## Stack Integration
In order to utilize the output of Stream1090, we will send it to a decoder like readsb or dump1090-fa via TCP. This has one big advantage: You do not have to modify anything in the remaining stack.
```
   stream1090
        |
        v 
  readsb/dump1090-fa
        | 
        v
 bells and whistles
```

In the following, we will take readsb as an example. If you are using dump1090-fa, please read the readsb part first. It is just about where their settings are located/passed. 

### Readsb
Currently you probably have something like this
```
  Native device
        |
        v 
     readsb
        | 
        v
 bells and whistles
```
The idea is to
- Detach the device from readsb
- Pipe the messages from stream1090's stdout into socat which then forwards those to readsb via TCP. 

Readsb is able to receive messages in hex format and decode these.
If you haven't already installed readsb, head over to https://github.com/wiedehopf/readsb and follow the instructions there. For socat, you can simply do

```sudo apt install socat```

For testing you may now proceed in two steps:
1. In a separate terminal, start readsb with a minimal configuration in interactive mode with 

    ```readsb --net-only --net-ri-port 30001 --interactive```

    This puts readsb into network only mode so it does not claim the SDR dongle.
    Furthermore, it starts listening on port 30001 for message frames that it will then decode.

2. Once readsb is up and running, we can start stream1090 and send the output to readsb via socat. For RTL-SDR, we may do something like this

    ```
    ./build/stream1090 -s 2.4 -d ./configs/rtlsdr.ini | 
    socat -u - TCP4:localhost:30001
    ```
    and in the same way for Airspy
    ```
    ./build/stream1090 -s 6 -d ./configs/airspy.ini | 
    socat -u - TCP4:localhost:30001
    ```

You should now see the readsb table filling up with planes.

### Readsb as a service
If you have readsb running as a service by for example using the install script. 
You may have to edit the config file ```/etc/default/readsb```. Especially when readsb has been compiled with native RTL-SDR support. So if you want readsb to not use the dongle, you have to get rid of this
```
RECEIVER_OPTIONS="--device 0 --device-type rtlsdr --gain auto --ppm 0"
```
by setting it to nothing
```
RECEIVER_OPTIONS=""
```
Make sure that ```NET_OPTIONS="..."``` contains ```--net-ri-port 30001```


### Dump1090-fa
If you want to use dump1090-fa instead, you have to basically follow the same steps as with readsb. However, there is a slight difference about the options. You will have to edit ```/etc/default/dump1090-fa``` and do the following:
- Detach the device from dump1090-fa 
```RECEIVER=none```
- Enable dump1090-fa to receive raw messages
```NET_RAW_INPUT_PORTS=30001```

Do not forget to reload the service to make the changes come into effect.
With stream1090 you proceed as above in the readsb section.


#### Disable stream1090 statistics
If you want to run stream1090 as a service, it makes sense to disable the statistics. You can do so by setting the corresponding option for cmake and rebuild the project:
```
cmake ../ --fresh -DENABLE_STATS=OFF && make
```

## Experimental Features
### SIGHUP support

There is now basic experimental support for the SIGHUP signal. This signal can be send via ```kill -HUP <process id of stream1090>``` telling stream1090 to reload the device specific ini file. You can figure out the PID via ```ps```or ```pidof stream1090``` when it is running.

Clearly, there are some things you will not be able to change like serial (and sample rate which is not part of the ini anyways). The purpose is to not have to restart for adjusting gain settings. For airspy, make sure you know what you are doing when switching between manual and simple gain controls.

### Advanced RTL-SDR gain controls

If you have an RTL-SDR device and still not happy, you can push things further. Stream1090 comes with a hacked version of the [RTL-SDR-BLOG](https://github.com/rtlsdrblog/rtl-sdr-blog) lib which in turn is a fork of librtlsdr. There are two aspects here.
- This lib behaves differently in terms of results. Might be in your favour.
- If you have an R82xx tuner, this version gives you gain control over the LNA, MIX and VGA stages.

If you want to use it, there is no need to download anything nor building and such. Stream1090's CMake project will take care of it. Go to the build folder and rebuild with
```
cmake ../ --fresh -DENABLE_RTLSDR_BLOG=1 && make
``` 
Check if everything has worked out by running ```./stream1090 -h```. The native device support section should now list ```RTL-SDR Blog (advanced)```.

Regarding the manual gain control of the stages: I am not taking any responsibility here. It might work, it might not. I added these functions to the lib. There are usually good reasons that these functions are not exposed.


However, in the ini file you can now add something like
```
lna_gain = 14
mixer_gain = 13
vga_gain = 10
``` 




## FAQ & Troubleshooting
- Why is my message rate 0-2 messages per second?

  You most likely feed in the wrong format or at a wrong sampling speed.

- Why is my message rate not that good? 

  Make sure your gain setting is right and matches your setup. 

- Why are there only so few configurations in the table?
The compiler will create for each configuration a separate pipeline.

## Acknowledgments 
I would like to thank several people that provided sample data, tried it in their setups and also did some early tests with airspy.
rhodan76, wiedehopf, caius, abcd567. cnuver, jrg1956, jimmerk2. Thank you very much!
