from binascii import unhexlify

def all_printable(s):
    for letter in s:
        if letter <= 32 or letter > 0x7e:
            return False
    return True

def find_names_in_asm(lines):
    first_addr = None
    data_at = []
    result = {}
    for lns in lines:
        l = lns.strip()
        addr_split = l.split(' ')
        code_split = addr_split[1].split('\t')
        if len(addr_split) == 0 or (not addr_split[0].endswith(':')):
            continue

        addr = int(addr_split[0][:8], 16)
        if first_addr is None:
            first_addr = addr
        data = unhexlify(code_split[0])
        data_at.extend(data)

    i = 0
    data = data_at
    while i < len(data):
        taken_data = data[i:i+4]
        if taken_data[0] == 0 and taken_data[1] < 40:
            ending_null_at = i + 2 + taken_data[1]
            if ending_null_at >= len(data):
                i += 4
                continue

            if data[ending_null_at] != 0:
                i += 4
                continue

            letters_between = data[i+2:ending_null_at]
            if len(letters_between) == 0:
                i += 4
                continue

            if not all_printable(letters_between):
                i += 4
                continue

            name = bytes(letters_between).decode('utf8')

            # Search back for cror 31,31,31
            j = i
            while j >= 0:
                maybe_cror = bytes(data[j:j+4])
                if maybe_cror == b'\x4f\xff\xfb\x82':
                    result[first_addr + j] = name
                    break
                j -= 4

        i += 4

    return result

if __name__ == '__main__':
    import sys
    addrs = find_names_in_asm(open(sys.argv[1]).readlines())
    for a in addrs.keys():
        print(hex(a),addrs[a])
