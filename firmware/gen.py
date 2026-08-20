import os
import re

base_dir = r'C:\Users\athma\Downloads\CET Hackathon\firmware'

with open(os.path.join(base_dir, 'intersection_node', 'intersection_node.ino'), 'r', encoding='utf-8') as f:
    node_code = f.read()

with open(os.path.join(base_dir, 'vehicle_unit', 'vehicle_unit.ino'), 'r', encoding='utf-8') as f:
    veh_code = f.read()

macs_str = '''const uint8_t ALLOWED_MACS[][6] = {
  {0x1C, 0xC3, 0xAB, 0xBB, 0xE2, 0x30},  // Vehicle A
  {0x28, 0x05, 0xA5, 0xE2, 0xBF, 0xDC},  // Vehicle B
  {0x6C, 0xC8, 0x40, 0x05, 0x5A, 0x50},  // Node 1
  {0x08, 0xD1, 0xF9, 0xE1, 0x2B, 0xFC},  // Node 2
  {0x00, 0x70, 0x07, 0x3A, 0x38, 0x80},  // Node 3
};'''

node_code = re.sub(r'const uint8_t ALLOWED_MACS\[\]\[6\].*?};', macs_str, node_code, flags=re.DOTALL)
node_code = re.sub(r'uint8_t BROADCAST_MAC\[6\] = \{.*?\};', 'uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};', node_code)

for i in range(1, 4):
    c = re.sub(r'#define NODE_ID\s+\d', f'#define NODE_ID  {i}', node_code)
    d = os.path.join(base_dir, f'node_{i}')
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, f'node_{i}.ino'), 'w', encoding='utf-8') as f:
        f.write(c)

v_ids = ['A', 'B']
for i, v in enumerate(v_ids):
    c = re.sub(r'#define VEHICLE_ID\s+\'.\'', f"#define VEHICLE_ID '{v}'", veh_code)
    d = os.path.join(base_dir, f'vehicle_{i+1}')
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, f'vehicle_{i+1}.ino'), 'w', encoding='utf-8') as f:
        f.write(c)

print('Generated 5 ready-to-burn folders!')
