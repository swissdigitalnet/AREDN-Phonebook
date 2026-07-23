# Problem After upgrade to 4.26.7.0

LAN devices (VoIP phones, IoT) lose mesh access after upgrading a node to AREDN
4.26.7.0.

**Applies to:** any node upgraded to AREDN **4.26.7.0** (OpenWrt 25.12 / apk) that
has VoIP phones or other devices on its LAN.

This is an **AREDN node/DHCP configuration issue, not a phonebook bug.** It is
documented here because it presents as "the phonebook / VoIP stopped working" and
operators of this package will hit it during the 4.x rollout.

---

## Symptom

After upgrading a node to 4.26.7.0, devices on that node's LAN can only reach hosts
**on their own LAN subnet**:

- SIP phones **register and ring**, and **same-node calls work**, but
  **cross-node calls are one-way** — you hear the far end, they don't hear you.
- Any LAN device (phone, IoT sensor, printer, PC without RFC 3442 support) can't
  reach other mesh hosts, even though the node itself can.

## Cause

The 4.x upgrade silently resets the node setting **`lan_dhcp_route` to `0`**.

`/usr/local/bin/node-setup` builds the LAN DHCP options in two branches keyed on
that flag (the code is **identical** between 4.26.1.0 and 4.26.7.0):

| `lan_dhcp_route` | DHCP behaviour | Result |
|---|---|---|
| **`1`** (old nodes, works) | omits an explicit option 3 → **dnsmasq auto-sends option 3 = node IP** as the default gateway | every LAN device reaches the mesh |
| **`0`** (upgraded nodes, broken) | ships a **blank option 3** and the mesh route **only** via option 121/249 (RFC 3442 classless static routes) | devices that ignore option 121 (Yealink phones, many IoT devices) get **no gateway** and are stranded on their /29 |

Because the phones don't implement option 121, they have no route off their own
subnet. Inbound packets arrive (the node forwards them onto the LAN), but the phone
can't send anything back off-subnet — hence one-way audio.

## Fix

SSH into the **upgraded** node (dropbear is on port 2222):

```sh
ssh -p 2222 root@localnode.local.mesh      # or root@<nodename>.local.mesh
```

**1. Check the flag** (`0` = broken, `1` = OK):

```sh
uci -q get aredn.@wan[0].lan_dhcp_route
```

**2. If `0`, apply the fix** (paste the whole block — node-agnostic, uses this
node's own LAN IP):

```sh
LAN=$(uci -q get network.lan.ipaddr)

# make the setting permanent (survives future config regeneration)
uci set aredn.@wan[0].lan_dhcp_route='1'
uci commit aredn

# apply the corrected DHCP options live: real default route via the node,
# no blank option 3
uci -q delete dhcp.@dhcp[0].dhcp_option
uci add_list dhcp.@dhcp[0].dhcp_option="121,10.0.0.0/8,${LAN},44.32.112.0/20,${LAN},0.0.0.0/0,${LAN}"
uci add_list dhcp.@dhcp[0].dhcp_option="249,10.0.0.0/8,${LAN},44.32.112.0/20,${LAN},0.0.0.0/0,${LAN}"
uci commit dhcp

# restart DHCP only — mesh/babel stay up, no node reboot needed
/etc/init.d/dnsmasq restart
```

**3. Confirm the node config:**

```sh
uci show dhcp | grep dhcp_option
```

The 121 and 249 entries should end in `,0.0.0.0/0,<LAN_IP>`, and there should be
**no** standalone `'3'` entry.

**4. Reboot the LAN devices.** DHCP leases don't update until renewed — reboot every
phone / LAN device so it gets a lease that now includes the default gateway.

## Verify

- On a phone: **Status → Network** should show **Default Gateway = the node's LAN IP**
  (e.g. `10.x.x.x`), not blank / `0.0.0.0`.
- A **cross-node call** should have **two-way audio**.
- (Optional, from the node) a cross-node ping to the phone from an off-subnet source
  succeeds where it failed before:
  ```sh
  ping -c3 -I <node_dtdlink_ip> <phone_lan_ip>
  ```

## Notes

- **Re-apply after every upgrade to a 4.x release** — the upgrade does not preserve
  `lan_dhcp_route`.
- Nodes still on 4.26.1.0 or earlier already have `lan_dhcp_route=1` and are not
  affected.
- The setting is **not exposed in the 4.26.7.0 web UI** — SSH is required.
- Root-caused 2026-07-23 by packet-level analysis on the HB9BLA mesh
  (upgraded node `vm-1` vs old node `hap-2`); fix verified with two-way RTP.
