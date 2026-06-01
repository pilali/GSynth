################################################################################
#
# gsynth
#
# Recette Buildroot pour mod-plugin-builder.
#
# À copier dans le tree mod-plugin-builder :
#   mod-plugin-builder/plugins/package/gsynth/gsynth.mk
#
# Build pour MOD Dwarf :
#   ./build moddwarf-new gsynth
#
# Le hash ci-dessous épingle la révision source à compiler. À mettre à jour
# vers le dernier commit de la branche après chaque modif significative.
#
################################################################################

GSYNTH_VERSION = 727049ed1e870ebf042c686a8f7af9a8f894b19c
GSYNTH_SITE = $(call github,pilali,GSynth,$(GSYNTH_VERSION))
GSYNTH_BUNDLES = gsynth.lv2

GSYNTH_TARGET_MAKE = $(TARGET_MAKE_ENV) $(TARGET_CONFIGURE_OPTS) \
                     $(MAKE) $(MOD_PLUGIN_BUILDER_GCC_FLAGS) -C $(@D)

GSYNTH_LICENSE = MIT

define GSYNTH_BUILD_CMDS
	$(GSYNTH_TARGET_MAKE)
endef

define GSYNTH_INSTALL_TARGET_CMDS
	cp -rL $(@D)/gsynth.lv2 $(TARGET_DIR)/usr/lib/lv2/
endef

$(eval $(generic-package))
