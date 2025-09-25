# SIPserverV5 (Original Version)

SIPserverV5 is a SIP server that provides directory services for Amateur Radio Emergency Data Network (AREDN) mesh networks. It automatically fetches a phonebook from common servers and maintains a copy on the router, making it easy for SIP phones to access directory listings across the mesh network.

## Features

- **Automatic Directory Updates**: Downloads phonebook from mesh servers every 30 minutes
- **Multi-threaded Architecture**: Background fetching doesn't affect SIP performance
- **Phone Integration**: Provides XML directory for SIP phones
- **Configurable Servers**: Support for multiple fallback phonebook sources
- **Hash-based Updates**: Only processes phonebook when content changes

## Installation

### Download

1. Go to the [Releases page](https://github.com/dhamstack/AREDN-Phonebook/releases/tag/1.0.8)
2. Download the latest `SIPserverV5-x.x.x-x_[architecture].ipk` file for your device:
   - **ath79**: Most common AREDN routers (MikroTik hap lite, small white)
   - **x86**: PC-based AREDN nodes (e.g. Proxmos)
   - **ipq40xx**: (Mikrotik hap3, bigger black)

### Install via AREDN Web Interface

1. **Access AREDN Node**: Connect to your AREDN node's web interface

2. **Navigate to Administration**: Go to **Administration** → **Package Management**

   ![Package Management Screen](images/package-management.png)

3. **Upload Package**:
   - Click **Choose File** and select your downloaded `.ipk` file

     ![Upload Package Dialog](images/upload-package.png)

4. **Install**: Click **Fetch and Install**

## Configuration (optional, not needed for most users)

The phonebook server automatically configures itself. Default settings:

- **Configuration**: `/etc/sipserver.conf`
- **Service Commands**: `/etc/init.d/sip-proxy start|stop|restart|status`
- **SIP Port**: 5060
- **Directory URL**: `http://[your-node].local.mesh/arednstack/phonebook_generic_direct.xml`

## Phone Setup

Configure your SIP phone to use the node's directory:

1. **Directory URL**: `http://localnode.local.mesh/arednstack/phonebook_generic_direct.xml`
2. **SIP Server**: `localnode.local.mesh`
3. **Refresh**: Directory updates automatically every xx seconds from router (your Update Time Interval)

## Troubleshooting

### Check Service Status
```bash
ps | grep sip-proxy
logread | grep "sip-server"
```

### Verify Directory Files
```bash
ls -la /www/arednstack/phonebook*
curl http://localhost/arednstack/phonebook_generic_direct.xml
```

### Common Issues

- **No directory showing**: Wait up to 30 minutes for first download
- **Service not starting**: Check logs with `logread | tail -50`
- **Permission errors**: Ensure `/www/arednstack/` directory exists

## Technical Details

- **Multi-threaded**: Separate threads for SIP handling, phonebook fetching, and status updates
- **Hash-based Updates**: Only downloads when phonebook content actually changes
- **CSV Processing**: Converts downloaded CSV to XML format for phone compatibility
- **Fallback Servers**: Tries multiple configured servers if primary fails

## Support

- **Issues**: [GitHub Issues](https://github.com/dhamstack/AREDN-Phonebook/issues)
- **Documentation**: [Functional Specification](AREDN-phonebook-fsd.md)
- **AREDN Community**: [AREDN Forums](https://www.arednmesh.org/)

## License

This project is released under open source license for amateur radio emergency communications.
