#!/usr/bin/env python3

import numpy as np
import matplotlib.pyplot as plt
from scipy.signal import firwin2, freqz


class Firwin2Designer:

    def __init__(self):

        self.taps = 24

        #
        # firwin2 control points
        #
        self.freq = np.array([
            0.0,
            0.25,
            0.50,
            0.75,
            1.00,
        ])

        self.gain = np.array([
            1.0,
            1.0,
            0.5,
            0.2,
            0.0,
        ])

        self.active = None

        self.fig, (
            self.ax_control,
            self.ax_impulse,
            self.ax_freq,
        ) = plt.subplots(1, 3, figsize=(16, 6))

        #
        # connect events
        #
        self.fig.canvas.mpl_connect(
            "button_press_event",
            self.on_press,
        )

        self.fig.canvas.mpl_connect(
            "button_release_event",
            self.on_release,
        )

        self.fig.canvas.mpl_connect(
            "motion_notify_event",
            self.on_motion,
        )

        self.redraw()

    def build_filter(self):

        return firwin2(
            self.taps,
            self.freq,
            self.gain,
        )

    def redraw(self):

        self.ax_control.clear()
        self.ax_impulse.clear()
        self.ax_freq.clear()

        #
        # control points
        #
        self.ax_control.plot(
            self.freq,
            self.gain,
            "-o",
            color="red",
            markersize=10,
        )

        self.ax_control.set_title(
            "firwin2 Control Points"
        )

        self.ax_control.set_xlabel(
            "Normalized Frequency"
        )

        self.ax_control.set_ylabel(
            "Gain"
        )

        self.ax_control.set_xlim(
            -0.02,
            1.02,
        )

        self.ax_control.set_ylim(
            -0.05,
            1.05,
        )

        self.ax_control.grid(True)

        #
        # FIR
        #
        h = self.build_filter()
        print("-------------------------------")
        for x in h:
            print(x)
        print("-------------------------------")
        self.ax_impulse.plot(
            h,
            marker="o",
        )

        self.ax_impulse.set_title(
            f"Impulse Response ({self.taps} taps)"
        )

        self.ax_impulse.grid(True)

        #
        # Frequency response
        #
        w, H = freqz(
            h,
            worN=256,
        )

        mag = 20 * np.log10(
            np.maximum(np.abs(H), 1e-12)
        )

        self.ax_freq.plot(
            w / np.pi,
            mag,
        )

        self.ax_freq.set_title(
            "Frequency Response"
        )

        self.ax_freq.set_xlabel(
            "Normalized Frequency"
        )

        self.ax_freq.set_ylabel(
            "Magnitude (dB)"
        )

        self.ax_freq.set_xlim(
            0,
            1,
        )

        self.ax_freq.grid(True)

        self.fig.tight_layout()
        self.fig.canvas.draw_idle()

    def on_press(self, event):

        if event.inaxes != self.ax_control:
            return

        if event.xdata is None:
            return

        if event.ydata is None:
            return

        dist = (
            (self.freq - event.xdata) ** 2
            + (self.gain - event.ydata) ** 2
        )

        idx = np.argmin(dist)

        if np.sqrt(dist[idx]) < 0.05:
            self.active = idx

    def on_release(self, event):
        self.active = None

    def on_motion(self, event):

        if self.active is None:
            return

        if event.inaxes != self.ax_control:
            return

        if event.xdata is None:
            return

        if event.ydata is None:
            return

        idx = self.active

        #
        # endpoints fixed in frequency
        #
        if idx == 0:

            self.freq[idx] = 0.0

        elif idx == len(self.freq) - 1:

            self.freq[idx] = 1.0

        else:

            left = self.freq[idx - 1] + 0.01
            right = self.freq[idx + 1] - 0.01

            self.freq[idx] = np.clip(
                event.xdata,
                left,
                right,
            )

        self.gain[idx] = np.clip(
            event.ydata,
            0.0,
            2.0,
        )

        self.redraw()


if __name__ == "__main__":

    designer = Firwin2Designer()

    plt.show()
