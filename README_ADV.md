## Advanced Usage of Stream1090

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

For ```airspy_rx``` this works the same way. However, make sure that your sample rate is correct. Similar to the stdin section, you have to tell airspy_rx twice the speed that you are using in stream1090. So for example to capture one minute of 10 Msps data, a call would look like this:

1. Call ```airspy_rx``` together with a timeout of 1 minute like in this example:
    ```
    timeout 1m airspy_rx -t 4 -b 1 -l 10 -m 9 -v 11 -f 1090.0 -a 20000000 -r - > ./samples.bin
    ```
    Make sure that you use the correct parameters that correspond to your airspy.ini. In this example:
    - LNA gain is set to 10 with ```-l 10```
    - MIX gain is set to  9 with ```-m 10```
    - VGA gain is  set to 11 with ```-v 10```
    - Enable bias-t with ```-b 1```
    - Dump samples as raw IQ (```-t 4```) at a sample rate of 2x10 IQ-Msps (```-a 20000000```) to stdout (```-r -```)
2. You can then pipe the raw file contents into stream1090 using the same sample rate like for example
    ```
    cat ./samples.bin | ./build/stream1090 -s 10 -u 24 > /dev/null
    ```

When changing the CRC correction patterns, build and run the `table_gen` target to confirm the declared pattern count. The advanced table uses open addressing to preserve colliding entries. Then run CTest: `crc_error_table_test` verifies that every declared DF17 and DF11 correction remains reachable from its CRC syndrome.

Altitude replies support both Q=1 binary encoding in 25-foot increments and
Q=0 Gillham/Mode C encoding in 100-foot increments. Metric M=1 altitude
encoding is currently treated as unavailable. Gillham values are checked
against an altitude already established by Q=1 replies and cannot replace that
reference value.

DF11 all-call replies may overlay the CRC parity with a non-zero II/SI interrogator code. Stream1090 accepts these replies for known aircraft. A first clean DF11 or DF17 sighting enters the cache as untrusted; the address becomes trusted only after a matching sighting between 100 microseconds and 30 seconds later. Interrogator-overlaid DF11 replies require two sightings of the same address between 100 microseconds and two seconds apart; the II/SI codes may differ, and an existing untrusted cache entry does not bypass confirmation. ICAO address `000000` is always discarded.

For DF0/4/5/16/20/21 address-parity replies, altitude and squawk checks normally reject implausible values. A rejected frame from an already active ICAO address is nevertheless accepted after the identical complete frame is received again between 100 microseconds and two seconds later. The first observation is withheld, short and long frames cannot confirm each other, and cache collisions can only discard a candidate. Corroborated Gillham or unsupported metric altitude replies do not update the stored altitude reference.

### Plausibility regressions (covered)

The following three CTest targets pin down three previously-open bugs around DF17/18/19/11 trust and address plausibility. Each is now fixed in production code; the tests exercise the fix end to end together with a positive control proving the surrounding, unrelated behavior still works.

Build with `-DBUILD_TESTING=ON` as usual, then run just these three:

```
ctest -R "df18_fine_tisb_regression_test|df19_repair_trust_regression_test|df11_pi_icao_bypass_regression_test" --output-on-failure
```

