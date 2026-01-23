import math

def iq_to_float(v):
    return (v - 127.5) / 127.5

print("inline constexpr std::array<float, 65536> iq_sqrt_table = {")
count = 0

for i in range(256):
    f_i = iq_to_float(i)
    for q in range(256):
        f_q = iq_to_float(q)
        mag = math.sqrt(f_i*f_i + f_q*f_q)

        # print with 8 decimals, C++ float literal
        print(f"    {mag:.8f}f,", end="")

        count += 1
        if count % 8 == 0:
            print()  # newline every 8 values

print("};")

