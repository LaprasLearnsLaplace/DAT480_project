#!/usr/bin/env python3
"""
生成 patterns.h 的 Python 脚本
用法: python3 generate_patterns.py <pattern_file> <output_file>
python3 scripts/gen.py /home/m2_1/dat480_project_base/Project_resources/MINI_pattern_match_snort3_content.txt /home/m2_1/dat480_project_base/Project_kernels_HLS/src/patterns.h
"""

import sys

def parse_pattern_file(filename):
    patterns = []
    with open(filename, 'r', encoding='utf-8', errors='ignore') as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            pattern_bytes = parse_pattern_string(line)
            if pattern_bytes:
                patterns.append({'id': line_num, 'original': line, 'bytes': pattern_bytes})
    return patterns

def parse_pattern_string(s):
    result = []
    i = 0
    while i < len(s):
        if s[i] == '|':
            end = s.find('|', i + 1)
            if end == -1:
                break
            hex_str = s[i+1:end].replace(' ', '')
            for j in range(0, len(hex_str), 2):
                if j + 1 < len(hex_str):
                    result.append(int(hex_str[j:j+2], 16))
            i = end + 1
        else:
            result.append(ord(s[i]))
            i += 1
    return result

def collect_used_bytes(patterns):
    used = set()
    for p in patterns:
        for b in p['bytes']:
            used.add(b)
    return sorted(list(used))

def generate_patterns_h(patterns, output_file, max_len=16):
    used_bytes = collect_used_bytes(patterns)
    byte_to_index = {b: i for i, b in enumerate(used_bytes)}
    
    with open(output_file, 'w') as f:
        f.write(f"#ifndef PATTERNS_H\n#define PATTERNS_H\n\n#include <ap_int.h>\n\n")
        f.write(f"#define PATTERN_MAX_LEN {max_len}\n#define NUM_PATTERNS {len(patterns)}\n\n")
        
        f.write(f"static const unsigned char used_bytes[{len(used_bytes)}] = {{\n")
        for i, b in enumerate(used_bytes):
            if 32 <= b < 127 and chr(b) not in "'\\\"":
                f.write(f"    '{chr(b)}',  // {i}\n")
            else:
                f.write(f"    0x{b:02X}, // {i}\n")
        f.write(f"}};\n#define NUM_USED_BYTES {len(used_bytes)}\n\n")
        
        f.write("struct Rule {\n    int len;\n    unsigned char data[PATTERN_MAX_LEN];\n    int byte_index[PATTERN_MAX_LEN];\n};\n\n")
        
        f.write(f"static const Rule rules[NUM_PATTERNS] = {{\n")
        for p in patterns:
            pb = p['bytes'][:max_len]
            length = len(pb)
            data_padded = pb + [0] * (max_len - length)
            idx_padded = [byte_to_index[b] for b in pb] + [0] * (max_len - length)
            f.write(f"    {{{length}, {{{', '.join(f'0x{b:02X}' for b in data_padded)}}}, {{{', '.join(str(i) for i in idx_padded)}}}}},\n")
        f.write("};\n\n#endif\n")
    
    print(f"Generated {output_file}: {len(patterns)} patterns, {len(used_bytes)} unique bytes")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python3 generate_patterns.py <pattern_file> <output_file>")
        sys.exit(1)
    patterns = parse_pattern_file(sys.argv[1])
    generate_patterns_h(patterns, sys.argv[2])