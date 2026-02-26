#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2025 Martin Gronemann
#
# This file is part of stream1090 and is licensed under the GNU General
# Public License v3.0. See the top-level LICENSE file for details.
#

import argparse
import numpy as np
from scipy.optimize import differential_evolution
from scipy.signal import firwin2
import subprocess

# ============================================================
#  CLI
# ============================================================

def parse_args():
    p = argparse.ArgumentParser(
        description="Differential Evolution FIR optimizer for stream1090"
    )

    p.add_argument("--data", required=True,
                   help="Path to raw IQ sample file")

    p.add_argument("--fs", type=int, required=True,
                   help="Input sample rate (Hz)")

    p.add_argument("--fs-up", type=int, required=True,
                   help="Upsampled rate (Hz)")

    p.add_argument("--taps", type=int, default=15,
                   help="Number of FIR taps")

    p.add_argument("--k", type=int, default=9,
                   help="Number of interior gain points")

    p.add_argument("--margin", type=float, default=0.5,
                   help="Initial margin around center seed")

    p.add_argument("--log", default="de_log.txt",
                   help="Log file path")

    p.add_argument("--maxiter", type=int, default=40)
    p.add_argument("--popsize", type=int, default=20)
    p.add_argument("--alpha", type=float, default=2.0)

    p.add_argument("--bounds-min", type=float, default=-2.0)
    p.add_argument("--bounds-max", type=float, default=2.0)

    return p.parse_args()


args = parse_args()

# ============================================================
#  Config (center seed fixed for now)
# ============================================================

DATA_PATH = args.data
FILTER_PATH = "./diff_evolve_fir_temp.txt"
FS = args.fs
FS_UP = args.fs_up
NUMTAPS = args.taps
K = args.k

#CENTER_SEED = [-0.30480078491692353, 1.191383872925207,
#               -0.8946397241760973, 0.7031294489517014,
#               -0.29172370808888537]

# CENTER_SEED = [ 0.1124400431237176, 0.2945768572815861, 0.4467455514322548, -0.15850105688460994, 0.7284096361116326 ]
#CENTER_SEED =[-0.12391788663154957, 0.5152763990510445, -0.008532948239352134, 0.18314696648401085, 0.4110798978894785]
#CENTER_SEED = [-0.23812380372220648, 1.2441888114890145, -0.4746853326196218, 0.7235454630229908, -0.41116151597374584]

# CENTER_SEED = [0.7385309183821223, 0.6114527033450945, -0.3917265124929632, 0.7266782443794657, 0.29960379460549413, -0.03613019498971792, 0.6144657445878667, 0.30430007848809293, 0.6083306413129055]
# CENTER_SEED = [0.8953316451605866, 0.43872496156003293, -0.3840206096448354, 0.8786481144040845, 0.17848275307716915, -0.06846413439288586, 0.6507569613982499, 0.17612725705609528, 0.7751576518630502]
# CENTER_SEED = [0.7662608273353088, -0.15335230367640687, -0.05194925649066817, 1.51212319552764, -0.4345460488485211, -0.19636243576359802, 0.03134797456802446, -0.7152770541213661, 0.8084580490735267]
# CENTER_SEED = [0.8951580148026792, -0.4741858577323217, 0.08507478624116516, 1.5640528956520119, -0.5947692764810215, 0.06137415850011474, 0.38673004133632116, -0.6167445600079496, 0.6045396626573174]
#CENTER_SEED = [0.8765755453193712, -0.29313973010044625, 0.03207607717724785, 1.6135135048069866, -0.5719526653753269, 0.06606279933784028, 0.3580928180806818, -0.7409875124262786, 0.5696702674735069]

# 6 MPS
# CENTER_SEED = [1.132628390718036, -0.5713013809370806, 0.1942756236647941, 1.4017867435841338, -0.7021318578202541, 0.19823534836436946, 0.1509537204874616, -0.6307461177600584, 0.2033243976841408]
# CENTER_SEED = [1.2079943814023073, -0.7571321547407389, 0.35435822965117775, 1.4388135377501774, -0.5313934278706354, 0.3820177718513053, 0.2231856471416289, -0.74755593103677, 0.14887973362913182]
# CENTER_SEED = [1.36465812088526, -0.9316954656409512, 0.5024379101124314, 1.4550828477873121, -0.3836934452643007, 0.503511625223503, 0.3837373447175557, -0.8895703488307819, 0.2642591783021267]
# CENTER_SEED = [1.0159007917372314, -0.7854574795756804, 0.614547133483366, 1.1002955447928109, -0.38301963451268645, 0.9087679923143273, -0.0951244282664174, -0.7647533426443369, 0.40473526611574795]

