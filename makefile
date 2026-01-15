CC = gcc
TARGET = parrot
INSTALL_DIR = /usr/local/bin
INSTALL_PATH = $(INSTALL_DIR)/$(TARGET)

# Compiler flags
CFLAGS = -std=c99 -Wall -Wextra -O2
LDFLAGS = -lncurses

# Source files
SRC = terminal.c \
      main.c

# Object files
OBJ = $(SRC:.c=.o)

# Default target
all: build

# Build target
build: $(OBJ)
	$(CC) $(CFLAGS) $^ -o $(TARGET) $(LDFLAGS)

# Compile .c files to .o files
%.o: %.c terminal.h
	$(CC) $(CFLAGS) -c $< -o $@

# Install target
install: build
	@echo "Installing $(TARGET) to $(INSTALL_DIR)..."
	@if [ -w $(INSTALL_DIR) ]; then \
		cp $(TARGET) $(INSTALL_PATH); \
		chmod 755 $(INSTALL_PATH); \
		echo "Installation complete"; \
	else \
		echo "Root permissions are required to install to $(INSTALL_DIR)"; \
		sudo cp $(TARGET) $(INSTALL_PATH); \
		sudo chmod 755 $(INSTALL_PATH); \
	fi

# Uninstall target
uninstall:
	@echo "Removing $(TARGET) from $(INSTALL_DIR)..."
	@if [ -w $(INSTALL_DIR) ] && [ -f $(INSTALL_PATH) ]; then \
		rm $(INSTALL_PATH); \
		echo "Uninstallation complete"; \
	else \
		echo "Root permissions are required to uninstall from $(INSTALL_DIR)"; \
		sudo rm -f $(INSTALL_PATH) 2>/dev/null || true; \
	fi

# Clean target
clean:
	rm -f $(TARGET) $(OBJ)

# Rebuild target
rebuild: clean build

# Distclean target
distclean: clean
	rm -f *~ .*~ *.bak

# Phony targets
.PHONY: all build install uninstall clean rebuild distclean
