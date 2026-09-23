#
# Helper targets for deployment and debugging.
# Source setuptarget.sh before using target-side helpers.
#

TARGET_DIR ?= /usr/local/packages/$(PROG)
TARGET_SSH_PORT ?= 22
TARGET_PROTOCOL ?= https

.PHONY: checktarget
checktarget:
ifndef TARGET_IP
	$(error Please source setuptarget.sh first)
endif

.PHONY: checksshpass
checksshpass:
	@command -v sshpass >/dev/null 2>&1 || { \
		echo "sshpass is required for target deployment helpers"; \
		exit 1; \
	}

# Deploy only the Quake II executable to an already installed ACAP.
.PHONY: deploy
deploy: checktarget checksshpass build
	@sshpass -p '$(TARGET_PWD)' scp -P $(TARGET_SSH_PORT) \
		third_party/yquake2/release/quake2 \
		$(TARGET_USR)@$(TARGET_IP):$(TARGET_DIR)/$(PROG)

# Deploy only the Quake II renderer to an already installed ACAP.
.PHONY: deployref
deployref: checktarget checksshpass yquake2-client
	@sshpass -p '$(TARGET_PWD)' scp -P $(TARGET_SSH_PORT) \
		third_party/yquake2/release/ref_gles3.so \
		$(TARGET_USR)@$(TARGET_IP):$(TARGET_DIR)/ref_gles3.so

# Deploy only the built web interface to an already installed ACAP.
.PHONY: deployweb
deployweb: checktarget checksshpass web
	@sshpass -p '$(TARGET_PWD)' ssh -p $(TARGET_SSH_PORT) \
		$(TARGET_USR)@$(TARGET_IP) \
		'rm -rf $(TARGET_DIR)/html/*'
	@sshpass -p '$(TARGET_PWD)' scp -P $(TARGET_SSH_PORT) -r \
		web/build/. \
		$(TARGET_USR)@$(TARGET_IP):$(TARGET_DIR)/html/

# Deploy the development shell profile.
.PHONY: deployprofile
deployprofile: checktarget checksshpass
	@sshpass -p '$(TARGET_PWD)' scp -P $(TARGET_SSH_PORT) \
		scripts/profile \
		$(TARGET_USR)@$(TARGET_IP):~/.profile
	@sshpass -p '$(TARGET_PWD)' ssh -p $(TARGET_SSH_PORT) \
		$(TARGET_USR)@$(TARGET_IP) \
		'sed -i -e "s#/usr/local/packages/xxxxxx#$(TARGET_DIR)#g" -e "s/xxxxxx/$(PROG)/g" ~/.profile'

# Open an interactive shell in the installed ACAP directory.
.PHONY: logon
logon: checktarget checksshpass
	@sshpass -p '$(TARGET_PWD)' ssh -p $(TARGET_SSH_PORT) -t \
		$(TARGET_USR)@$(TARGET_IP) \
		"cd $(TARGET_DIR) && sh"

# Force-stop the running ACAP process.
.PHONY: kill
kill: checktarget checksshpass
	@sshpass -p '$(TARGET_PWD)' ssh -p $(TARGET_SSH_PORT) \
		$(TARGET_USR)@$(TARGET_IP) \
		'kill -KILL $$(pidof $(PROG))'

# Print embedded SDK information from the target.
.PHONY: checksdk
checksdk: checktarget
	@curl --anyauth --insecure --noproxy "*" \
		-u '$(TARGET_USR):$(TARGET_PWD)' \
		'$(TARGET_PROTOCOL)://$(TARGET_IP):$(TARGET_PORT)/axis-cgi/admin/param.cgi?action=list&group=Properties.EmbeddedDevelopment'

# Open the ACAP setting page in the default browser on Linux.
.PHONY: openweb
openweb: checktarget
	@xdg-open '$(TARGET_PROTOCOL)://$(TARGET_IP):$(TARGET_PORT)/local/$(PROG)/' \
		>/dev/null 2>&1

# Follow target journal logs over SSH.
.PHONY: log
log: checktarget
	@./scripts/log.py

# Remove stopped containers.
.PHONY: containerprune
containerprune:
	@$(CONTAINER_RUNTIME) container prune

# List the Quake II build image.
.PHONY: containerlist
containerlist:
	@$(CONTAINER_RUNTIME) image list $(DOCKER_TAG)

# Alias for the interactive SDK shell.
.PHONY: containerrun
containerrun: shell
