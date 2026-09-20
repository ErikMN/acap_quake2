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

CONTAINER_SHELL_CMD := $(CONTAINER_RUNTIME) run -it \
	$(CONTAINER_ARGS) \
	$(DOCKER_TAG)

FINAL ?= y

.DEFAULT_GOAL := build

.PHONY: image
image:
	$(CONTAINER_RUNTIME) build \
		--build-arg ARCH=$(ARCH) \
		-t $(DOCKER_TAG) \
		./oci

.PHONY: build
build: yquake2-client

.PHONY: eap
eap: yquake2-client
	$(CONTAINER_CMD) ./oci/build_eap.sh $(FINAL)

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
