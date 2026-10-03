#!/usr/bin/env python3
"""
filter_editor.py — interactive FIR filter designer.

Drag control points to shape a target frequency response; the tool runs
scipy.signal.firwin2 against your points live and overlays the ACTUAL
achievable response for the given tap count, so you can see directly how
much a low tap count (e.g. 15 for stream1090's software filter, 32 for the
AD9361 hardware FIR) forces a wide transition band / ripple versus what
you asked for.

No export — this is purely a design/exploration tool. Read tap values off
the table (standard copy/paste works) if you need them elsewhere.

Requires: PySide6, pyqtgraph, numpy, scipy
    pip install PySide6 pyqtgraph numpy scipy

Performance note: filter design (firwin2) and response computation (freqz)
on 15-128 taps is sub-millisecond, so there is no need to debounce drag
events — the cost here is entirely in redraw, which is why this uses
pyqtgraph (fast, built for live-updating plots) rather than matplotlib.
"""

import sys

import numpy as np
import subprocess
from scipy.signal import firwin2, freqz

import json
from PySide6 import QtCore, QtWidgets
import pyqtgraph as pg


MIN_DB = -80.0
MAX_DB = 5.0


class FilterEditor(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("FIR Filter Editor")
        self.resize(1100, 650)

        # Control points: list of [freq_norm (0..1, 1 = Nyquist), gain_db]
        # Sorted by freq_norm at all times.
        self.points = [
            [0.0, 0.0],
            [0.33, 0.0],
            [0.45, -6.0],
            [0.6, -40.0],
            [1.0, -60.0],
        ]
        self.target_items = []  # parallel list of pg.TargetItem

        self.fs = 8_000_000.0
        self.numtaps = 24
        self.window = "hamming"

        self._create_menu()
        self._build_ui()
        self._rebuild_targets()
        self._recompute()

    # ------------------------------------------------------------------ Menu
    def _create_menu(self):
        file_menu = self.menuBar().addMenu("&File")

        load_action = file_menu.addAction("Load Preset...")
        load_action.triggered.connect(self._load_preset)

        save_action = file_menu.addAction("Save Preset...")
        save_action.triggered.connect(self._save_preset)

        file_menu.addSeparator()

        exit_action = file_menu.addAction("Exit")
        exit_action.triggered.connect(self.close)
    # ------------------------------------------------------------------ UI

    def _build_ui(self):
        central = QtWidgets.QWidget()
        layout = QtWidgets.QHBoxLayout(central)
        self.setCentralWidget(central)

        # --- plot -------------------------------------------------------
        pg.setConfigOptions(antialias=True)
        self.plot_widget = pg.PlotWidget()
        self.plot = self.plot_widget.getPlotItem()
        self.plot.setLabel("bottom", "Frequency", units="Hz")
        self.plot.setLabel("left", "Gain", units="dB")
        self.plot.setYRange(MIN_DB, MAX_DB)
        self.plot.showGrid(x=True, y=True, alpha=0.3)
        self.plot.setMenuEnabled(False)

        self.target_curve = self.plot.plot(
            pen=pg.mkPen((120, 170, 255), width=1, style=QtCore.Qt.DashLine),
            name="target",
        )
        self.actual_curve = self.plot.plot(
            pen=pg.mkPen((255, 180, 60), width=2),
            name="actual response",
        )
        self.plot.addLegend()

        self.plot_widget.scene().sigMouseClicked.connect(self._on_scene_clicked)

        left_side = QtWidgets.QWidget()
        left_layout = QtWidgets.QVBoxLayout(left_side)

        left_layout.addWidget(self.plot_widget, stretch=1)

        # RUN 
        cmd_row = QtWidgets.QHBoxLayout()

        self.command_edit = QtWidgets.QLineEdit()
        self.command_edit.setText("cat ../../samples/ant_bw4_8m_0.raw | ../build/stream1090 -s 8 -u 24 -f fir_edit_taps.txt > /dev/null")

        self.run_button = QtWidgets.QPushButton("Run")
        self.run_button.clicked.connect(self._run_stream1090)

        cmd_row.addWidget(self.command_edit, stretch=1)
        cmd_row.addWidget(self.run_button)

        left_layout.addWidget(QtWidgets.QLabel("Command:"))
        left_layout.addLayout(cmd_row)
        layout.addWidget(left_side, stretch=1)

        # --- side panel ---------------------------------------------------
        panel = QtWidgets.QWidget()
        panel.setFixedWidth(260)
        form = QtWidgets.QVBoxLayout(panel)

        grid = QtWidgets.QFormLayout()
        self.numtaps_spin = QtWidgets.QSpinBox()
        self.numtaps_spin.setRange(3, 256)
        self.numtaps_spin.setValue(self.numtaps)
        self.numtaps_spin.valueChanged.connect(self._on_numtaps_changed)
        grid.addRow("Taps:", self.numtaps_spin)

        self.make_even_checkbox = QtWidgets.QCheckBox(
            "Make symmetric even (AntSDR)"
        )
        self.make_even_checkbox.toggled.connect(self._recompute)
        grid.addRow(self.make_even_checkbox)

        self.fs_spin = QtWidgets.QDoubleSpinBox()
        self.fs_spin.setRange(1_000, 200_000_000)
        self.fs_spin.setDecimals(0)
        self.fs_spin.setSingleStep(100_000)
        self.fs_spin.setValue(self.fs)
        self.fs_spin.valueChanged.connect(self._on_fs_changed)
        grid.addRow("Sample rate (Hz):", self.fs_spin)

        self.window_combo = QtWidgets.QComboBox()
        self.window_combo.addItems(
            ["hamming", "hann", "blackman", "boxcar", "bartlett", "kaiser"]
        )
        self.window_combo.currentTextChanged.connect(self._on_window_changed)
        grid.addRow("Window:", self.window_combo)

        form.addLayout(grid)

        hint = QtWidgets.QLabel(
            "Double-click empty space to add a point.\n"
            "Right-click a point to remove it.\n"
            "Drag points to reshape the target response."
        )
        hint.setWordWrap(True)
        hint.setStyleSheet("color: gray;")
        form.addWidget(hint)

        form.addWidget(QtWidgets.QLabel("Taps:"))
        self.tap_text = QtWidgets.QPlainTextEdit()
        self.tap_text.setReadOnly(True)

        font = self.tap_text.font()
        font.setFamily("Courier New")
        self.tap_text.setFont(font)
        form.addWidget(self.tap_text)
       
        layout.addWidget(panel)

    # ------------------------------------------------------------- points

    def _rebuild_targets(self):
        for item in self.target_items:
            self.plot.removeItem(item)
        self.target_items = []

        for fnorm, gain in self.points:
            item = pg.TargetItem(
                pos=(fnorm * self.fs / 2.0, gain),
                size=10,
                movable=True,
            )
            item.sigPositionChanged.connect(self._on_point_moved)
            self.plot.addItem(item)
            self.target_items.append(item)

        self.plot.setXRange(0, self.fs / 2.0)

    def _on_point_moved(self, item):
        idx = self.target_items.index(item)
        x, y = item.pos().x(), item.pos().y()

        y = max(MIN_DB, min(MAX_DB, y))
        fnorm = max(0.0, min(1.0, x / (self.fs / 2.0)))

        # keep endpoints pinned on the frequency axis, movable in gain only
        if idx == 0:
            fnorm = 0.0
        elif idx == len(self.points) - 1:
            fnorm = 1.0

        self.points[idx] = [fnorm, y]

        # if the drag crossed a neighbour, re-sort everything and rebuild
        # (keeps interaction simple; cheap enough to just redo it)
        order = sorted(range(len(self.points)), key=lambda i: self.points[i][0])
        if order != list(range(len(self.points))):
            self.points = [self.points[i] for i in order]
            self._rebuild_targets()
        else:
            # snap the visual position to the clamped values without
            # retriggering this handler
            item.blockSignals(True)
            item.setPos((fnorm * self.fs / 2.0, y))
            item.blockSignals(False)

        self._recompute()

    def _on_scene_clicked(self, ev):
        vb = self.plot.getViewBox()
        scene_pos = ev.scenePos()
        if not vb.sceneBoundingRect().contains(scene_pos):
            return
        view_pos = vb.mapSceneToView(scene_pos)
        x, y = view_pos.x(), view_pos.y()

        # find nearest existing point in pixel space
        nearest_idx, nearest_dist = None, None
        for i, item in enumerate(self.target_items):
            ipos = item.pos()
            dist = vb.mapViewToScene(
                QtCore.QPointF(ipos.x(), ipos.y())
            ) - scene_pos
            d = (dist.x() ** 2 + dist.y() ** 2) ** 0.5
            if nearest_dist is None or d < nearest_dist:
                nearest_dist, nearest_idx = d, i

        HIT_PX = 15

        if ev.button() == QtCore.Qt.RightButton:
            if nearest_dist is not None and nearest_dist < HIT_PX:
                if 0 < nearest_idx < len(self.points) - 1:
                    # endpoints (0 and last) are kept so firwin2 always has
                    # a full 0..Nyquist definition
                    del self.points[nearest_idx]
                    self._rebuild_targets()
                    self._recompute()
            return

        if ev.double() and ev.button() == QtCore.Qt.LeftButton:
            if nearest_dist is not None and nearest_dist < HIT_PX:
                return  # double-click landed on an existing point, ignore
            fnorm = max(0.0, min(1.0, x / (self.fs / 2.0)))
            gain = max(MIN_DB, min(MAX_DB, y))
            self.points.append([fnorm, gain])
            self.points.sort(key=lambda p: p[0])
            self._rebuild_targets()
            self._recompute()

    # ------------------------------------------------------------ options

    def _on_numtaps_changed(self, value):
        self.numtaps = value
        self._recompute()

    def _on_fs_changed(self, value):
        self.fs = value
        self._rebuild_targets()
        self._recompute()

    def _on_window_changed(self, text):
        self.window = text
        self._recompute()

    # --------------------------------------------------------- computation
    def make_symmetric_even(taps):
        n = len(taps)

        if n % 2 == 1:
            center = n // 2
            taps = np.insert(taps, center + 1, taps[center])

        return taps
    
    def _recompute(self):
        freqs = np.array([p[0] for p in self.points])
        gains_db = np.array([p[1] for p in self.points])
        gains_lin = 10 ** (gains_db / 20.0)

        # firwin2 requires freq[0] == 0 and freq[-1] == 1
        if freqs[0] != 0.0:
            freqs = np.concatenate(([0.0], freqs))
            gains_lin = np.concatenate(([gains_lin[0]], gains_lin))

        if freqs[-1] != 1.0:
            freqs = np.concatenate((freqs, [1.0]))
            gains_lin = np.concatenate((gains_lin, [gains_lin[-1]]))

        numtaps = self.numtaps

        try:
            if self.window == "kaiser":
                taps = firwin2(
                    numtaps,
                    freqs,
                    gains_lin,
                    window=("kaiser", 6),
                )
            else:
                taps = firwin2(
                    numtaps,
                    freqs,
                    gains_lin,
                    window=self.window,
                )

        except ValueError:
            # firwin2 restriction:
            # even numtaps + nonzero Nyquist gain
            if self.window == "kaiser":
                taps = firwin2(
                    numtaps + 1,
                    freqs,
                    gains_lin,
                    window=("kaiser", 6),
                )
            else:
                taps = firwin2(
                    numtaps + 1,
                    freqs,
                    gains_lin,
                    window=self.window,
                )

        # AntSDR upload behaviour:
        # duplicate centre tap to force even-length symmetry
        if self.make_even_checkbox.isChecked():
            if len(taps) % 2 == 1:
                center = len(taps) // 2
                taps = np.insert(taps, center + 1, taps[center])

        w, h = freqz(taps, worN=2048)

        freq_hz = w / np.pi * (self.fs / 2.0)
        mag_db = 20 * np.log10(np.maximum(np.abs(h), 1e-12))

        target_x = freqs * self.fs / 2.0
        target_y = 20 * np.log10(np.maximum(gains_lin, 1e-12))

        self.target_curve.setData(target_x, target_y)
        self.actual_curve.setData(freq_hz, mag_db)

        self._update_tap_table(taps)
        self.current_taps = taps

    def _update_tap_table(self, taps):    
        text = "\n".join(
            f"{t:.12f}"
            for t in taps
        )
        self.tap_text.setPlainText(text)

    def _save_preset(self):
        filename, _ = QtWidgets.QFileDialog.getSaveFileName(
            self,
            "Save Filter Preset",
            "",
            "Filter Presets (*.json);;"
            "Textfile (*.txt);;"  
            "All Files (*)"
        )

        if not filename:
            return

        data = {
            "sample_rate": self.fs,
            "numtaps": self.numtaps,
            "window": self.window,
            "command": self.command_edit.text(),
            "make_symmetric_even": self.make_even_checkbox.isChecked(),
            "points": self.points,
        }

        with open(filename, "w") as f:
            json.dump(data, f, indent=2)

    def _load_preset(self):
        filename, _ = QtWidgets.QFileDialog.getOpenFileName(
            self,
            "Load Filter Preset",
            "",
            "Filter Presets (*.json);;"
            "Textfile (*.txt);;"  
            "All Files (*)"
        )

        if not filename:
            return

        with open(filename, "r") as f:
            data = json.load(f)

        self.fs = data["sample_rate"]
        self.numtaps = data["numtaps"]
        self.window = data["window"]
        self.points = data["points"]

        self.command_edit.setText(data.get("command", ""))
        self.make_even_checkbox.setChecked(data.get("make_symmetric_even"))
        self.fs_spin.setValue(self.fs)
        self.numtaps_spin.setValue(self.numtaps)
        self.window_combo.setCurrentText(self.window)

        self._rebuild_targets()
        self._recompute()
            
    def _run_stream1090(self):
        if not hasattr(self, "current_taps"):
            return

        with open("fir_edit_taps.txt", "w") as f:
            for tap in self.current_taps:
                f.write(f"{tap:.12f}\n")

        cmd = self.command_edit.text().strip()

        try:
            subprocess.Popen(
                cmd,
                shell=True,
            )
        except Exception as e:
            QtWidgets.QMessageBox.critical(
                self,
                "Stream1090",
                str(e),
            )

def main():
    app = QtWidgets.QApplication(sys.argv)
    win = FilterEditor()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()

