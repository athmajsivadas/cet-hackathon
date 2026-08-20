import os

p = r'software/bridge/bridge.py'
with open(p, 'r', encoding='utf-8') as f:
    code = f.read()

# Add regex pattern for SYNC
sync_regex = "'SYNC': re.compile(r'\\[NODE(\\d)\\] SYNC: (GREEN|YELLOW|RED)'),"
if "'SYNC':" not in code:
    code = code.replace("'NORMAL':", sync_regex + "\n    'NORMAL':")

# Add logic for SYNC in parse_and_push
sync_logic = '''
    # SYNC ─ real-time normal traffic cycle LED updates ────────────────
    m = PATTERNS['SYNC'].search(line)
    if m:
        node_id = int(m.group(1))
        color = m.group(2)
        # We don't spam the event log with these, just update the node status
        update_node(node_id, color)
        return
'''
if '# SYNC ─ real-time' not in code:
    code = code.replace("    # NORMAL", sync_logic + "\n    # NORMAL")

with open(p, 'w', encoding='utf-8') as f:
    f.write(code)
print('Updated bridge.py with SYNC logic')
