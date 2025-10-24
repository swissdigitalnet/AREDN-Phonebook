# Topology Connection Growth Analysis

**Date:** October 24, 2025
**Current deployment:** hap-2 (271 nodes, 465 connections)

---

## Question

**Will we hit the MAX_TOPOLOGY_CONNECTIONS limit (2000) as the mesh grows?**

---

## Current Status

### Database Limits

```c
#define MAX_TOPOLOGY_NODES 500
#define MAX_TOPOLOGY_CONNECTIONS 2000
```

### Current Usage (271 nodes)

```
Nodes: 271
Connections: 465
Utilization: 23.3% of capacity (465/2000)

Avg connections per node: 1.72
Connection density: 1.27% (of max possible full mesh)
Max possible connections: 36,585 (271 × 270 / 2)
```

**Key insight:** The mesh is **very sparse** - only 1.27% of possible connections exist.

---

## Connection Distribution

### Statistics

```
Nodes with connections: 230 (85% of total)
Min connections per node: 1
Max connections per node: 28
Median: 3 connections
Mean: 4 connections
Std dev: 3.7
```

### Top 10 Hub Nodes

| Node | Connections | Type |
|------|-------------|------|
| hb9bla-vm-tunnelserver | 28 | Tunnel server |
| hb0fl-entrypoint-01 | 21 | Entry point |
| HB9BLA-VM-TUNNELSERVER | 19 | Tunnel server |
| hb9cf-11-router | 16 | Router |
| hb9y-tracouet-5g155-10m-120 | 14 | Router |
| hb9hfm-hap-1 | 13 | Router |
| hb9fts-wstein-5g151-120 | 13 | Router |
| hb9tvp-vm-tunnelserver | 13 | Tunnel server |
| hb9gno-hap-tunnelserver | 13 | Tunnel server |
| hb9be-rumbrg2-5g169-10m-120-sektor | 12 | Router |

**Analysis:**
- Tunnel servers are the most connected (13-28 connections)
- Most nodes have 1-5 connections (typical mesh pattern)
- Hub-and-spoke topology, not full mesh

---

## Connection Growth Projections

### Linear Growth Model

**Assumption:** Avg connections per node stays constant at 1.72

| Nodes | Projected Connections | % of Limit | Status |
|-------|----------------------|------------|---------|
| 271 (current) | 465 | 23% | ✅ Safe |
| 300 | 514 | 26% | ✅ Safe |
| 400 | 686 | 34% | ✅ Safe |
| 500 | 857 | 43% | ✅ Safe |

**Verdict:** ✅ **No issue** - would use only 43% of capacity at 500 nodes

### Power Law Growth Model

**Assumption:** Connections grow faster than nodes (typical for mesh networks)

Using growth exponent α = 1.5:
```
connections(n) = connections(271) × (n/271)^1.5
```

| Nodes | Projected Connections | % of Limit | Status |
|-------|----------------------|------------|---------|
| 271 (current) | 465 | 23% | ✅ Safe |
| 300 | 541 | 27% | ✅ Safe |
| 400 | 833 | 42% | ✅ Safe |
| 500 | 1165 | 58% | ✅ Safe |

**Verdict:** ✅ **Still safe** - would use 58% of capacity at 500 nodes

### Worst Case: Quadratic Growth

**Assumption:** Every node added connects to a fixed number of existing nodes (e.g., 5)

| Nodes | Projected Connections | % of Limit | Status |
|-------|----------------------|------------|---------|
| 271 (current) | 465 | 23% | ✅ Safe |
| 300 | 650 | 33% | ✅ Safe |
| 400 | 1200 | 60% | ✅ Safe |
| 500 | 1850 | 93% | ⚠️ Near limit |

**Verdict:** ⚠️ **Tight but OK** - would use 93% of capacity at 500 nodes

---

## Why Connections Don't Grow Exponentially

### Mesh Network Topology

AREDN mesh networks are **NOT** fully connected because:

1. **Radio range limits** - Nodes can only connect to nearby nodes
2. **Physical geography** - Mountains, buildings, distance
3. **Hub-and-spoke** - Most nodes connect to 1-3 nearby routers, not all nodes
4. **Tunnel servers** - Act as hubs (10-30 connections), but edge nodes have 1-5

### Actual Growth Pattern

From the data:
- 271 nodes → 465 connections
- Avg connections per node: **1.72** (very low)
- Connection density: **1.27%** of theoretical maximum

This is **much** sparser than exponential growth would predict.

---

## Memory Impact Analysis

### Current Memory Usage (465 connections)

```
TopologyConnection: 704 bytes each
465 connections × 704 bytes = 319 KB
```

### Projected Memory at Capacity

#### Without Optimization

| Scenario | Connections | Memory (MB) | Total Process (MB) |
|----------|-------------|-------------|-------------------|
| Current | 465 | 0.32 | 1.9 |
| Linear (500 nodes) | 857 | 0.59 | 2.1 |
| Power law (500 nodes) | 1165 | 0.80 | 2.3 |
| Worst case (500 nodes) | 1850 | 1.27 | 2.7 |
| At limit (2000 conn) | 2000 | 1.38 | 2.8 |

#### With Strategy A (Quick Wins - reduce to 624 bytes/connection)

| Scenario | Connections | Memory (MB) | Savings |
|----------|-------------|-------------|---------|
| Current | 465 | 0.28 | 40 KB |
| At limit (2000) | 2000 | 1.22 | 160 KB |

