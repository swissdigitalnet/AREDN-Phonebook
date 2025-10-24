# Topology Database Memory Optimization Plan

## Problem Statement

The topology database currently uses significant memory for hostname storage:

**Current Memory Usage (measured):**
```
TopologyNode:       360 bytes each
TopologyConnection: 704 bytes each

Current load (186 nodes, 413 connections):
  - Nodes: 65 KB
  - Connections: 284 KB
  - Total: 349 KB

At capacity (500 nodes, 2000 connections):
  - Nodes: 175 KB
  - Connections: 1375 KB
  - Total: 1550 KB (1.5 MB)
```

**Memory Breakdown per Structure:**
```
TopologyNode (360 bytes):
  - name[256]:      256 bytes  ← 71% of node size
  - type[16]:        16 bytes  ← 4%
  - lat[32]:         32 bytes  ← 9%
  - lon[32]:         32 bytes  ← 9%
  - status[16]:      16 bytes  ← 4%
  - last_seen:        8 bytes  ← 2%

TopologyConnection (704 bytes):
  - from_name[256]: 256 bytes  ← 36% of connection size
  - to_name[256]:   256 bytes  ← 36%
  - samples[10]:    160 bytes  ← 23%
  - metadata:        32 bytes  ← 5%
```

**Key Insight:** Hostname strings consume **82% of total memory** (768 bytes per connection, 256 bytes per node).

---

## Optimization Options

### Option 1: String Interning (String Pool)

**Concept:** Store each unique hostname once in a shared pool, reference by 16-bit index.

**Memory Impact:**
- Node: 360 bytes → 20 bytes (94% reduction)
- Connection: 704 bytes → 192 bytes (73% reduction)
- String pool: ~10 KB for 500 unique hostnames
- **Total savings: 1156 KB (75% reduction)**

**Implementation Complexity:** HIGH
- New string pool data structure
- API changes (indirect access via index)
- Migration of all hostname references
- String deduplication logic

**Benefits:**
- ✅ Massive memory savings
- ✅ Automatic deduplication
- ✅ Faster comparisons (compare uint16_t instead of strcmp)
- ✅ Cache-friendly (smaller structures)

