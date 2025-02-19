import re

def remove_non_printable(sstr):
    return ''.join(x for x in sstr if x.isprintable())

SWAP_TRACE_DATA_OFS = 50
PAGE_SIZE = 4096

src_input = "./swapout_trace.txt"
meta_output = src_input + ".meta"
data_output = src_input + ".data"

src_file = open(src_input, 'rb')
meta_file = open(meta_output, 'w+', encoding='utf-8')
data_file = open(data_output, 'wb+')

pattern  = r"TEST^^^#(.*?)#^^^"
while True:
    page_info = src_file.read(SWAP_TRACE_DATA_OFS)
    if not page_info:
        print("Finished!")
        break
    meta = page_info.decode('utf-8').strip()
    clean_meta = remove_non_printable(meta).replace("TEST^^^#", "").replace("#^^^", "").replace("^", "")
    sps = clean_meta.split(",")
    page_data = src_file.read(PAGE_SIZE)
    if not page_data:
        print("====================== ERROR!!!!!!!!!!!")
        break
    if len(sps) == 3 and sps[0].isdigit() and sps[1].isdigit() and sps[2].isdigit() and sps[1] is not None:
        meta_file.write(clean_meta + "\n")
        data_file.write(page_data)

src_file.close()