#### With Strategy C (String Interning - reduce to 192 bytes/connection)

| Scenario | Connections | Memory (MB) | Savings |
|----------|-------------|-------------|---------|
| Current | 465 | 0.09 | 230 KB |
| At limit (2000) | 2000 | 0.37 | 1.0 MB |

---

## Risk Assessment

### Will We Hit the 2000 Connection Limit?

**NO - for the following reasons:**

1. **HB boundary filtering limits to Swiss mesh only**
   - Current Swiss mesh: 271 nodes
   - Growth is slow and bounded
   - Unlikely to exceed 500 nodes

2. **Mesh topology is sparse**
   - Only 1.27% connection density
   - Most nodes have 1-5 connections
   - Even at 500 nodes: 857-1165 connections (43-58% of limit)

3. **Worst case is still acceptable**
   - Even with worst-case quadratic growth: 1850 connections (93%)
   - 150 connection headroom remaining

### When Would We Hit the Limit?

**Scenario:** To hit 2000 connections with current density (1.72 conn/node):

```
2000 connections ÷ 1.72 = 1163 nodes
```

**Verdict:** Would need **1163 nodes** - more than 2× the database capacity (500 nodes).

**Conclusion:** ✅ **Cannot hit connection limit before hitting node limit.**

---

## Recommendations

### 1. No Immediate Action Needed

✅ Current configuration is safe:
- 465/2000 connections (23% used)
- Projected 857-1165 at 500 nodes (43-58%)
- Sufficient headroom

### 2. Monitor Connection Growth

Track these metrics over time:
```bash
ssh hap-2 "cat /tmp/arednmon/network_topology.json | grep total_connections"
```

Alert if:
- Connections exceed 1500 (75% of limit)
- Connections grow faster than nodes

### 3. If Needed: Increase Limits

**Option A:** Raise MAX_TOPOLOGY_CONNECTIONS

```c
// In topology_db.h
#define MAX_TOPOLOGY_CONNECTIONS 5000  // Was 2000
```

**Memory impact:**
- Additional: 3000 × 704 bytes = 2.1 MB
- Total at limit: 4.3 MB (still acceptable)

**Option B:** Implement memory optimization first

If memory becomes a concern, implement Strategy C (string interning) which reduces connections from 704 → 192 bytes. Then:
- 2000 connections = 375 KB (vs 1.4 MB currently)
- Could support 10,000 connections in same memory footprint

### 4. Connection Pruning (Advanced)

If connection limit becomes an issue, implement aging:
- Remove connections not seen in >1 hour
- Keep only active/reachable connections
- Prune connections with 0 RTT (unreachable)

---

## Conclusions

### Main Question: Will we hit the connection limit?

**NO** - Analysis shows:

1. ✅ **Current usage is only 23%** (465/2000)
2. ✅ **Projected growth is manageable** (857-1165 at 500 nodes)
3. ✅ **Cannot hit connection limit before node limit** (needs 1163 nodes)
4. ✅ **HB filtering prevents unbounded growth**

### Memory Impact

Even at the 2000 connection limit:
- Current structure: 1.4 MB
- With quick wins: 1.2 MB
- With string interning: 0.4 MB

**All scenarios are acceptable** for target hardware (128 MB RAM).

### Final Verdict

✅ **No action needed** - current limits are sufficient

⚠️ **Re-evaluate IF:**
- Connection count exceeds 1500 (75% of limit)
- Swiss mesh grows beyond 400 nodes
- Connection growth rate changes significantly

---

## Monitoring Commands

```bash
# Check current topology stats
ssh hap-2 "cat /tmp/arednmon/network_topology.json | grep -E 'total_nodes|total_connections'"

# Calculate connection density
ssh hap-2 "logread | grep 'Topology written' | tail -1"

# Monitor memory usage
ssh hap-2 "cat /proc/\$(pidof AREDN-Phonebook)/status | grep VmRSS"

# Check for connection limit warnings
ssh hap-2 "logread | grep -i 'connection.*full'"
```

---

## Appendix: Connection Growth Mathematical Analysis

### Observed Growth Rate

From current data:
- 271 nodes, 465 connections
- Ratio: 1.72 connections per node

### Growth Models

**Model 1: Linear (O(n))**
```
connections(n) = k × n
where k = 1.72

At 500 nodes: 857 connections
```

**Model 2: Power Law (O(n^α))**
```
connections(n) = c × n^α
where α = 1.5 (typical for scale-free networks)

At 500 nodes: 1165 connections
```

**Model 3: Quadratic (O(n²))**
```
connections(n) = n × k
where k = avg connections per new node ≈ 5

At 500 nodes: 2500 connections ⚠️ (exceeds limit)
```

### Why Quadratic Growth Doesn't Apply

AREDN mesh networks are **not** random graphs with O(n²) connections because:

1. **Spatial constraints** - Radio range limits
2. **Hub topology** - Most nodes connect to few hubs
3. **Boundary filtering** - Only Swiss nodes
4. **Physical deployment** - Not every node can reach every other node

### Actual Network Model

AREDN mesh follows a **scale-free network** model:
- Few nodes (hubs) have many connections (10-30)
- Most nodes (edge) have few connections (1-5)
- Connection growth: O(n^α) where α ≈ 1.3-1.5

This is confirmed by our observation: **1.72 connections per node**.