**Risks:**
- ❌ API breaking changes
- ❌ String pool management complexity
- ❌ Potential for index bugs
- ❌ Debugging harder (can't printf node.name directly)

**Effort:** 2-3 days

---

### Option 2: Fixed-Point Coordinates

**Concept:** Store lat/lon as int32_t (degrees × 10,000,000) instead of strings.

**Example:**
```
"47.47446" (32 bytes) → 474744600 (4 bytes)
Precision: 7 decimal places (~1cm accuracy)
```

**Memory Impact:**
- Saves 56 bytes per node (lat[32] + lon[32] → 8 bytes)
- **Total savings: 28 KB (2% reduction)**

**Implementation Complexity:** LOW
- Add conversion functions
- Update coordinate storage
- Modify JSON export

**Benefits:**
- ✅ Simple to implement
- ✅ Better precision than string conversion
- ✅ No API changes (conversion transparent)

**Risks:**
- ❌ Conversion overhead on read/write
- ❌ Limited savings compared to string interning

**Effort:** 2-4 hours

---

### Option 3: Packed Enums (Type/Status)

**Concept:** Replace 16-byte strings with uint8_t enums.

**Example:**
```c
"phone" (16 bytes) → 0 (1 byte)
"router" (16 bytes) → 1 (1 byte)
"ONLINE" (16 bytes) → 0 (1 byte)
```

**Memory Impact:**
- Saves 30 bytes per node (type[16] + status[16] → 2 bytes)
- **Total savings: 15 KB (1% reduction)**

**Implementation Complexity:** LOW
- Define enums
- Add conversion functions
- Update JSON export

**Benefits:**
- ✅ Very simple
- ✅ Type-safe
- ✅ No API changes (conversion transparent)

**Risks:**
- ❌ Minimal savings
- ❌ Less flexible (adding types requires code change)

**Effort:** 1-2 hours

---

### Option 4: Hash Table for Lookups (Performance)

**Concept:** Add hash table for O(1) hostname lookups instead of O(n) linear search.

**Current Lookup:**
```c
for (int i = 0; i < 500; i++) {
    if (strcmp(g_nodes[i].name, hostname) == 0) return &g_nodes[i];
}
// O(n) - 500 strcmp calls worst case
```

**Optimized Lookup:**
```c
uint16_t idx = hash_table_get(hostname);
return &g_nodes[idx];
// O(1) - single hash lookup
```

**Memory Impact:**
- Adds ~2 KB for hash table (500 entries × 4 bytes)
- **Total savings: -2 KB (slight increase)**

**Implementation Complexity:** MEDIUM
- Implement hash table
- Update add/remove functions
- Handle hash collisions

**Benefits:**
- ✅ Faster lookups (critical for crawler)
- ✅ Scales better with more nodes
- ✅ No API changes

**Risks:**
- ❌ Slight memory increase
- ❌ Hash collision handling
- ❌ Must maintain sync with array

**Effort:** 4-6 hours

---

### Option 5: Reduce RTT Sample Count

**Concept:** Store fewer RTT samples (5 instead of 10).

**Memory Impact:**
- Saves 80 bytes per connection (8 bytes × 5 samples)
- **Total savings: 160 KB (10% reduction)**

**Implementation Complexity:** TRIVIAL
- Change `#define MAX_RTT_SAMPLES 5`

**Benefits:**
- ✅ Zero effort
- ✅ Significant savings

**Risks:**
- ❌ Less accurate statistics
- ❌ Shorter history window
- ❌ May affect connection quality assessment

**Effort:** 5 minutes

---

## Comparison Matrix

| Option | Savings | Complexity | Effort | API Impact | Risk |
|--------|---------|------------|--------|------------|------|
| String Interning | 1156 KB (75%) | HIGH | 2-3 days | Breaking | HIGH |
| Fixed-Point Coords | 28 KB (2%) | LOW | 2-4 hours | None | LOW |
| Packed Enums | 15 KB (1%) | LOW | 1-2 hours | None | LOW |
| Hash Table | -2 KB (perf) | MEDIUM | 4-6 hours | None | MEDIUM |
| Reduce RTT Samples | 160 KB (10%) | TRIVIAL | 5 min | None | LOW |

---

## Recommendations

### Strategy A: Quick Wins Only (Conservative)

**Implement:**
1. Reduce RTT samples (10 → 5) - 160 KB saved
2. Packed enums - 15 KB saved
3. Fixed-point coords - 28 KB saved

**Total savings: 203 KB (13% reduction)**
**Effort: 4 hours**
**Risk: LOW**

**Result at capacity:** 1550 KB → 1347 KB

**Rationale:** Safe incremental improvements, no API changes, minimal risk.

---

### Strategy B: Moderate Optimization (Recommended)

**Implement:**
1. Strategy A (quick wins) - 203 KB
2. Hash table for lookups - Performance boost
3. Add instrumentation to measure actual memory pressure

**Total savings: 201 KB + performance improvement**
**Effort: 1 day**
**Risk: LOW**

**Rationale:** Balances memory savings with performance gains. Defer string interning until proven necessary.

---

### Strategy C: Aggressive Optimization (If Needed)

**Implement:**
1. String interning with index pool - 1156 KB saved
2. Hash table integrated with string pool
3. All quick wins from Strategy A

**Total savings: 1359 KB (88% reduction)**
**Effort: 1 week**
**Risk: HIGH**

**Result at capacity:** 1550 KB → 191 KB

**Rationale:** Only pursue if running on very constrained hardware (e.g., 16 MB RAM routers).

---

## Decision Criteria

### When to choose Strategy A (Quick Wins):
- ✅ Running on nodes with 64+ MB RAM
- ✅ Current load is <500 KB
- ✅ Want minimal code churn
- ✅ Prioritize stability

### When to choose Strategy B (Moderate):
- ✅ Running on nodes with 32-64 MB RAM
- ✅ Current load is 500 KB - 1 MB
- ✅ Want performance improvements
- ✅ Can tolerate 1 day of development

### When to choose Strategy C (Aggressive):
- ✅ Running on nodes with 16-32 MB RAM
- ✅ Current load exceeds 1 MB
- ✅ Expect to scale to 1000+ nodes
- ✅ Can tolerate API breaking changes

---

## Questions to Answer Before Implementation

### 1. Hardware Constraints
**Q:** What is the total RAM on target AREDN nodes?
- MikroTik hAP ac²: 128 MB RAM
- Ubiquiti NanoStation: 32-64 MB RAM
- Other devices: ?

**Q:** What is acceptable memory budget for phonebook?
- Current: 1.5 MB at capacity
- With other services running: ?

### 2. Scale Expectations
**Q:** What is realistic maximum mesh size?
- Current Swiss mesh: ~186 nodes
- Expected growth: ?
- Boundary filtering keeps it <500?

**Q:** Do we need to support international meshes?
- If yes, could exceed 1000+ nodes
- If no, 500 capacity is sufficient

### 3. Performance Requirements
**Q:** Is lookup performance a bottleneck?
- Crawler makes many `topology_db_find_node()` calls
- Linear search O(n) vs hash table O(1)
- Measureable impact?

### 4. Development Resources
**Q:** How much time can be allocated?
- Quick wins: 4 hours
- Moderate: 1 day
- Aggressive: 1 week

**Q:** Testing capacity?
- Need to test on actual hardware
- Need to validate with large meshes
- Can we afford regression risk?

---

## Recommended Next Steps

1. **Measure actual memory usage on vm-1**
   ```bash
   ssh vm-1 "cat /proc/$(pidof AREDN-Phonebook)/status | grep VmRSS"
   ssh vm-1 "free -m"
   ```

2. **Profile which operations are slowest**
   - Add timing logs to `topology_db_find_node()`
   - Measure crawler total time
   - Identify bottlenecks

3. **Decision:**
   - If memory < 2 MB total: **Strategy A** (quick wins)
   - If memory 2-5 MB: **Strategy B** (moderate)
   - If memory > 5 MB: **Strategy C** (aggressive)

4. **Prototype** selected strategy in a branch

5. **Benchmark** before/after on actual hardware

6. **Validate** with full crawl (500 nodes)

---

## Open Questions for Discussion

1. **Is 1.5 MB at capacity actually a problem?**
   - AREDN nodes typically have 32-128 MB RAM
   - Other services: SIP server, web server, OLSR daemon
   - What % can phonebook reasonably consume?

2. **Should we optimize for current load (186 nodes) or capacity (500 nodes)?**
   - Swiss mesh is stable at ~200 nodes
   - Boundary filtering prevents overflow
   - Over-engineering vs future-proofing?

3. **API stability vs. optimization?**
   - String interning breaks API
   - Worth the complexity?
   - Alternative: keep current API, optimize internals only?

4. **JSON file size vs. memory?**
   - Current JSON: ~350 KB uncompressed
   - gzip could reduce to ~70 KB
   - Does JSON size matter? (served over local mesh)

5. **Could we use an external database (SQLite)?**
   - Offload to disk instead of RAM
   - Trade memory for I/O
   - Overkill for 500 nodes?

---

## Recommendation Summary

**Start with Strategy A (Quick Wins)** to gain 13% memory reduction with minimal risk:
- Reduce RTT samples to 5
- Use packed enums for type/status
- Use fixed-point coordinates

**Re-evaluate after 3 months:**
- Measure actual memory pressure on deployed nodes
- Monitor mesh growth trends
- Decide if string interning is justified

**Only pursue Strategy C (String Interning) if:**
- Memory usage exceeds 2 MB in production
- Mesh is expected to grow beyond 500 nodes
- Hardware constraints require aggressive optimization
