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
        addr_split = l.split('\t')
        if len(addr_split) < 2:
            continue

        code_split = addr_split[1].split('\t')
        if not addr_split[0].endswith(':'):
            continue

        address_text = addr_split[0].replace(':','')
        addr = int(address_text, 16)
        if first_addr is None:
            first_addr = addr

        if 'out of bounds' in code_split[0]:
            continue

        if addr - first_addr != len(data_at):
            extend_by = (addr - first_addr) - len(data_at)
            data_at.extend(b'\0' * extend_by)
        data = unhexlify(code_split[0].replace(' ',''))
        data_at.extend(data)

    i = 0
    data = data_at
    prev = None
    while i < len(data):
        taken_data = data[i:i+4]
        if taken_data[0] == 0 and taken_data[1] >= 4 and taken_data[1] < 40:
            ending_null_at = i + taken_data[1] + 2
            letters_between = data[i+2:ending_null_at]
            if len(letters_between) == 0:
                i += 4
                continue

            if not all_printable(letters_between):
                i += 4
                continue

            name = bytes(letters_between).decode('utf8')
            if prev is not None:
                result[prev] = name
            else:
                # Just list the address of the name block.
                result[first_addr + i] = name

            i = ending_null_at

            while i % 4 != 0:
                i += 1

            while i < len(data) and data[i] == 0:
                i += 4

            prev = i

        i += 4

    return result

if __name__ == '__main__':
    import sys
    addrs = find_names_in_asm(open(sys.argv[1]).readlines())
    for a in addrs.keys():
        print(hex(a),addrs[a])
