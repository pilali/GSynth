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

GSYNTH_VERSION = fdb35853d53e1ae1594072a3b59c6f3c969eec40
GSYNTH_SITE = $(call github,pilali,GSynth,$(GSYNTH_VERSION))
GSYNTH_SITE_METHOD = git
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
