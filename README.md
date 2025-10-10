# 📞 AREDN Phonebook

> 🎯 **Emergency-Ready Directory & Monitoring for Amateur Radio Mesh Networks**

AREDN Phonebook provides SIP directory services and network monitoring for Amateur Radio Emergency Data Network (AREDN) mesh networks. During normal times, it automatically fetches a phonebook from common servers and maintains a copy on the router, making it easy for SIP phones to access directory listings across the mesh network. The router stores this phonebook so the latest copied version is always available.

## ✨ Features

- 🔄 **Automatic Directory Updates**: Downloads phonebook from mesh servers every 30 minutes
- 🛡️ **Emergency Resilience**: Survives power outages with persistent storage
- 💾 **Flash-Friendly**: Minimizes writes to preserve router memory
- 🔌 **Plug-and-Play**: Works immediately after installation
- 📱 **Phone Integration**: Provides XML directory for SIP phones (tested with Yealink)
- 🔧 **Passive Safety**: Self-healing with automatic error recovery
- 📊 **AREDNmon Dashboard**: Real-time web-based network monitoring with visual status display
- 🎯 **Dual-Mode Testing**: ICMP ping + SIP OPTIONS tests with RTT/jitter measurement
- 📈 **Performance Metrics**: Color-coded latency indicators and progress tracking

## 📦 Installation

### 🔗 Download