# 2.56
# CENTER_SEED = [1.1054968766228943, 0.46413033088940703, -0.21895330152482792, 1.102050765068151, -0.13133078472716953, 0.4661397300549525, 0.5625421438179954, 0.3626185876587192, 0.9071282683560457]
# CENTER_SEED = [1.110081294712715, 0.45263508592318974, -0.23281821241787481, 1.089989797332681, -0.12335865560588051, 0.48761908218137623, 0.5131726889454095, 0.37689293718926203, 0.8599152582876056]
# CENTER_SEED = [1.0739335339066483, 0.4041926902831805, -0.2010769924462814, 1.0731499684738497, -0.11375588115919864, 0.4519689975424281, 0.49975354200602895, 0.4033854129709088, 0.8659253195651582]
# CENTER_SEED = [1.055867709079552, 0.44473749071167906, -0.21236569029378588, 1.0895055566019645, -0.12106583722598437, 0.46178970293227345, 0.4611787407181642, 0.4162467840945036, 0.8205024363691791]
# CENTER_SEED = [1.1576877400005454, 0.4748119519320697, -0.16798808932203993, 1.0638294953356093, -0.11066882809900488, 0.4520383423236834, 0.5089569553553485, 0.3962739302890628, 0.9048967623825177]
# CENTER_SEED = [1.15606431547649, 0.3944644011967393, -0.14208003582609668, 1.102786962557022, -0.10341066946483495, 0.37297235575227045, 0.5291315251301256, 0.3343523456699447, 0.9495577036492571]
CENTER_SEED = [1.1316168334764158, 0.20308185445041663, -0.18880172571207673, 1.1015299202753106, -0.13136240939270896, 0.3352500341043928, 0.5515897246310204, 0.30513096033801856, 0.7708951792896465]
# US 256 Jim
#CENTER_SEED = [1.0991245379930517, 0.0664570338507843, -0.09823450832418411, 1.0635130601568472, -0.1990555972293719, 0.3894335167500704, 0.46673462161029255, 0.3449599682400649, 0.6316060581049451]
#CENTER_SEED = [0.9998844780546715, 0.22277578580152152, -0.2362766049844264, 0.9982140839128525, -0.001980525366301389, 0.3424896035758465, 0.40883721978840065, 0.2892085992373185, 0.8217956612961002]


# 2.4
# CENTER_SEED = [1.1518216514701056, 0.46794292344499944, -0.22308585167037603, 1.090965367621634, -0.07361239044789661, 0.37574737877733705, 0.4398671446138982, 0.3270148206691401, 0.8451603076481947]
# CENTER_SEED = [1.0704860746703018, 0.5028093313972919, -0.2111190031969827, 0.9964997439073392, -0.014012866854529321, 0.28705417831763314, 0.40309041015291647, 0.3766383374312689, 0.7898864281002824]
# CENTER_SEED = [1.1021649694556346, 0.4424178874195193, -0.280665885311384, 1.081335945661291, -0.06602527162468749, 0.2702952028129114, 0.40134490931963307, 0.3828534395497509, 0.7578939306373056]
# CENTER_SEED = [1.0971959333209598, 0.37977342480575715, -0.2234298862227773, 1.0293514002857396, -0.08053530533821397, 0.3565416707299342, 0.49252346790957274, 0.35662002862640724, 0.7986136364693184]
# CENTER_SEED = [1.1494341351599182, 0.4570778836139672, -0.26969485436036755, 1.0658666225724556, -0.03575567283526529, 0.40222271389290765, 0.4251981647436547, 0.3691542269271819, 0.845857523913327]
# CENTER_SEED = [1.133010390758254, 0.36503621278522713, -0.25604945087969117, 1.0337343481755354, 0.04346786772870334, 0.4202899998207231, 0.4168687247935738, 0.40712239685437834, 0.9229772606818283]

