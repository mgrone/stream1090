## Advanced Usage

**WIP**

This readme is intended for users who want to take advantage of some other features that are not for the average user. It assumes that you are familiar with the default way of using stream1090.

## Table of Contents
- [The Stdin Way](#stream1090-via-Stdin)
- [Recording Sample Datasets](#recording-sample-datasets)

## Stream1090 via Stdin
Initially stream1090 had no native device driver support. So where did it get the SDR data from then? Short answer: From the command-line tools ```rtl_sdr``` and ```airspy_rx``` via stdin. So instead of 
```
   native device
        |
        v
    stream1090
```
you would do something like
```
 rtl_sdr/airspy_rx
        |
        v
    stream1090
```
The two command-line tools (```rtl_sdr``` and ```airspy_rx```) come with librtlsdr and libairspy, respectively. They are able to read data from the SDR device and dump it to stdout which in turn can be piped into stream1090's stdin.
From there on things work the same as with native device driver support.

This functionality is still present in stream1090 and will not be deprecated for reasons that become evident later. We first start with two simple examples to show how this works in practice. We will pipe stdout of ```rtl_sdr``` and ```airspy_rx``` into stdin of stream1090.
1. The RTL-SDR way:
    ```
    rtl_sdr -f 1090000000 -s 2400000 - |
    ./build/stream1090 -s 2.4 > /dev/null
    ```
    Dial in on 1090 MHz, set the sample rate to 2.4 Msps. Pipe the output into stream1090 and tell it that the input sample rate is 2.4 Msps (for the missing ```-u``` it will default to 8)

    **Important:** If you use the normal librtlsdr, there is no difference between rtl_sdr and the native device support variant. However, when using the RTL-SDR Blog library there is currently no rtl_sdr version available that behaves the same way and offers full gain control. This is being worked on.

2. Similarly, for Airspy we do
    ```
    airspy_rx -t 4 -g 20 -f 1090.000 -a 12000000 -r - | 
    ./build/stream1090 -s 6 > /dev/null
    ```
    **Important:** We use here ```-t 4``` which tells ```airspy_rx``` to output a single U16_REAL sample at 12Msps. Stream1090 works usually on an IQ pair basis. So an input sample rate of ```-s 6``` means, it is expecting pairs not single values. That is why we use ```-a 12000000```, because 2 x 6 Msps = 12 Msps. You also want to replace the gain setting with your own.  

TODO: insert socat example

## Recording Sample Datasets
Using stdin to feed stream1090 with device data has one big advantage when it comes to benchmarking: You can easily capture data once and feed it multiple times with different parameters into stream1090 just by using standard unix tools. We only provide here an example for RTL-SDR. Both ```rtl_sdr``` and ```airspy_rx``` have an option where you can specify the number of samples to capture and where to put them. We are not going to use this. We will just use pipes and the timeout tool.

 The easy way for ```rtl_sdr``` is 

1. Call ```rtl_sdr``` together with a timeout of 1 minute and pipe the output into a file
    ```
    timeout 1m rtl_sdr -f 1090000000 -s 2400000 - > ./samples.bin
    ```
2. And then pipe the raw file contents into stream1090 using the same sample rate
    ```
    cat ./samples.bin | ./build/stream1090 -s 2.4 > /dev/null
    ```

For ```airspy_rx``` this works exactly the same way.
