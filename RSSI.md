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