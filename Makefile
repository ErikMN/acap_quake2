PROG := acap_quake2
ARCH := aarch64

CONTAINER_RUNTIME ?= docker
DOCKER_TAG := acap_quake2_$(ARCH)
CONTAINER_TTY_ARG := $(shell if test -t 0; then printf '%s' -t; fi)

# Absolute path to the project root on the host:
ROOT := $(CURDIR)

# Common options used by all SDK container commands:
CONTAINER_ARGS := --rm \
	-u $(shell id -u):$(shell id -g) \
	-e HOME=$(ROOT) \
	-w $(ROOT) \
	-v $(ROOT):$(ROOT) \
	-v /etc/passwd:/etc/passwd:ro \
	-v /etc/group:/etc/group:ro

# Run a normal command inside the ACAP SDK container:
CONTAINER_CMD := $(CONTAINER_RUNTIME) run -i $(CONTAINER_TTY_ARG) \
	$(CONTAINER_ARGS) \
	$(DOCKER_TAG)

# Run a command inside the SDK container with target device credentials available as environment variables:
CONTAINER_TARGET_CMD := $(CONTAINER_RUNTIME) run -i $(CONTAINER_TTY_ARG) \
	$(CONTAINER_ARGS) \
	-e TARGET_IP=$(TARGET_IP) \
	-e TARGET_USR=$(TARGET_USR) \
	-e TARGET_PWD=$(TARGET_PWD) \
	$(DOCKER_TAG)

# Open an interactive shell inside the ACAP SDK container:
CONTAINER_SHELL_CMD := $(CONTAINER_RUNTIME) run -it \
	$(CONTAINER_ARGS) \
	$(DOCKER_TAG)

FINAL ?= y

include helpers.mak

.DEFAULT_GOAL := build

#==============================================================================#
# General project targets:

# Print the list of available Make targets:
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  acap           Build the complete ACAP package from a fresh checkout"
	@echo "  image          Build the ACAP SDK container image"
	@echo "  build          Build the Quake II client"
	@echo "  eap            Build the client, web UI, and EAP"
	@echo "  web            Build the production web UI"
	@echo "  webdev         Run the local Vite development server"
	@echo "  install        Build and install the EAP on the target device"
	@echo "  deploy         Deploy only the Quake II executable"
	@echo "  deployref      Deploy only the Quake II renderer"
	@echo "  deployweb      Deploy only the built web UI"
	@echo "  deployprofile  Deploy the development shell profile"
	@echo "  logon          Open a shell in the installed ACAP directory"
	@echo "  log            Follow target journal logs"
	@echo "  kill           Force-stop the running ACAP process"
	@echo "  checksdk       Print target embedded SDK information"
	@echo "  openweb        Open the ACAP setting page in a browser"
	@echo "  shell          Open an interactive ACAP SDK container"
	@echo "  containerlist  List the Quake II build image"
	@echo "  containerrun   Alias for shell"
	@echo "  containerprune Remove stopped containers"
	@echo "  clean          Remove generated package and web build files"
	@echo "  distclean      Remove all generated build artifacts"

# Initialize all Git submodules required by the project:
.PHONY: submodules
submodules:
	git submodule update --init --recursive

#==============================================================================#
# Main build and package targets:

# Build the complete ACAP package including submodules, container image, and EAP:
.PHONY: acap
acap:
	$(MAKE) submodules
	$(MAKE) image
	$(MAKE) eap

# Build the ACAP SDK container image used for cross-compilation:
.PHONY: image
image:
	$(CONTAINER_RUNTIME) build \
		--build-arg ARCH=$(ARCH) \
		-t $(DOCKER_TAG) \
		./oci

# Build the Quake II client and its required dependencies:
.PHONY: build
build: yquake2-client

# Build the Quake II client, web interface, and final EAP package:
.PHONY: eap
eap: yquake2-client web
	$(CONTAINER_CMD) ./oci/build_eap.sh $(FINAL)

# Build the EAP and install it on the configured target device:
.PHONY: install
install: checktarget eap
	$(CONTAINER_TARGET_CMD) ./oci/eap-install.sh

#==============================================================================#
# Web interface targets:

# Build the production web interface inside the SDK container:
.PHONY: web
web:
	$(CONTAINER_CMD) bash -lc 'cd web && yarn install --frozen-lockfile --cache-folder ../build/yarn-cache && yarn build'

# Run the local Vite development server for the web interface:
.PHONY: webdev
webdev:
	cd web && yarn install --frozen-lockfile && yarn start

#==============================================================================#
# Quake II and dependency build targets:

# Build the custom SDL2 library used by Quake II:
.PHONY: sdl2
sdl2:
	$(CONTAINER_CMD) ./oci/build_sdl2.sh

# Build the libwebsockets library used by the browser input server:
.PHONY: libwebsockets
libwebsockets:
	$(CONTAINER_CMD) ./oci/build_libwebsockets.sh

# Build the base Yamagi Quake II sources:
.PHONY: yquake2-core
yquake2-core:
	$(CONTAINER_CMD) ./oci/build_yquake2.sh

# Build the ACAP-specific Yamagi Quake II client and renderer:
.PHONY: yquake2-client
yquake2-client: sdl2 libwebsockets
	$(CONTAINER_CMD) ./oci/build_yquake2_client.sh

#==============================================================================#
# Source formatting targets:

# Format the C source code with clang-format inside the SDK container:
.PHONY: indent
indent:
	@echo "*** Formatting code"
	@./scripts/container-clang-format.sh

#==============================================================================#
# Cleanup targets:

# Remove generated package files and the production web build:
.PHONY: clean
clean:
	$(RM) *.eap *_LICENSE.txt
	$(RM) package.conf package.conf.orig param.conf
	$(RM) -r web/build

# Remove all generated build artifacts and downloaded dependency outputs:
.PHONY: distclean
distclean: clean
	$(RM) -r build
	$(RM) -r debug release html tmp*
	$(RM) -r web/node_modules web/src/assets/etc
	$(RM) -r third_party/yquake2/build
	$(RM) -r third_party/yquake2/debug
	$(RM) -r third_party/yquake2/release

#==============================================================================#
