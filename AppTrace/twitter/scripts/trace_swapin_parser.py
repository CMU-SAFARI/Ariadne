
src_input = "./swapin_trace.txt"
meta_output = src_input + ".meta"

src_file = open(src_input, 'r', encoding='utf-8')
meta_file = open(meta_output, 'wb')

for line in src_file:
    line = line.strip()
    if line is None or line == "":
        continue
    sps = line.split(",")
    if len(sps) != 2:
        continue
    sps[0] = sps[0].split(" ")[-1]
    if sps[0] is None or sps[0] == "":
        continue
    if sps[1] is None or sps[1] == "":
        continue
    
    app_id = int(sps[0]).to_bytes(8, byteorder='little', signed=False)
    sec = int(sps[1]).to_bytes(8, byteorder='little', signed=False)
    meta_file.write(app_id)
    meta_file.write(sec)

src_file.close()
meta_file.close()

src_input = "./ttid_swapin_trace.txt"
meta_output = src_input + ".meta"

src_file = open(src_input, 'r', encoding='utf-8')
meta_file = open(meta_output, 'wb')

for line in src_file:
    line = line.strip()
    if line is None or line == "":
        continue
    sps = line.split(",")
    if len(sps) != 2:
        continue
    sps[0] = sps[0].split(" ")[-1]
    if sps[0] is None or sps[0] == "":
        continue
    if sps[1] is None or sps[1] == "":
        continue
    
    app_id = int(sps[0]).to_bytes(8, byteorder='little', signed=False)
    sec = int(sps[1]).to_bytes(8, byteorder='little', signed=False)
    meta_file.write(app_id)
    meta_file.write(sec)

src_file.close()
meta_file.close()