- `df18_fine_tisb_regression_test`: `Plausibility::checkDF17()` used to be applied to every extended squitter reaching the first-sighting insert, DF18 included. On DF18 the same bit position holds CF (Control Field), not DF17's CA (transponder capability); CF=2 ("Fine TIS-B Message", a genuine ICAO address, see readsb's `mode_s.c`, `decodeExtendedSquitter()`) falls in the range `checkDF17()` rejects for CA. `handleExtSquitterLongMessage()` now applies `checkDF17()` only to frames that are genuinely DF17-shaped (native DF17, or DF19 promoted to 17 -- see below); a DF18 CF=2 report goes through `checkICAO()` alone and can earn trust like before. Residual limitation, not addressed by this fix: DF18 CF=1 and CF=5 reports, and CF=2/3 with IMF=1, carry non-ICAO addresses (anonymous, or a 12-bit Mode-A code plus track-file number) rather than a 24-bit ICAO address; stream1090's cache is keyed on ICAO addresses and does not distinguish these cases from an ordinary ICAO-addressed report. This was true before this fix and remains true after it.
- `df19_repair_trust_regression_test`: the DF19-to-DF17 promotion (flip bit 108, adjust the CRC, treat a crc==0 result as a clean squitter) used to reach the same "not known" trust door a genuinely clean DF17 uses, with nothing distinguishing a guessed repair from an actual clean reception. `handleExtSquitterLongMessage()` now records whether a frame arrived as DF19 before the promotion overwrites `downlinkFormat`, and a frame that only reaches crc==0 by way of that guess no longer seeds or confirms trust for an address that is not already trusted (case A: a repeated, structurally-plausible DF19-shaped signal for a brand-new address emits nothing and leaves no trust-candidate behind for a later clean reception to complete). Recovery for an address already trusted through clean DF17 sightings is unaffected and still emits the corrected, original DF17 frame (case B).
- `df11_pi_icao_bypass_regression_test`: specific to this branch (not upstream). Every other insertion point (DF17's own first-sighting insert, DF11's own crc==0 first-sighting and confirmed-second-sighting inserts, and the shared helper the crc==0 path funnels through) calls `Plausibility::checkICAO()` before inserting. The DF11 `crc<80` (PI-overlaid) path's confirming second sighting used to insert and trust the address directly instead, skipping that call entirely for that one path. It now calls `checkICAO()` before a fresh insert, matching the other three insertion points; promotion of an already-known-but-untrusted entry is unaffected, since that entry was already screened at its own insertion.

Important: If you want to see the statistics for the whole file and not every 5 seconds. You can enable the a summary at the end by rebuilding stream with after the following cmake call in the build directory
```
cmake ../ --fresh -DEND_STATS=ON -DENABLE_STATS=ON && make
```

## Sloppy guide to filter optimization (WIP)
I am in a hurry, but instead of a giving a quick tour to rhodan via chat, i decided to quickly write this down for everyone. So this here is all heavy WIP.

### Requirements
- Python
- At least scipy and numpy. See requirements.txt
- That you have read the recording section
- And a sample dataset from your setup over 30 seconds or 1 minute (start with 30s first)
- Stream1090 build with the end stats flag set (see recording section)
- Time: optimizing is not done in seconds or minutes. It is a matter of hours. 

### What does the optimizer do?

1. Run your samples through stream1090 with the builtin filter = Baseline
2. Come up with a new filter
3. Run the new filter with the -f parameter and your sample data
4. Compute a score for the messages obtained
5. Goto 2 unless the optimizer is done.
6. Adjust search area based on the two best solutions.
7. Goto 2.

During this process it will write a log file, where it keeps track of the best solutions. An entry looks like this:
```
# ================================================================
# Instance: ../../samples/10/caius_new_10M_raw.bin
# Time: 2026-05-18 11:28:51
# FS: 10.0 MHz → FS_UP: 24.0 MHz
# Number of taps: 15
# Best score: 52640
# Best message count: 39387
# Best DF17 count: 5845
# Best params: [0.02021815228862463, -0.6807687195818193, 1.3921104576136925, -0.027741899536818458, -0.9195485727663808, 0.232007021621214, 0.09441632613183229, -0.3824879056350703, 0.2592474884024356]
# Current bounds:
# [-0.001124, 0.048876]
# [-0.712022, -0.662022]
# [1.355048, 1.405048]
# [-0.052130, -0.001372]
# [-0.947887, -0.897887]
# [0.225073, 0.275073]
# [0.053137, 0.103137]
# [-0.415576, -0.365576]
# [0.243603, 0.293603]
# Best taps:
0.037165913730859756
0.10831016302108765
0.043313708156347275
-0.08276823908090591
-0.10514900833368301
0.19626487791538239
0.23486527800559998
0.1359947919845581
0.23486527800559998
0.19626487791538239
-0.10514900833368301
-0.08276823908090591
0.043313708156347275
0.10831016302108765
0.037165913730859756
```
So there are some things that are important to know. The stuff that stream1090 requires are so-called taps. These are the numbers at the end of the entry. The other lines starting with # are skipped by stream1090. You can simply copy an entry from the log file, paste it into a separate txt file and tell stream1090 via ``-f`` to load the filter from that file.

