#!/usr/bin/env python3
"""
优化版 patterns.h 生成脚本
- 预计算每个pattern需要的tap位置
- 支持多字节并行处理
"""

import sys

def parse_pattern_file(filename):
    patterns = []
    with open(filename, 'r', encoding='utf-8', errors='ignore') as f:
        for line_num, line in enumerate(f, 1):
            line = line. strip()
            if not line or line. startswith('#'):
                continue
            pattern_bytes = parse_pattern_string(line)
            if pattern_bytes:
                patterns.append({'id': line_num, 'original': line, 'bytes':  pattern_bytes})
    return patterns

def parse_pattern_string(s):
    result = []
    i = 0
    while i < len(s):
        if s[i] == '|': 
            end = s. find('|', i + 1)
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

def generate_patterns_h(patterns, output_file, max_len=16, dcam_p=4):
    used_bytes = collect_used_bytes(patterns)
    byte_to_index = {b: i for i, b in enumerate(used_bytes)}
    
    # 计算需要的history长度
    max_pattern_len = max(len(p['bytes']) for p in patterns)
    history_len = max_pattern_len + dcam_p - 1
    # 向上取整到2的幂次
    history_bits = 1
    while history_bits < history_len: 
        history_bits *= 2
    
    with open(output_file, 'w') as f:
        f.write("#ifndef PATTERNS_H\n")
        f.write("#define PATTERNS_H\n\n")
        f.write("#include <ap_int.h>\n\n")
        
        f.write(f"#define PATTERN_MAX_LEN {max_len}\n")
        f.write(f"#define NUM_PATTERNS {len(patterns)}\n")
        f.write(f"#define MAX_ACTUAL_LEN {max_pattern_len}\n")
        f.write(f"#define HISTORY_BITS {history_bits}\n\n")
        
        # used_bytes 数组
        f.write(f"static const unsigned char used_bytes[{len(used_bytes)}] = {{\n")
        for i, b in enumerate(used_bytes):
            if 32 <= b < 127 and chr(b) not in "'\\\"": 
                f.write(f"    '{chr(b)}',  // {i}\n")
            else:
                f.write(f"    0x{b:02X}, // {i}\n")
        f.write(f"}};\n")
        f.write(f"#define NUM_USED_BYTES {len(used_bytes)}\n\n")
        
        # Rule 结构体
        f.write("struct Rule {\n")
        f.write("    int len;\n")
        f.write(f"    unsigned char byte_index[PATTERN_MAX_LEN];  // 索引到used_bytes\n")
        f.write("};\n\n")
        
        # 规则数组 - 只存储byte_index，减少存储
        f.write(f"static const Rule rules[NUM_PATTERNS] = {{\n")
        for p in patterns: 
            pb = p['bytes'][:max_len]
            length = len(pb)
            idx_padded = [byte_to_index[b] for b in pb] + [0] * (max_len - length)
            f.write(f"    {{{length}, {{{', '.join(str(i) for i in idx_padded)}}}}},\n")
        f.write("};\n\n")
        
        f. write("#endif // PATTERNS_H\n")
    
    print(f"Generated {output_file}:")
    print(f"  - {len(patterns)} patterns")
    print(f"  - {len(used_bytes)} unique bytes")
    print(f"  - Max pattern length: {max_pattern_len}")
    print(f"  - History bits: {history_bits}")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage:  python3 gen. py <pattern_file> <output_file>")
        sys.exit(1)
    patterns = parse_pattern_file(sys.argv[1])
    generate_patterns_h(patterns, sys.argv[2])