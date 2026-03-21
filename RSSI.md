Stub (TODO)

Can be enabled via 

```cmake ../ -DENABLE_RSSI=1```

and then rebuilding the project.

**Warning:** This is currently a very bad implementation of RSSI. 
- It will change sooner than later
- DO NOT compare stream1090 RSSI values with other software
- DO NOT apply rules for your gain settings from other software to stream1090
- The value shown is an estimation (and a very bad one)! It is not even the value for that particular message demodulated.

### Rules of thumb
This needs further testing. 

### Experiment
If you are bored, have an airspy, and want to do a fun experiment:

Uncomment linearity or sensitivity gain settings and use manual settings.
Crank VGA and MIX up to max (13) and set LNA to something low. Say for example 6. 
```
# Optional: manual gain controls. If set, these override
# linearity_gain and sensitivity_gain.
lna_gain = 6
mixer_gain = 13
vga_gain = 13
```

Then slowly (this will take some time), raise the LNA value until you get into some range that works. Works = until your Min RSSI value is above -45, -40 or more is ok. Avg should then get into its 30s or higher. Do not care about the max RSSI.   

Reasoning: LNA will up the SNR which will separate the good signal from the noise and weak signals. We do not want that. Stream1090 is a truffle pig. It will sniffle through the noise.

This should give you a lower bound for the LNA value.

### SIGHUP support

There is now basic experimental support for the SIGHUP signal. If you are using native device support (RTL-SDR or Airspy). This signal can be send via ```kill -HUP <process id of stream1090>``` telling stream1090 to reload the ini file. You can figure the PID via ```ps```or ```pidof stream1090``` when it is running.


Clearly there are some things you will not be able to change like serial (and sample rate which is not part of the ini anyways). The purpose is to not have to restart for adjusting gain settings. For airspy, make sure you know what you are doing when switching between manual and automatic gain controls.

### Gain mixer fun:
Someone was joking around in the past. Here you go with your ALSA-Mixer style gain control. 

```
Gain Mixer — AIRSPY  (h=help)

    Frequency:   1090000000 Hz
      10        13        13




                █         █
                █         █
      █         █         █
      █         █         █
      █         █         █
      █         █         █
      █         █         █
      █         █         █
      █         █         █
      █         █         █
     [LNA]     MIX       VGA
    Bias-T: OFF   (b)



  ←/→ select   ↑/↓ adjust   h help   s SIGHUP   q quit
``` 
You need to provide the config file and the pid
```
python gain_mixer.py --config ../configs/airspy.ini --pid <pid of stream1090 instance running>
```