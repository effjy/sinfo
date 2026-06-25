# sinfo — Services Information v1.0.1
# Build / install with gtkmm-4.0

CXX      ?= g++
PKGS      = gtkmm-4.0
CXXFLAGS ?= -O2 -std=c++17 -Wall
CXXFLAGS += $(shell pkg-config --cflags $(PKGS))
LIBS      = $(shell pkg-config --libs $(PKGS))

PREFIX  ?= /usr
BINDIR   = $(PREFIX)/bin
DATADIR  = $(PREFIX)/share
APPDIR   = $(DATADIR)/applications
ICONDIR  = $(DATADIR)/icons/hicolor/scalable/apps

TARGET   = sinfo
SRC      = main.cpp
DESKTOP  = org.effjy.sinfo.desktop
ICON     = sinfo.svg

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LIBS)

install: $(TARGET)
	install -Dm755 $(TARGET)  $(DESTDIR)$(BINDIR)/$(TARGET)
	install -Dm644 $(DESKTOP) $(DESTDIR)$(APPDIR)/$(DESKTOP)
	install -Dm644 $(ICON)    $(DESTDIR)$(ICONDIR)/$(ICON)
	-gtk-update-icon-cache -f -t $(DESTDIR)$(DATADIR)/icons/hicolor 2>/dev/null || true
	-update-desktop-database $(DESTDIR)$(APPDIR) 2>/dev/null || true
	@echo "Installed sinfo to $(DESTDIR)$(BINDIR)/$(TARGET)"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(APPDIR)/$(DESKTOP)
	rm -f $(DESTDIR)$(ICONDIR)/$(ICON)
	-gtk-update-icon-cache -f -t $(DESTDIR)$(DATADIR)/icons/hicolor 2>/dev/null || true
	-update-desktop-database $(DESTDIR)$(APPDIR) 2>/dev/null || true
	@echo "Uninstalled sinfo"

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
