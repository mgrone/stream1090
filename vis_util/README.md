This python script plots an individual message together with the corresponding samples obtained from either airspy_rx or rtl_sdr. This of course is only meant for pre-recorded samples.

1. Record a sample and run it through stream1090 (see example_record.sh for an airspy_rx example)
2. Pick a message from stream1090's output which is in AVR format. Something like `` @00000010f53a8d4cace09914dbb0380488e38d71; ``
3. Tell the python script about the recorded sample file and the message. You have to include the format and sample speed (in Hz).
``python vis_util.py --file samples.raw --format int16 --fs 6000000 --message "@00000010f53a8d4cace09914dbb0380488e38d71;"``

The script will grab the samples from the file based on the MLAT timestamp (the first 12 hex values) and plot them including the binary representation of the message. This works also with big sample files. 

## FAQ
- Why does this not work properly with other decoders/demodulators?
The script makes assumptions about the MLAT timestamp which depend on stream1090

- Why are there tiny alignment issues?
The timestamp depends on the time when stream1090 can first make sense of a message. Yes, there could be better alignments, but the earliest time possible is used as timestamp.

- Why do some bits not check out with the samples?
The bits are derived from the message you provided as parameter. This might be a repaired message. So some bits have been flipped. 
