PROG := acap_quake2
ARCH := aarch64

CONTAINER_RUNTIME ?= docker
DOCKER_TAG := acap_quake2_$(ARCH)

ROOT := $(CURDIR)

CONTAINER_CMD := $(CONTAINER_RUNTIME) run --rm -i \
	-u $(shell id -u):$(shell id -g) \
	-e HOME=$(ROOT) \
	-w $(ROOT) \
	-v $(ROOT):$(ROOT) \
	-v /etc/passwd:/etc/passwd:ro \
	-v /etc/group:/etc/group:ro \
	$(DOCKER_TAG)

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:.c=.o)

CFLAGS += -DAPP_NAME=\"$(PROG)\"
CFLAGS += -Wall
CFLAGS += -Wextra
CFLAGS += -Wformat=2
CFLAGS += -Wpointer-arith
CFLAGS += -Wvla

FINAL ?= y

ifeq ($(FINAL),y)
	CFLAGS += -DNDEBUG -O2
	LDFLAGS += -s
else
	CFLAGS += -DDEBUG -g3
endif

.DEFAULT_GOAL := all

.PHONY: all
all: $(PROG)

ifdef OECORE_SDK_VERSION

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

else

$(PROG):
	$(error Build $(PROG) inside the ACAP SDK container)

endif

.PHONY: image
image:
	$(CONTAINER_RUNTIME) build \
		--build-arg ARCH=$(ARCH) \
		-t $(DOCKER_TAG) \
		./oci

.PHONY: build
build: image
	$(CONTAINER_CMD) ./oci/build.sh $(FINAL)

.PHONY: eap
eap: image
	$(CONTAINER_CMD) ./oci/build_eap.sh $(FINAL)

.PHONY: shell
shell: image
	$(CONTAINER_CMD) bash

# Run clang format in Docker:
.PHONY: indent
indent:
	@echo "*** Formatting code"
	@./scripts/container-clang-format.sh

.PHONY: clean
clean:
	$(RM) $(PROG) $(OBJS) *.eap *_LICENSE.txt
	$(RM) package.conf package.conf.orig param.conf
