CXX = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17
LDFLAGS =

TARGET = wine-launcher
SRCDIR = src
SOURCES = wine-launcher.cpp
OBJECTS = $(SOURCES:.cpp=.o)

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJECTS)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	@echo ""
	@echo "Installed $(TARGET) to $(DESTDIR)$(BINDIR)"
	@echo ""
	@echo "To make this the default 'wine' command, run:"
	@echo "  sudo ln -sf $(BINDIR)/$(TARGET) $(BINDIR)/wine"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
