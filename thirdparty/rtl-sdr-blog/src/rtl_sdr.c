/*
 * rtl-sdr, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include "getopt/getopt.h"
#endif

#include "rtl-sdr.h"
#include "convenience/convenience.h"

#define DEFAULT_SAMPLE_RATE		2048000
#define DEFAULT_BUF_LENGTH		(16 * 16384)
#define MINIMAL_BUF_LENGTH		512
#define MAXIMAL_BUF_LENGTH		(256 * 16384)

static int do_exit = 0;
static uint32_t bytes_to_read = 0;
static rtlsdr_dev_t *dev = NULL;

static int verbose_tuner_bandwidth(rtlsdr_dev_t *dev, uint32_t bw)
{
	int r;

	r = rtlsdr_set_tuner_bandwidth(dev, bw);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set tuner bandwidth to %u Hz.\n", bw);
	else
		fprintf(stderr, "Tuner bandwidth set to %u Hz.\n", bw);
	return r;
}

static int verbose_stage_gain(rtlsdr_dev_t *dev, const char *stage, int gain,
			      int (*set_gain)(rtlsdr_dev_t *, int))
{
	int r;

	if (gain < 0 || gain > 15) {
		fprintf(stderr, "WARNING: %s gain index %d out of range 0..15.\n", stage, gain);
		return -1;
	}

	r = set_gain(dev, gain);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set %s gain index %d.\n", stage, gain);
	else
		fprintf(stderr, "%s gain index set to %d.\n", stage, gain);
	return r;
}

static int verbose_agc(rtlsdr_dev_t *dev, int on)
{
	int r;

	r = rtlsdr_set_agc_mode(dev, on);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to %s AGC.\n", on ? "enable" : "disable");
	else
		fprintf(stderr, "AGC %s.\n", on ? "enabled" : "disabled");
	return r;
}

static int verbose_bias_tee(rtlsdr_dev_t *dev, int on)
{
	int r;

	r = rtlsdr_set_bias_tee(dev, on);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to %s bias tee.\n", on ? "enable" : "disable");
	else
		fprintf(stderr, "Bias tee %s.\n", on ? "enabled" : "disabled");
	return r;
}

void usage(void)
{
	fprintf(stderr,
		"rtl_sdr, an I/Q recorder for RTL2832 based DVB-T receivers\n\n"
		"Usage:\t -f frequency_to_tune_to [Hz]\n"
		"\t[-s samplerate (default: 2048000 Hz)]\n"
		"\t[-d device_index (default: 0)]\n"
		"\t[-g gain (default: 0 for auto)]\n"
		"\t[-B tuner bandwidth in Hz, 0 = auto (default: 0)]\n"
		"\t[-L LNA gain index 0..15 (default: auto)]\n"
		"\t[-M mixer gain index 0..15 (default: auto)]\n"
		"\t[-V VGA gain index 0..15 (default: auto)]\n"
		"\t[-A enable digital AGC (default: off)]\n"
		"\t[-O enable offset tuning (default: off)]\n"
		"\t[-T enable bias tee (default: off)]\n"
		"\t[-p ppm_error (default: 0)]\n"
		"\t[-b output_block_size (default: 16 * 16384)]\n"
		"\t[-n number of samples to read (default: 0, infinite)]\n"
		"\t[-S force sync output (default: async)]\n"
		"\t[-D enable direct sampling (default: off)]\n"
		"\tfilename (a '-' dumps samples to stdout)\n\n");
	exit(1);
}

#ifdef _WIN32
BOOL WINAPI
sighandler(int signum)
{
	if (CTRL_C_EVENT == signum) {
		fprintf(stderr, "Signal caught, exiting!\n");
		do_exit = 1;
		rtlsdr_cancel_async(dev);
		return TRUE;
	}
	return FALSE;
}
#else
static void sighandler(int signum)
{
	signal(SIGPIPE, SIG_IGN);
	fprintf(stderr, "Signal caught, exiting!\n");
	do_exit = 1;
	rtlsdr_cancel_async(dev);
}
#endif

static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx)
{
	if (ctx) {
		if (do_exit)
			return;

		if ((bytes_to_read > 0) && (bytes_to_read < len)) {
			len = bytes_to_read;
			do_exit = 1;
			rtlsdr_cancel_async(dev);
		}

		if (fwrite(buf, 1, len, (FILE*)ctx) != len) {
			fprintf(stderr, "Short write, samples lost, exiting!\n");
			rtlsdr_cancel_async(dev);
		}

		if (bytes_to_read > 0)
			bytes_to_read -= len;
	}
}

int main(int argc, char **argv)
{
#ifndef _WIN32
	struct sigaction sigact;
#endif
	char *filename = NULL;
	int n_read;
	int r, opt;
	int gain = 0;
	int lna_gain = -1;
	int mixer_gain = -1;
	int vga_gain = -1;
	int bias_tee = 0;
	int offset_tuning = 0;
	int agc = 0;
	int ppm_error = 0;
	int direct_sampling = 0;
	int sync_mode = 0;
	FILE *file;
	uint8_t *buffer;
	int dev_index = 0;
	int dev_given = 0;
	uint32_t frequency = 100000000;
	uint32_t samp_rate = DEFAULT_SAMPLE_RATE;
	uint32_t tuner_bandwidth = 0;
	uint32_t out_block_size = DEFAULT_BUF_LENGTH;

	while ((opt = getopt(argc, argv, "d:f:g:s:b:n:p:B:L:M:V:SDOAT")) != -1) {
		switch (opt) {
		case 'd':
			dev_index = verbose_device_search(optarg);
			dev_given = 1;
			break;
		case 'f':
			frequency = (uint32_t)atofs(optarg);
			break;
		case 'g':
			gain = (int)(atof(optarg) * 10); /* tenths of a dB */
			break;
		case 'B':
			tuner_bandwidth = (uint32_t)atofs(optarg);
			break;
		case 'L':
			lna_gain = atoi(optarg);
			break;
		case 'M':
			mixer_gain = atoi(optarg);
			break;
		case 'V':
			vga_gain = atoi(optarg);
			break;
		case 'A':
			agc = 1;
			break;
		case 'O':
			offset_tuning = 1;
			break;
		case 'T':
			bias_tee = 1;
			break;
		case 's':
			samp_rate = (uint32_t)atofs(optarg);
			break;
		case 'p':
			ppm_error = atoi(optarg);
			break;
		case 'b':
			out_block_size = (uint32_t)atof(optarg);
			break;
		case 'n':
			bytes_to_read = (uint32_t)atof(optarg) * 2;
			break;
		case 'S':
			sync_mode = 1;
			break;
		case 'D':
			direct_sampling = 1;
			break;
		default:
			usage();
			break;
		}
	}

	if (argc <= optind) {
		usage();
	} else {
		filename = argv[optind];
	}

	if(out_block_size < MINIMAL_BUF_LENGTH ||
	   out_block_size > MAXIMAL_BUF_LENGTH ){
		fprintf(stderr,
			"Output block size wrong value, falling back to default\n");
		fprintf(stderr,
			"Minimal length: %u\n", MINIMAL_BUF_LENGTH);
		fprintf(stderr,
			"Maximal length: %u\n", MAXIMAL_BUF_LENGTH);
		out_block_size = DEFAULT_BUF_LENGTH;
	}

	buffer = malloc(out_block_size * sizeof(uint8_t));

	if (!dev_given) {
		dev_index = verbose_device_search("0");
	}

	if (dev_index < 0) {
		exit(1);
	}

	r = rtlsdr_open(&dev, (uint32_t)dev_index);
	if (r < 0) {
		fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
		exit(1);
	}