# 10 mps to 24 caius
# CENTER_SEED = [1.247299324169421, -0.41106015166953314, 0.8416018028559116, 1.3171708411482275, 0.0654110374097755, 1.2944738267835878, -0.1364938967571458, -1.2592805648419447, 0.15445938466113973]
# CENTER_SEED = [1.6941328048354511, -0.7002415008019047, 0.8670820537539988, 1.3365265271341085, -0.34192597608770103, 0.4284718729200812, -0.34951251376205517, -0.521851479625922, 0.5340911713979972]
# CENTER_SEED = [1.531180561386735, -0.2624482938645385, 1.1860915128012577, 0.8882232942208413, -0.4681312289209082, 0.28300458807542195, -0.3540243402821355, -0.3413237541363725, 0.3477510695813424]
# CENTER_SEED = [1.3573991301131867, -0.39360233196765715, 1.335867964151845, 0.8939318587816482, -0.4761622889521111, 0.2257769781954686, -0.3752069806391935, -0.21788295902222257, 0.2300603933470855]
#CENTER_SEED = [1.2066043443715624, -0.4457719255623999, 1.4127501140656287, 0.9653829225454439, -0.5422629682928157, 0.027058400949436345, -0.5335937157536679, -0.24092572363622772, 0.2507752899857299]

# 10 to 24 cnuver
#CENTER_SEED = [1.0214736996249683, -0.5459552718800976, 1.478900502897858, 0.9578318315833567, -0.6468953329113127, 0.04126009364826224, -0.48454156591673514, -0.25932720589958796, 0.387978859690953]
#CENTER_SEED = [0.8608663338191436, -0.58760543107793, 1.3260405068980157, 0.8767676176214373, -0.7187530290595887, 0.12533337073942397, -0.556882328835109, -0.10996781398546665, 0.234592333829012]

CENTER_SEED_MARGIN = args.margin

G_DC = 1.0
G_NYQ = 0.0

GAIN_MIN = args.bounds_min
GAIN_MAX = args.bounds_max

LOGFILE = args.log

bounds = [
    (max(GAIN_MIN, c - CENTER_SEED_MARGIN),
     min(GAIN_MAX, c + CENTER_SEED_MARGIN))
    for c in CENTER_SEED
]

# ============================================================
#  FIR builder
# ============================================================

def build_lowpass_firwin2(params, K, g_dc=G_DC, g_nyq=G_NYQ):
    params = np.asarray(params, dtype=np.float64)
    g_interior = params[:K]

    #freq = np.concatenate(([0.0], [0.25, 0.375, 0.5, 0.625, 0.75], [1.0]))
    freq = np.concatenate(([0.0], [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9], [1.0]))
    g = np.concatenate(([g_dc], g_interior, [g_nyq]))

    h = firwin2(NUMTAPS, freq, g).astype(np.float32)
    h /= np.sum(h)

    return h, freq, g

# ============================================================
#  Low-pass sanity check
# ============================================================

def is_lowpass(h, min_dc=0.1, max_hf_ratio=0.7):
    h = np.asarray(h, dtype=np.float32)
    N = len(h)

    H0 = float(np.sum(h))
    if H0 <= min_dc:
        return False

    signs = np.power(-1.0, np.arange(N, dtype=np.float32))
    Hpi = float(np.sum(h * signs))

    if abs(Hpi) >= max_hf_ratio * H0:
        return False

    return True

# ============================================================
#  Output parsing
# ============================================================

def count_ext_squitter(out):
    return sum(
        line.startswith(b"@") and len(line) > 13 and line[13] == ord("8")
        for line in out.splitlines()
    )

# ============================================================
#  Best-so-far tracking
# ============================================================

best_score = -np.inf
best_total = -np.inf
best_params = None
best_taps = None

# ============================================================
#  DE objective
# ============================================================
DEFAULT_RATE_PAIRS = [
    (2.4, 8.0),
    (6.0, 6.0),
    (10.0, 10.0)
]

def default_output_rate(input_mhz: float) -> float:
    for (inp, out) in DEFAULT_RATE_PAIRS:
        if inp == input_mhz:
            return out
    raise ValueError(f"No default output rate for input rate {input_mhz}")


