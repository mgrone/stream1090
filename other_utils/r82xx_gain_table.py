#!/usr/bin/env python3
# Generate the R82xx gain → (LNA, MIX, VGA_upstream, VGA_blog) mapping.

LNA_STEPS = [
    0, 9, 13, 40, 38, 13, 31, 22, 26, 31, 26, 14, 19, 5, 35, 13
]

MIX_STEPS = [
    0, 5, 10, 10, 19, 9, 10, 25, 17, 10, 8, 16, 13, 6, 3, -8
]

VGA_BLOG_INDEX = 11        # RTL-SDR Blog fork
VGA_UPSTREAM_INDEX = 8     # Upstream librtlsdr

def r82xx_gain_to_indices(requested_gain_tenth_db):
    total = 0
    lna = 0
    mix = 0

    for _ in range(15):
        if total >= requested_gain_tenth_db:
            break

        lna += 1
        total += LNA_STEPS[lna]

        if total >= requested_gain_tenth_db:
            break

        mix += 1
        total += MIX_STEPS[mix]

    return lna, mix

def build_gain_mapping():
    table = {}

    for req in range(0, 2000):
        lna, mix = r82xx_gain_to_indices(req)

        rf_gain = (
            sum(LNA_STEPS[1:lna+1]) +
            sum(MIX_STEPS[1:mix+1])
        )

        if rf_gain not in table:
            table[rf_gain] = (lna, mix, VGA_UPSTREAM_INDEX, VGA_BLOG_INDEX)

    return table

def main():
    table = build_gain_mapping()

    print("# Gain (dB) | LNA | MIX | VGA | VGA_blog")
    print("# --------------------------------------------")

    for gain, (lna, mix, vga_up, vga_blog) in sorted(table.items()):
        print(f"#{gain/10:10.1f} | {lna:3d} | {mix:3d} |"
              f" {vga_up:2d}  |  {vga_blog:2d}")

if __name__ == "__main__":
    main()
