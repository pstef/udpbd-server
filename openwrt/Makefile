include $(TOPDIR)/rules.mk

PKG_NAME:=udpbd-server
PKG_VERSION:=0.0.1
PKG_REV:=0.0.1
PKG_RELEASE:=1
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-$(PKG_REV).tar.gz
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/pstef/udpbd-server
PKG_SOURCE_VERSION:=$(PKG_REV)

include $(INCLUDE_DIR)/package.mk

define Package/udpbd-server
	SECTION:=net
	CATEGORY:=Network
	TITLE:=UDPBD server
	URL:=https://github.com/pstef/udpbd-server
	DEPENDS:=+kmod-usb-storage
endef

define Package/udpbd-server/description
	UDPBD server for use with PlayStation 2
endef

define Package/udpbd-server/install
	$(CP) ./files/* $(1)/

	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/build/udpbd-server $(1)/usr/bin/
endef

$(eval $(call BuildPackage,udpbd-server))
