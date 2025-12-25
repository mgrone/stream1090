This python script plots individual messages together with the samples obtained from either airspy_rx or rtl_sdr. This of course is only meant for pre-recorded samples.

1. Record a sample and run it through stream1090 (see example_record.sh for an airspy_rx example)
2. Pick a message from stream1090's output which is in AVR format. Something like `` @00000010f53a8d4cace09914dbb0380488e38d71; ``
3. Tell the python script about the recorded sample file and the message. You have to include the format and sample speed (in Hz).
``python vis_util.py --file samples.raw --format iq_int16 --fs 6000000 --message "@00000010f53a8d4cace09914dbb0380488e38d71;"``

The script will grab the samples from the file based on the MLAT timestamp (the first 12 hex values) and plot them including the binary representation of the message. This works also with big sample files. 

## Advanced Features
### Multi-message view
If you want to look at multiple messages, you can simply list them in the message field like this:

``--message "@0000028eb0de02a185107aff4f;@0000028ebb745d4845eb30d2b4;"``

The script will determine the smallest window that contains all of them. Make that they are not too far away to avoid large windows.

### Multi-sample view
If you have not only one sample set, but multiple and you want to compare for example the original to a filtered set, you can specify multiple files. They can be of different formats and sample speeds. Here is an advanced example:

``
python vis_util.py --file ./raw.bin --format iq_uint16 --fs 10000000 --file ./mag_inter_20msps.bin --format mag_float32 --fs 20000000 --message "@0000028eb0de02a185107aff4f;@0000028ebb745d4845eb30d2b4;``

This will show 12-bit raw IQ samples at a sample rate of 10Msps together with an upsampled variant in a second file. The second set are not IQ pairs. Only the magnitude is given. The sample speed of that set is 20Msps. The window is chosen based on two messages. 

## FAQ
- Why does this not work properly with other decoders/demodulators?
The script makes assumptions about the MLAT timestamp which depend on stream1090

- Why are there tiny alignment issues?
The timestamp depends on the time when stream1090 can first make sense of a message. Yes, there could be better alignments, but the earliest time possible is used as timestamp.

- Why do some bits not check out with the samples?
The bits are derived from the message you provided as parameter. This might be a repaired message. So some bits have been flipped. 
