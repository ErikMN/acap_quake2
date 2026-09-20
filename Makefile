PROG := acap_quake2
ARCH := aarch64

CONTAINER_RUNTIME ?= docker
DOCKER_TAG := acap_quake2_$(ARCH)

ROOT := $(CURDIR)

CONTAINER_ARGS := --rm \
	-u $(shell id -u):$(shell id -g) \
	-e HOME=$(ROOT) \
	-w $(ROOT) \
	-v $(ROOT):$(ROOT) \
	-v /etc/passwd:/etc/passwd:ro \
	-v /etc/group:/etc/group:ro

CONTAINER_CMD := $(CONTAINER_RUNTIME) run -i \
	$(CONTAINER_ARGS) \
	$(DOCKER_TAG)

CONTAINER_TARGET_CMD := $(CONTAINER_RUNTIME) run -i \
	$(CONTAINER_ARGS) \
	-e TARGET_IP=$(TARGET_IP) \
	-e TARGET_USR=$(TARGET_USR) \
	-e TARGET_PWD=$(TARGET_PWD) \
	$(DOCKER_TAG)

CONTAINER_SHELL_CMD := $(CONTAINER_RUNTIME) run -it \
	$(CONTAINER_ARGS) \
	$(DOCKER_TAG)

FINAL ?= y

include helpers.mak

.DEFAULT_GOAL := build

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

.PHONY: submodules
submodules:
	git submodule update --init --recursive

.PHONY: acap
acap:
	$(MAKE) submodules
	$(MAKE) image
	$(MAKE) eap

.PHONY: image
image:
	$(CONTAINER_RUNTIME) build \
		--build-arg ARCH=$(ARCH) \
		-t $(DOCKER_TAG) \
		./oci

.PHONY: build
build: yquake2-client

.PHONY: web
web:
	$(CONTAINER_CMD) bash -lc 'cd web && yarn install --frozen-lockfile --cache-folder ../build/yarn-cache && yarn build'

.PHONY: webdev
webdev:
	cd web && yarn install --frozen-lockfile && yarn start

.PHONY: eap
eap: yquake2-client web
	$(CONTAINER_CMD) ./oci/build_eap.sh $(FINAL)

.PHONY: install
install: checktarget eap
	$(CONTAINER_TARGET_CMD) ./oci/eap-install.sh

.PHONY: shell
shell:
	$(CONTAINER_SHELL_CMD) bash

.PHONY: sdl2
sdl2:
	$(CONTAINER_CMD) ./oci/build_sdl2.sh

.PHONY: libwebsockets
libwebsockets:
	$(CONTAINER_CMD) ./oci/build_libwebsockets.sh

.PHONY: yquake2-core
yquake2-core:
	$(CONTAINER_CMD) ./oci/build_yquake2.sh

.PHONY: yquake2-client
yquake2-client: sdl2 libwebsockets
	$(CONTAINER_CMD) ./oci/build_yquake2_client.sh

# Run clang format in Docker:
.PHONY: indent
indent:
	@echo "*** Formatting code"
	@./scripts/container-clang-format.sh

.PHONY: clean
clean:
	$(RM) *.eap *_LICENSE.txt
	$(RM) package.conf package.conf.orig param.conf
	$(RM) -r web/build


.PHONY: distclean
distclean: clean
	$(RM) -r build
	$(RM) -r debug release html tmp*
	$(RM) -r web/node_modules web/src/assets/etc
	$(RM) -r third_party/yquake2/build
	$(RM) -r third_party/yquake2/debug
	$(RM) -r third_party/yquake2/release
