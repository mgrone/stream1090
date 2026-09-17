# AntSDR E200

This is a first draft of how to make stream1090 work **on** an AntSDR E200, not **with**. As a first step, the objectives will be

1. Build stream1090 for the ant by cross-compiling it.
2. Upload it to the ant and starting it.
3. Feed stream1090 raw IQ samples.
4. Find a way to send the output over the network.  

## Building stream1090 

As you know, stream1090 is written in C++. In particular, it requires a compiler that supports C++20 including a standard library that does so aswell. This poses a first hurdle, because the libs available on the ant do not support this. The solution is simple. We are taking the big suitcase and bring everything with us. In other words, we do static linking so everything required to run stream1090 is contained in the binary.

The next problem is that we need to strip the build process of any airspy/rtl-sdr related stuff including their detection mechanisms. The solution here is also simple. Use a separate ```CMakeLists.txt```.

### Requirements
For the sake of an easier description, you should have stream1090 working including a decoder like readsb etc. On top of this now comes

- Compiler that supports C++20 for the ARM platform including its complete toolchain. I went for 

    ``` arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-linux-gnueabihf.tar.xz ```
- SSH access to your ant (not via UART terminal).


#### Toolchain
Let us get started by first unpacking the toolchain and move it somewhere permanent where it is safe.

1. Extract the archive file
    
    ```bash
    tar -xf arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-linux-gnueabihf.tar.xz
    ```

2. Move it to the ```/opt/``` folder 

    ```bash
    sudo mkdir -p /opt/toolchains

    sudo mv arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-linux-gnueabihf /opt/toolchains/arm-gcc-15.2
    ```

Having the toolchain accquired, we move on now to compiling. Make sure that you are in the ```stream1090/AntSDR/``` directory and not the ```stream1090/``` directory. CMake needs to know where to find the toolchain. We solve this with a temporary enviroment variable.

```bash
export TOOLCHAIN_PATH=/opt/toolchains/arm-gcc-15.2
```

Make sure that everything is as it should be

```bash
$TOOLCHAIN_PATH/bin/arm-none-linux-gnueabihf-g++ --version
```

#### Compiling
Now we do the usual CMake thing, but for the ant. Create the build directory

```bash
mkdir build && cd build
```
Run CMake with the ant specific toolchain and build the binary

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=../antsdr_e200.cmake ..
make
```

You can verify that the binary is truly self-contained and compiled for the Zynq ARM architecture by running:
```bash
file stream1090
```

## Running stream1090

Before running stream1090, we need to upload the binary to the ant. The problem here is that the the minimal Buildroot OS running on the ant does not include a modern SFTP subsystem. We circumvent this by using the legacy SCP protocol.
Assuming that you are in the ```AntSDR/build``` directory, we upload the binary into the root's home directory with

```bash
# default password is "analog"
scp -O stream1090 root@192.168.1.10:/root/
```
Now login via SSH 
```bash
ssh root@192.168.1.10
``` 
and check if everything is alright.
```bash 
./stream1090 -h
```
**Important:** The contents of the folder ```/root/``` are located in volatile memory, that is, if you power down the ant, it will be gone.

With the binary in place, we now configure the ant. Keep in mind that stream1090 does not know where it is and has no control about the device. First order of business is to tune in on 1090 Mhz.

```bash
iio_attr -o -c ad9361-phy altvoltage0 frequency 1090000000
```
As a next step, we will set the sampling rate to 4 Msps.
```bash
iio_attr -i -c ad9361-phy voltage0 sampling_frequency 4000000
```
If you want to set the gain manually, you can do that with
```bash
iio_attr -i -c ad9361-phy voltage0 gain_control_mode manual
iio_attr -i -c ad9361-phy voltage0 hardwaregain 60
```
We are now ready to give stream1090 a first go with 4 Msps and no upsampling. The output for now will go to waste.
```bash
iio_readdev -b 15360 cf-ad9361-lpc voltage0 voltage1 | /root/stream1090 -s 4 > /dev/null
```
This should give you the usual stream1090 stats screen. 

#### A note on the sampling frequency
As you know, the ant can deliever a lot more than 4 Msps. However, the ARM processor is slow and stream1090 has to do plenty of work. You can also do upsampling with stream1090. 

However, and this is **very important**: You will not notice directly when the CPU cannot keep up anymore. Also stream1090 will not notice. Your message rate will drop, but everyone keeps working as nothing has happened. The best way is to open a second SSH session and check the situation with ```top```.

### Getting the output out
While it looks silly to dedicate a small section to this, it turned out that it is not easy to get the AVR messages from the ant out into the big wide world.
The problem is that the minimal Buildroot OS does not include any network utilities like `nc` or `socat`. So we cannot pipe stream1090's standard output somewhere.

The only solution so far is to do it via SSH from the host side. Basically, we start stream1090 in an SSH session and pipe the standard output of that session
into socat. Since settings like frequency and sample rate are getting wiped by a restart, we chain everything into one command. Assuming you have readsb running on your Raspberry Pi, we do

```bash
ssh root@192.168.1.10 "iio_attr -o -c ad9361-phy altvoltage0 frequency 1090000000 && iio_attr -i -c ad9361-phy voltage0 sampling_frequency 4000000 && iio_readdev -b 15360 cf-ad9361-lpc voltage0 voltage1 | /root/stream1090 -s 4" | socat -u - TCP4:raspberrypi:30001
```

Note that this command only pipes the standard ouput of stream1090 into socat which are the messages in AVR format and not any other output like the stats screen. 

## Appendix: Handling the Annoying SSH Key Reset

As mentioned earlier, the AntSDR filesystem is entirely volatile. When the board restarts, it generates fresh SSH host keys. This will cause your host PC to aggressively complain with a `WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!` error every single boot cycle.

To permanently ignore this security warning specifically for the AntSDR, you can add this block to your local machine's configuration file at `~/.ssh/config`:

```text
Host 192.168.1.10
    User root
    StrictHostKeyChecking no
    UserKnownHostsFile /dev/null
    LogLevel ERROR
```
The `LogLevel ERROR` flag completely silences the automated host additions, ensuring your upload and connection scripts run cleanly every single time without requiring manual intervention via `ssh-keygen`.