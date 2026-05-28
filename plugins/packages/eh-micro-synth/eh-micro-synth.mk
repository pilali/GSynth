################################################################################
#
# eh-micro-synth
#
# Recette Buildroot pour mod-plugin-builder.
#
# À copier dans le tree mod-plugin-builder :
#   mod-plugin-builder/plugins/package/eh-micro-synth/eh-micro-synth.mk
#
# Build pour MOD Dwarf :
#   ./build moddwarf-new eh-micro-synth
#
# Le hash ci-dessous épingle la révision source à compiler.
#
################################################################################

EH_MICRO_SYNTH_VERSION = fdb35853d53e1ae1594072a3b59c6f3c969eec40
EH_MICRO_SYNTH_SITE = $(call github,pilali,GSynth,$(EH_MICRO_SYNTH_VERSION))
EH_MICRO_SYNTH_SITE_METHOD = git
EH_MICRO_SYNTH_BUNDLES = eh_micro_synth.lv2

EH_MICRO_SYNTH_TARGET_MAKE = $(TARGET_MAKE_ENV) $(TARGET_CONFIGURE_OPTS) \
                             $(MAKE) $(MOD_PLUGIN_BUILDER_GCC_FLAGS) -C $(@D)

EH_MICRO_SYNTH_LICENSE = MIT

define EH_MICRO_SYNTH_BUILD_CMDS
	$(EH_MICRO_SYNTH_TARGET_MAKE)
endef

define EH_MICRO_SYNTH_INSTALL_TARGET_CMDS
	cp -rL $(@D)/eh_micro_synth.lv2 $(TARGET_DIR)/usr/lib/lv2/
endef

$(eval $(generic-package))