1. Go to the [📥 Releases page](https://github.com/dhamstack/AREDN-Phonebook/releases)
2. Download the latest `AREDN-Phonebook-x.x.x-x_[architecture].ipk` file for your device:
   - 🏠 **ath79**: Most common AREDN routers (e.g., Ubiquiti, MikroTik)
   - 💻 **x86**: PC-based AREDN nodes
   - 🔧 **ipq40xx**: Some newer routers

### 🌐 Install via AREDN Web Interface

1. 🌐 **Access AREDN Node**: Connect to your AREDN node's web interface

2. ⚙️ **Navigate to Administration**: Go to **Administration** → **Package Management**

   ![Package Management Screen](images/package-management.png)

3. 📤 **Upload Package**:
   - Click **Choose File** and select your downloaded `.ipk` file

     ![Upload Package Dialog](images/upload-package.png)

4. ⚡ **Install**: Click **Fetch and Install**

## ⚙️ Configuration (optional, not needed for most users)

The phonebook server automatically configures itself. Default settings:

- 📄 **Configuration**: `/etc/sipserver.conf`
- 🔧 **Service Commands**: `/etc/init.d/AREDN-Phonebook start|stop|restart|status`
- 🔌 **SIP Port**: 5060
- 🌐 **Directory URL**: `http://[your-node].local.mesh/arednstack/phonebook_generic_direct.xml`

### 🧪 Phone Monitoring Configuration

Edit `/etc/sipserver.conf` to customize phone testing:

```ini
# UAC Test Interval - how often to test all phones (seconds)
UAC_TEST_INTERVAL_SECONDS=60

# UAC Call Test - enable INVITE testing (0=OPTIONS only, 1=OPTIONS+INVITE)
# Default: 0 (non-intrusive OPTIONS ping only)
UAC_CALL_TEST_ENABLED=0

# Number of OPTIONS pings per phone (1-20)
UAC_OPTIONS_PING_COUNT=5

# Only test phones with this prefix
UAC_TEST_PREFIX=4415
```

**Monitoring Modes:**
- 📊 **OPTIONS Only** (default): Non-intrusive latency/jitter measurement
- 📞 **OPTIONS + INVITE**: Fallback to ring test if OPTIONS fails

## 📱 Phone Setup

Configure your SIP phone to use the node's directory:

1. 🔗 **Directory URL**: `http://localnode.local.mesh/arednstack/phonebook_generic_direct.xml`
2. 📡 **SIP Server**: `localnode.local.mesh`
3. 🔄 **Refresh**: Directory updates automatically every xx seconds from router (your Update Time Interval)

## 📊 AREDNmon - Network Monitoring Dashboard

AREDNmon provides real-time network monitoring with a web-based dashboard showing the status of all phones on your mesh network.

### 🌐 Access Dashboard
- **URL**: `http://[your-node].local.mesh/cgi-bin/arednmon`
- **Auto-refresh**: Updates every 30 seconds automatically

### ✨ Dashboard Features
- 📈 **Real-time Status Display**: See all phones with ONLINE/OFFLINE/NO_DNS status
- 📊 **Performance Metrics**: RTT (round-trip time) and jitter measurements
- 🎨 **Color-coded Results**: Green (<100ms), Orange (100-200ms), Red (>200ms)
- 📱 **Contact Names**: Automatically shows names from phonebook
- 📉 **Progress Tracking**: Visual progress bar showing test completion
- 🔄 **Smart Caching**: Phonebook data cached in browser for performance
- ⚡ **Dual Testing**: Both ICMP ping and SIP OPTIONS tests

### 📋 Dashboard Columns
| Column | Description |
|--------|-------------|
| **Phone Number** | SIP extension number |
| **Name** | Contact name from phonebook |
| **Ping Status** | ICMP network-layer connectivity |
| **Ping RTT** | Network round-trip time in ms |
| **Ping Jitter** | Network jitter in ms |
| **OPTIONS Status** | SIP application-layer connectivity |
| **OPTIONS RTT** | SIP round-trip time in ms |
| **OPTIONS Jitter** | SIP jitter in ms |

### ⚙️ Test Configuration
Tests run automatically based on `/etc/phonebook.conf` settings:
- **Test Interval**: Default 600 seconds (10 minutes)
- **Ping Count**: Default 5 ICMP pings per phone
- **OPTIONS Count**: Default 5 SIP OPTIONS per phone
- Only phones with DNS resolution are tested (marked with * in phonebook)

### 💡 Status Meanings
- 🟢 **ONLINE**: Phone responded successfully to test
- 🔴 **OFFLINE**: DNS resolved but phone didn't respond
- ⚪ **NO DNS**: Phone hostname doesn't resolve (node not on mesh)
- ⚫ **DISABLED**: Testing disabled in configuration

## 🔗 Webhook Endpoints

### 🔄 Load Phonebook (Manual Refresh)
- 🌐 **URL**: `http://[your-node].local.mesh/cgi-bin/loadphonebook`
- 📡 **Method**: GET
- ⚡ **Function**: Triggers immediate phonebook reload
- 📋 **Response**: JSON with status and timestamp
- 🎯 **Use Case**: Manual refresh, testing, emergency situations

### 📊 Show Phonebook (API Access)
- 🌐 **URL**: `http://[your-node].local.mesh/cgi-bin/showphonebook`
- 📡 **Method**: GET
- 📖 **Function**: Returns current phonebook contents as JSON
- 📋 **Response**: JSON with entry count, last updated time, and full contact list
- 🎯 **Use Case**: Integration with other tools, status checking

### 📡 UAC Ping Test (Phone Monitoring)
- 🌐 **URL**: `http://[your-node].local.mesh/cgi-bin/uac_ping?target=441530&count=5`
- 📡 **Method**: GET
- 🎯 **Parameters**:
  - `target`: Phone number to test (required)
  - `count`: Number of pings (1-20, default: 5)
- ⚡ **Function**: Sends SIP OPTIONS ping requests and measures RTT/jitter
- 📋 **Response**: JSON with test status
- 📈 **Metrics**: Min/max/avg RTT, jitter, packet loss percentage
- 🎯 **Use Case**: Diagnose phone connectivity, measure network quality
- 💡 **Note**: Non-intrusive test (doesn't ring the phone)

## 🔧 Troubleshooting

### ✅ Check Service Status
```bash
ps | grep AREDN-Phonebook
logread | grep "AREDN-Phonebook"
```

### 📂 Verify Directory Files
```bash
ls -la /www/arednstack/phonebook*
curl http://localhost/arednstack/phonebook_generic_direct.xml
```

### ⚠️ Common Issues

- 📅 **No directory showing**: Wait up to 30 minutes for first download
- 🚫 **Service not starting**: Check logs with `logread | tail -50`
- 🔒 **Permission errors**: Ensure `/www/arednstack/` directory exists

## 🔬 Technical Details

- 🚀 **Emergency Boot**: Loads the existing phonebook immediately on startup
- 💾 **Persistent Storage**: Survives power cycles using `/www/arednstack/`
- 🛡️ **Flash Protection**: Only writes when phonebook content changes
- 🧵 **Multi-threaded**: Background fetching doesn't affect SIP performance
- 🔧 **Auto-healing**: Recovers from network failures and corrupt data
- 📊 **RFC3550 Metrics**: Industry-standard jitter calculation for voice quality
- 🎯 **Smart Testing**: DNS pre-check reduces unnecessary SIP traffic
- ⚡ **Fast Detection**: 50ms polling for sub-second phone status updates

## 🆘 Support

- 🐛 **Issues**: [GitHub Issues](https://github.com/dhamstack/AREDN-Phonebook/issues)
- 📚 **Documentation**: [Functional Specification](AREDN-phonebook-fsd.md)
- 🌐 **AREDN Community**: [AREDN Forums](https://www.arednmesh.org/)

## 📄 License

This project is released under open source license for amateur radio emergency communications.
