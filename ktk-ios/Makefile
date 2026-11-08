# KTKLegacy: КТК для iOS 6. Сборка: make package (нужны $THEOS, тулчейн и iPhoneOS9.3.sdk, см. README.md).
# armv7 only: the bundled curl/OpenSSL are thin armv7, so no armv7s/arm64 slices.
TARGET := iphone:clang:9.3:6.0
ARCHS := armv7

include $(THEOS)/makefiles/common.mk

APPLICATION_NAME = KTKLegacy

KTKLegacy_FILES = $(wildcard Sources/*.m)
KTKLegacy_FRAMEWORKS = UIKit Foundation CoreGraphics QuartzCore QuickLook Security
KTKLegacy_CFLAGS = -fobjc-arc -IVendor/curl/include -Wall -Wno-unused-function
KTKLegacy_LDFLAGS = -LVendor/curl/lib -lcurl -lssl -lcrypto -lz
KTKLegacy_CODESIGN_FLAGS = -Sentitlements.plist

include $(THEOS_MAKE_PATH)/application.mk