def evaluate_filter(params):
    global best_score, best_params, best_taps, best_total, bounds

    h, freq, gain = build_lowpass_firwin2(params, K)

    if not is_lowpass(h):
        return 1e9

    with open(FILTER_PATH, "w") as f:
        for t in h:
            f.write(f"{t}\n")

    input_mhz = FS / 1_000_000.0

    if FS_UP is not None:
        output_mhz = FS_UP / 1_000_000.0
    else:
        output_mhz = default_output_rate(input_mhz)

    exe = "../build/stream1090"

    cmd = [
        "bash", "-c",
        f"cat {DATA_PATH} | {exe} "
        f"-s {input_mhz} "
        f"-u {output_mhz} "
        f"-f {FILTER_PATH}"
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    out, _ = proc.communicate()

    total = sum(line.startswith(b"@") for line in out.splitlines())
    df17 = count_ext_squitter(out)

    #score = df17 + total
    score = total
    print(score)

    # ========================================================
    #  Best-so-far logging
    # ========================================================
    if score > best_score:
        best_score = score
        best_total = total
        best_params = params.copy()
        best_taps = h.copy()

        from datetime import datetime
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        with open(LOGFILE, "a") as f:
            f.write("# ================================================================\n")
            f.write(f"# Instance: {DATA_PATH}\n")
            f.write(f"# Time: {timestamp}\n")
            f.write(f"# FS: {FS/1_000_000} MHz → FS_UP: {FS_UP/1_000_000} MHz\n")
            f.write(f"# Number of taps: {NUMTAPS}\n")
            f.write(f"# Best score: {best_score}\n")
            f.write(f"# Best total: {best_total}\n")
            f.write(f"# Best params: {best_params.tolist()}\n")
            f.write("# Current bounds:\n")
            for lo, hi in bounds:
                f.write(f"# [{lo:.6f}, {hi:.6f}]\n")
            f.write("# Best taps:\n")
            for t in best_taps:
                f.write(f"{t}\n")
            f.write("\n")

    print(f"Eval params={np.round(params, 4)} → messages={total}")
    print(f"Eval bounds={np.round(bounds, 4)}")
    print(f"| Best so far: {best_score} @ {np.round(best_params, 4) if best_params is not None else None}")

    return -score

# ============================================================
#  DE loop
# ============================================================

center = np.array(CENTER_SEED, dtype=np.float64)

while True:
    print("Starting Differential Evolution...")

    result = differential_evolution(
        evaluate_filter,
        bounds,
        maxiter=args.maxiter,
        popsize=args.popsize,
        mutation=(0.5, 1.0),
        recombination=0.7,
        polish=False,
        workers=1,
        x0=center,
    )

    print("\n================ END OF RUN ====================")
    print(f"Best params: {np.round(best_params, 6)}")
    print(f"Best ext-squitter count: {best_score}")
    print(f"Best message count: {best_total}")
    print("Best taps:")
    print(np.round(best_taps, 8))
    print("===============================================\n")

    from datetime import datetime
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    with open(LOGFILE, "a") as f:
        f.write("# ==================== END OF RUN ====================\n")
        f.write(f"# Time: {timestamp}\n")
        f.write(f"# Instance: {DATA_PATH}\n")
        f.write(f"# FS: {FS/1_000_000} MHz → FS_UP: {FS_UP/1_000_000} MHz\n")
        f.write(f"# Number of taps: {NUMTAPS}\n")
        f.write(f"# Best params: {best_params.tolist()}\n")
        f.write(f"# Best ext-squitter count: {best_score}\n")
        f.write(f"# Best message count: {best_total}\n")
        f.write("# Current bounds:\n")
        for lo, hi in bounds:
            f.write(f"# [{lo:.6f}, {hi:.6f}]\n")
        f.write("# Best taps:\n")
        for t in best_taps:
            f.write(f"{t}\n")
        f.write("\n")

    energies = result.population_energies
    pop = result.population
    idx_sorted = np.argsort(energies)

    best = pop[idx_sorted[0]]
    second = pop[idx_sorted[1]]

    alpha = args.alpha
    margins = alpha * np.abs(best - second)
    margins = np.clip(margins, 0.05, 0.5)

    bounds = []
    for i in range(K):
        low = max(GAIN_MIN, best[i] - margins[i])
        high = min(GAIN_MAX, best[i] + margins[i])
        bounds.append((low, high))

    print("# Per-parameter margins:", margins)

    center = best_params.copy()
