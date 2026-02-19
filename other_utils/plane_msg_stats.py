import sys
import rs1090

valid_dfs = ['0', '4', '5', '11', '16', '17', '18', '19', '20', '21' ]
df_per_plane = {}

             
for line in sys.stdin:
    try:
        if (not line.startswith('@')):
            continue

        line = line[13:].strip('*;\n')
				
        res = rs1090.decode(line)
        if (res == None):
            binary_message = format(int(line, 16), f'0{len(line)*4}b')
            #print("ERROR " + binary_message[:5] + " " + line[2:8] + " " + binary_message[33:38])
            continue

        p = res['icao24']
        df = res['df']
        if not (p in df_per_plane):
            df_per_plane[p] = {}
            df_per_plane[p]

        if not (df in df_per_plane[p]):
            df_per_plane[p][df] = 0
        df_per_plane[p][df] += 1

    except:
        continue
        #print("error: " + line)

header="icao"
for df in valid_dfs:
    header = header + ", DF-" + df

print(header)

for p in df_per_plane:
    line = p
    for df in valid_dfs:
        cnt = 0
        if (df in df_per_plane[p]):
            cnt = df_per_plane[p][df]
        line = line + ", " + str(cnt)
    print(line)