**Important**: ALWAYS copy the complete entry, that is, including the comments aka header and DO NOT modify it. There are two reasons for this:
1. You can keep track of for which the filter is optimized for (upsample rate, instance used to train, and parameters)
2. The filter optimizer can read an entry and use the parameter section as initial starting solution.

As you can probably now guess from the previous comment. The paramaters are kind of important. So long story short: The optimizer works on a list of paramters called gain points. In the example above the filter has been constructed using
```
# Best params: [0.02021815228862463, -0.6807687195818193, 1.3921104576136925, -0.027741899536818458, -0.9195485727663808, 0.232007021621214, 0.09441632613183229, -0.3824879056350703, 0.2592474884024356]
```
as gain points. This is all the optimizer cares about. It will translate these gain points into tap values and evaluate these based on what stream1090 produces. This serves then as score for the gain points.

### How to run the optimizer
We start with an example. The example assumes that we are in the filter_utils directory, stream1090 is in its build directory and that there is a 10Msps sample called `samples.bin` in the filter_utils directory.

```
python filter_opt.py --data samples.bin --fs 10000000 --fs-up 24000000 --num-gain-points 9 --num-taps 15 --margin 1.0 --log de_log_10_to_24_15_taps.txt
```
So let us go through the parameters.
- Tell the script to optimize on our sample data by `--data samples.bin`
- The samples have been recorded at 10 Msps and we want to optimize for an internal upsampling rate of 24 Msps. `--fs 10000000 --fs-up 24000000` corresponds to running stream1090 with `-s 10 -u 24`. Note that these are given in Hz here.
- The number of gain points used by the optimizer is set via `--num-gain-points 9`. Please stick to 9 for now.
- `--num-taps 15` determines the number of taps to be calculated from the gain points. 15 is stream1090 default. **Important:** This number has to be odd for now.
- Since we do not provide any initial starting solution we set the bounds for the gain points to something very large by `--margin 1.0`
- `--log de_log_10_to_24_15_taps.txt` provides the filename for the log file to write the entries to.

You should see know the optimizer going and if you take a look at the log file, you should see the optimizer here and there adding entries. However, the results you will be really bad. You can abort this with Ctrl+C.

In this particular, case we can take a shortcut. The `custom_filters` directory contains some filters. We can use them as a starting point. Here we have a sample recorded at 10 Msps. In the custom_filters directory there is `EU_caius_10_24.txt` whose number of gain points is also 9. Good enough.

Let us resume from there by adding `--resume ../custom_filters/EU_caius_10_24.txt`, that is:
```
python filter_opt.py --data samples.bin --fs 10000000 --fs-up 24000000 --num-gain-points 9 --num-taps 15 --margin 0.2 --log de_log_10_to_24_15_taps.txt --resume ../custom_filters/EU_caius_10_24.txt
```
Note that we decreased the margin to 0.2 using `--margin 0.2`. This restricts the search space and you will get faster to something useful.

One thing that is important to know is that you can also use a complete log file for `--resume`. The script will look for the last entry in the file, and resumes from there. **DO NOT** resume from entries whose gain point count differs from what you set as parameter.

So it remains the question when the script terminates. Currently it does not. If there is no new solution after some time, you can stop it with Ctrl+c. If you are not happy with the results, you can restart it and resume from the log file and hope for some luck. You may want to increase the margin then a bit.

**ATTENTION** The above description is a very sloppy one. Everything is subject to change. This includes the scoring function and additional parameters. If you want to use the optimizer, always check this section for any remarks first.


