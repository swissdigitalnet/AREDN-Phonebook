include $(TOPDIR)/rules.mk

PKG_NAME:=AREDN-Phonebook

# Auto-detect version from git tag or use override
ifeq ($(PKG_VERSION_OVERRIDE),)
  # Try to get version from git tag (strip leading 'v' if present)
  PKG_VERSION:=$(shell git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || echo "2.0.0")
else
  PKG_VERSION:=$(PKG_VERSION_OVERRIDE)
endif

PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/AREDN-Phonebook
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Telephony
  TITLE:=AREDN Phonebook
  URL:=https://github.com/dhamstack/AREDN-Phonebook
endef

define Package/AREDN-Phonebook/description
  AREDN Phonebook with SIP directory service and network monitoring.
  Includes UAC testing, AREDNmon dashboard, and passive safety features
  for Amateur Radio Emergency Data Networks.
endef

define Package/AREDN-Phonebook/conffiles
/etc/phonebook.conf
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
	mkdir -p $(PKG_BUILD_DIR)/files/etc
	$(CP) ./files/etc/phonebook.conf $(PKG_BUILD_DIR)/files/etc/
endef

define Build/Compile
	# Build main AREDN-Phonebook server with modular architecture
	# Softphone (SIP client library) + Phone Monitoring + Network Monitor
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-static \
		-I$(PKG_BUILD_DIR) \
		-o $(PKG_BUILD_DIR)/AREDN-Phonebook \
		$(PKG_BUILD_DIR)/main.c \
		$(PKG_BUILD_DIR)/call-sessions/call_sessions.c \
		$(PKG_BUILD_DIR)/user_manager/user_manager.c \
		$(PKG_BUILD_DIR)/phonebook_fetcher/phonebook_fetcher.c \
		$(PKG_BUILD_DIR)/sip_core/sip_core.c \
		$(PKG_BUILD_DIR)/status_updater/status_updater.c \
		$(PKG_BUILD_DIR)/file_utils/file_utils.c \
		$(PKG_BUILD_DIR)/csv_processor/csv_processor.c \
		$(PKG_BUILD_DIR)/log_manager/log_manager.c \
		$(PKG_BUILD_DIR)/config_loader/config_loader.c \
		$(PKG_BUILD_DIR)/passive_safety/passive_safety.c \
		$(PKG_BUILD_DIR)/softphone/softphone.c \
		$(PKG_BUILD_DIR)/softphone/softphone_sip_builder.c \
		$(PKG_BUILD_DIR)/softphone/softphone_sip_parser.c \
		$(PKG_BUILD_DIR)/phone_monitoring/phone_ping.c \
		$(PKG_BUILD_DIR)/network_monitor/topology_db.c \
		$(PKG_BUILD_DIR)/network_monitor/traceroute.c \
		$(PKG_BUILD_DIR)/network_monitor/http_client.c \
		$(PKG_BUILD_DIR)/network_monitor/topology_crawler.c \
		$(PKG_BUILD_DIR)/phone_testing/ping_bulk_test.c \
		$(PKG_BUILD_DIR)/phone_testing/ping_test.c \
		$(PKG_BUILD_DIR)/software_health/software_health.c \
		$(PKG_BUILD_DIR)/software_health/health_metrics.c \
		$(PKG_BUILD_DIR)/software_health/health_scorer.c \
		$(PKG_BUILD_DIR)/software_health/health_reporter.c \
		$(PKG_BUILD_DIR)/software_health/json_formatter.c \
		$(PKG_BUILD_DIR)/software_health/http_client.c \
		$(PKG_BUILD_DIR)/software_health/crash_handler.c \
		-lpthread -lm -lrt

	# Build phone ping database JSON reader CGI
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-static \
		-I$(PKG_BUILD_DIR) \
		-o $(PKG_BUILD_DIR)/phone_ping_reader \
		$(PKG_BUILD_DIR)/phone_monitoring/phone_ping_reader.c \
		-lrt
endef

define Package/AREDN-Phonebook/preinst
#!/bin/sh
# AREDN-Phonebook pre-installation script
# Cleans all phonebook data to ensure fresh installation (migration safe)

[ -n "$$$${IPKG_INSTROOT}" ] || {
	echo "AREDN-Phonebook: Cleaning phonebook data directory..."
	if [ -d "/www/arednstack" ]; then
		rm -rf /www/arednstack/*
		echo "AREDN-Phonebook: Cleaned all files in /www/arednstack/ (directory preserved)"
	else
		echo "AREDN-Phonebook: /www/arednstack/ does not exist (first install)"
	fi
}
exit 0
endef

define Package/AREDN-Phonebook/postinst
#!/bin/sh
# AREDN-Phonebook post-installation script
# Merges new configuration options into existing config file (preserves user settings)

[ -n "$$$${IPKG_INSTROOT}" ] || {
	CONFIG_FILE="/etc/phonebook.conf"

	echo "AREDN-Phonebook: Checking for configuration updates..."

	# Check if NETWORK_TRACEROUTE_ENABLED exists in config
	if ! grep -q "NETWORK_TRACEROUTE_ENABLED" "$$$$CONFIG_FILE" 2>/dev/null; then
		echo "AREDN-Phonebook: Adding topology mapping settings to config..."
		cat >> "$$$$CONFIG_FILE" <<'EOF'

# ============================================================================
# TOPOLOGY MAPPING SETTINGS
# ============================================================================

# Enable ICMP traceroute to discover network paths
# 0 = disabled, 1 = enabled
# Default: 1 (enabled)
NETWORK_TRACEROUTE_ENABLED=1

# Maximum hops for traceroute
# Range: 1-30, Default: 20
NETWORK_TRACEROUTE_MAX_HOPS=20

# Enable mesh network crawler to discover all nodes
# 0 = disabled, 1 = enabled
# Default: 1 (enabled)
NETWORK_CRAWLER_ENABLED=1

# Mesh crawler interval (in seconds)
# How often to crawl the mesh network topology
# Default: 600 (10 minutes)
NETWORK_CRAWLER_INTERVAL_SECONDS=600
EOF
		echo "AREDN-Phonebook: Configuration updated successfully"

		# Restart service to apply new settings
		if [ -x /etc/init.d/AREDN-Phonebook ]; then
			echo "AREDN-Phonebook: Restarting service to apply new configuration..."
			/etc/init.d/AREDN-Phonebook restart
		fi
	else
		echo "AREDN-Phonebook: Configuration already up to date"
	fi
}
exit 0
endef

define Package/AREDN-Phonebook/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/AREDN-Phonebook $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/files/etc/phonebook.conf $(1)/etc/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/AREDN-Phonebook $(1)/etc/init.d/
	$(INSTALL_DIR) $(1)/www/cgi-bin
	$(INSTALL_BIN) ./files/www/cgi-bin/loadphonebook $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/showphonebook $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/phone_test $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/arednmon $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/phone_ping $(1)/www/cgi-bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/phone_ping_reader $(1)/www/cgi-bin/phone_ping_json
	$(INSTALL_BIN) ./files/www/cgi-bin/health_status $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/active_calls_json $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/topology_json $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/traceroute_json $(1)/www/cgi-bin/
endef

$(eval $(call BuildPackage,AREDN-Phonebook))