#ifndef _WIN32
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);
#else
	SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#endif

	/* Set direct sampling */
        if (direct_sampling)
                verbose_direct_sampling(dev, 2);

	/* Set the sample rate */
	verbose_set_sample_rate(dev, samp_rate);

	/* Set the frequency */
	verbose_set_frequency(dev, frequency);

	/* Set the tuner bandwidth, 0 lets the library choose it */
	if (tuner_bandwidth > 0)
		verbose_tuner_bandwidth(dev, tuner_bandwidth);

	if (lna_gain >= 0 || mixer_gain >= 0 || vga_gain >= 0) {
		/* Per-stage gain only exists for the R82xx tuners and needs
		 * manual mode. A combined -g is applied first and then
		 * overridden by whichever stages were given. */
		if (rtlsdr_set_tuner_gain_mode(dev, 1) < 0)
			fprintf(stderr, "WARNING: Failed to enable manual gain.\n");
		if (0 != gain) {
			gain = nearest_gain(dev, gain);
			verbose_gain_set(dev, gain);
		}
		if (lna_gain >= 0)
			verbose_stage_gain(dev, "LNA", lna_gain, rtlsdr_r82xx_set_lna_gain);
		if (mixer_gain >= 0)
			verbose_stage_gain(dev, "Mixer", mixer_gain, rtlsdr_r82xx_set_mixer_gain);
		if (vga_gain >= 0)
			verbose_stage_gain(dev, "VGA", vga_gain, rtlsdr_r82xx_set_vga_gain);
	} else if (0 == gain) {
		 /* Enable automatic gain */
		verbose_auto_gain(dev);
	} else {
		/* Enable manual gain */
		gain = nearest_gain(dev, gain);
		verbose_gain_set(dev, gain);
	}

	if (agc)
		verbose_agc(dev, 1);
	if (offset_tuning)
		verbose_offset_tuning(dev);
	if (bias_tee)
		verbose_bias_tee(dev, 1);

	verbose_ppm_set(dev, ppm_error);

	if(strcmp(filename, "-") == 0) { /* Write samples to stdout */
		file = stdout;
#ifdef _WIN32
		_setmode(_fileno(stdin), _O_BINARY);
#endif
	} else {
		file = fopen(filename, "wb");
		if (!file) {
			fprintf(stderr, "Failed to open %s\n", filename);
			goto out;
		}
	}

	/* Reset endpoint before we start reading from it (mandatory) */
	verbose_reset_buffer(dev);

	if (sync_mode) {
		fprintf(stderr, "Reading samples in sync mode...\n");
		while (!do_exit) {
			r = rtlsdr_read_sync(dev, buffer, out_block_size, &n_read);
			if (r < 0) {
				fprintf(stderr, "WARNING: sync read failed.\n");
				break;
			}

			if ((bytes_to_read > 0) && (bytes_to_read < (uint32_t)n_read)) {
				n_read = bytes_to_read;
				do_exit = 1;
			}

			if (fwrite(buffer, 1, n_read, file) != (size_t)n_read) {
				fprintf(stderr, "Short write, samples lost, exiting!\n");
				break;
			}

			if ((uint32_t)n_read < out_block_size) {
				fprintf(stderr, "Short read, samples lost, exiting!\n");
				break;
			}

			if (bytes_to_read > 0)
				bytes_to_read -= n_read;
		}
	} else {
		fprintf(stderr, "Reading samples in async mode...\n");
		r = rtlsdr_read_async(dev, rtlsdr_callback, (void *)file,
				      0, out_block_size);
	}

	if (do_exit)
		fprintf(stderr, "\nUser cancel, exiting...\n");
	else
		fprintf(stderr, "\nLibrary error %d, exiting...\n", r);

	if (file != stdout)
		fclose(file);

	rtlsdr_close(dev);
	free (buffer);
out:
	return r >= 0 ? r : -r;
}
