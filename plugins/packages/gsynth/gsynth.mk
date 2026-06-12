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
# Spécificité MOD Dwarf : le CPU ne suit pas avec le pitch tracker YIN, donc
# ce build compile le DSP avec WITH_YIN=0 (-DGSYNTH_NO_YIN, la voie
# Schmitt/flip-flop est toujours utilisée) et, à l'installation, retire le
# switch PTCH du modgui, cache le port pitch_track (pprops:notOnGUI) de la
# liste d'adressage et bascule sur les screenshot/thumbnail *-dwarf.png (sans
# PTCH). Les index/symboles de ports restent identiques au build standard,
# les pedalboards restent donc compatibles entre devices.
#
################################################################################

GSYNTH_VERSION = 727049ed1e870ebf042c686a8f7af9a8f894b19c
GSYNTH_SITE = $(call github,pilali,GSynth,$(GSYNTH_VERSION))
GSYNTH_BUNDLES = gsynth.lv2

GSYNTH_TARGET_MAKE = $(TARGET_MAKE_ENV) $(TARGET_CONFIGURE_OPTS) \
                     $(MAKE) $(MOD_PLUGIN_BUILDER_GCC_FLAGS) -C $(@D)

GSYNTH_LICENSE = MIT

define GSYNTH_BUILD_CMDS
	$(GSYNTH_TARGET_MAKE) WITH_YIN=0
endef

define GSYNTH_INSTALL_TARGET_CMDS
	cp -rL $(@D)/gsynth.lv2 $(TARGET_DIR)/usr/lib/lv2/
	sed -i 's|# GSYNTH_NO_YIN_PORT_HOOK.*|lv2:portProperty pprops:notOnGUI ;|' \
		$(TARGET_DIR)/usr/lib/lv2/gsynth.lv2/gsynth.ttl
	sed -i '/GSYNTH_NO_YIN_BEGIN/,/GSYNTH_NO_YIN_END/d' \
		$(TARGET_DIR)/usr/lib/lv2/gsynth.lv2/modgui/icon-gsynth.html
	sed -i -e 's|screenshot-gsynth.png|screenshot-gsynth-dwarf.png|' \
		-e 's|thumbnail-gsynth.png|thumbnail-gsynth-dwarf.png|' \
		$(TARGET_DIR)/usr/lib/lv2/gsynth.lv2/modgui.ttl
endef

$(eval $(generic-package))
