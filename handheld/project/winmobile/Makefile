#
# Minecraft PE 0.6.1 -- Windows Mobile 6.x / Windows CE 5.2 (HTC HD2)
#
#   make              debug build
#   make release      -Os -DNDEBUG -DPUBLISH, stripped
#   make clean
#
# Cross toolchain is CeGCC 4.4.0.  Point CROSS somewhere else if yours is not
# in /opt/mingw32ce:
#
#   make CROSS=/home/me/cegcc/bin/arm-mingw32ce-
#
# The result is Minecraft.exe.  Copy it to the device together with the game's
# data/ directory (paths are resolved against the .exe's own directory -- CE has
# no working directory).  libGLES_CM.dll and aygshell.dll are already in the
# HD2's ROM, and -static means no libstdc++-6.dll or libgcc_s_sjlj-1.dll to ship
# either: `objdump -p Minecraft.exe | grep "DLL Name"` should list nothing but
# coredll.dll, libGLES_CM.dll, aygshell.dll and ws2.dll.
#

MODULE := Minecraft.exe

CROSS ?= /opt/mingw32ce/bin/arm-mingw32ce-
CXX   := $(CROSS)g++
AR    := $(CROSS)ar
STRIP := $(CROSS)strip
RC    := $(CROSS)windres

SHELL := /bin/bash

#
# Sources
#
# Game code, relative to ../../src.  This is the Raspberry Pi list (the other
# OpenGL ES 1.1 target) with four changes:
#
#   * main_rpi.cpp is out; main.cpp picks up main_winmobile.h instead.
#   * ConsoleScreen.cpp is in.  It is absent from every other Makefile because
#     only the WIN32 branch of Minecraft.cpp instantiates it (the F3 debug
#     console), and no other port compiles that branch -- but CeGCC predefines
#     WIN32, so this one does.
#   * JoinByIPScreen.cpp and TextBox.cpp are in.  Both arrived with the "connect
#     to server by IP" work and neither reached the Pi Makefile; ScreenChooser
#     constructs JoinByIPScreen and Screen owns a TextBox, so leaving them out
#     is an undefined-reference at link time, not a missing feature.
#   * MouseUnlockedScreen.cpp is out.  The Pi Makefile still lists it, but the
#     file does not exist anywhere in the tree and nothing references the class
#     -- that Makefile would die on it ("No rule to make target").
#
# Deliberately not listed, having checked that nothing references them:
# GButton.cpp, SandTile.cpp (orphaned -- Tile::sand is a HeavyTile),
# NATPunchHandler.cpp and PHPDirectoryServer2.cpp (only each other),
# ArgumentsSettings.cpp and main_dedicated.cpp (headless server),
# AppPlatform_win32.cpp and the other main_*.cpp (other platforms), and
# platform/audio/SoundSystem{AL,SL}.cpp -- SoundEngine.h resolves to the
# header-only no-op SoundSystem on anything that is not Android or Apple, which
# is how audio stays stubbed with nothing to link.
#
GAME_SRCS := \
	main.cpp \
	platform/input/Controller.cpp \
	platform/input/Keyboard.cpp \
	platform/input/Mouse.cpp \
	platform/input/Multitouch.cpp \
	platform/time.cpp \
	platform/CThread.cpp \
	NinecraftApp.cpp \
	Performance.cpp \
	SharedConstants.cpp \
	client/IConfigListener.cpp \
	client/Minecraft.cpp \
	client/Options.cpp \
	client/OptionStrings.cpp \
	client/OptionsFile.cpp \
	client/MouseHandler.cpp \
	client/gamemode/CreatorMode.cpp \
	client/gamemode/GameMode.cpp \
	client/gamemode/CreativeMode.cpp \
	client/gamemode/SurvivalMode.cpp \
	client/gui/components/Button.cpp \
	client/gui/components/ImageButton.cpp \
	client/gui/components/GuiElement.cpp \
	client/gui/components/GuiElementContainer.cpp \
	client/gui/components/ItemPane.cpp \
	client/gui/components/InventoryPane.cpp \
	client/gui/components/LargeImageButton.cpp \
	client/gui/components/NinePatch.cpp \
	client/gui/components/RolledSelectionListH.cpp \
	client/gui/components/RolledSelectionListV.cpp \
	client/gui/components/ScrolledSelectionList.cpp \
	client/gui/components/ScrollingPane.cpp \
	client/gui/components/SmallButton.cpp \
	client/gui/components/Slider.cpp \
	client/gui/components/TextBox.cpp \
	client/gui/components/OptionsItem.cpp \
	client/gui/components/OptionsGroup.cpp \
	client/gui/components/OptionsPane.cpp \
	client/gui/Font.cpp \
	client/gui/Gui.cpp \
	client/gui/GuiComponent.cpp \
	client/gui/Screen.cpp \
	client/gui/screens/ScreenChooser.cpp \
	client/gui/screens/ArmorScreen.cpp \
	client/gui/screens/ChatScreen.cpp \
	client/gui/screens/ConfirmScreen.cpp \
	client/gui/screens/ConsoleScreen.cpp \
	client/gui/screens/ChestScreen.cpp \
	client/gui/screens/crafting/PaneCraftingScreen.cpp \
	client/gui/screens/crafting/StonecutterScreen.cpp \
	client/gui/screens/crafting/WorkbenchScreen.cpp \
	client/gui/screens/crafting/CraftingFilters.cpp \
	client/gui/screens/DeathScreen.cpp \
	client/gui/screens/ChooseLevelScreen.cpp \
	client/gui/screens/SimpleChooseLevelScreen.cpp \
	client/gui/screens/FurnaceScreen.cpp \
	client/gui/screens/InBedScreen.cpp \
	client/gui/screens/IngameBlockSelectionScreen.cpp \
	client/gui/screens/JoinGameScreen.cpp \
	client/gui/screens/JoinByIPScreen.cpp \
	client/gui/screens/OptionsScreen.cpp \
	client/gui/screens/PauseScreen.cpp \
	client/gui/screens/ProgressScreen.cpp \
	client/gui/screens/RenameMPLevelScreen.cpp \
	client/gui/screens/SelectWorldScreen.cpp \
	client/gui/screens/StartMenuScreen.cpp \
	client/gui/screens/TextEditScreen.cpp \
	client/gui/screens/touch/TouchIngameBlockSelectionScreen.cpp \
	client/gui/screens/touch/TouchJoinGameScreen.cpp \
	client/gui/screens/touch/TouchSelectWorldScreen.cpp \
	client/gui/screens/touch/TouchStartMenuScreen.cpp \
	client/gui/screens/UploadPhotoScreen.cpp \
	client/model/ChickenModel.cpp \
	client/model/CowModel.cpp \
	client/model/HumanoidModel.cpp \
	client/model/PigModel.cpp \
	client/model/QuadrupedModel.cpp \
	client/model/SheepModel.cpp \
	client/model/SheepFurModel.cpp \
	client/model/geom/Cube.cpp \
	client/model/geom/ModelPart.cpp \
	client/model/geom/Polygon.cpp \
	client/particle/Particle.cpp \
	client/particle/ParticleEngine.cpp \
	client/player/LocalPlayer.cpp \
	client/player/RemotePlayer.cpp \
	client/player/input/KeyboardInput.cpp \
	client/player/input/touchscreen/TouchscreenInput.cpp \
	client/renderer/Chunk.cpp \
	client/renderer/EntityTileRenderer.cpp \
	client/renderer/GameRenderer.cpp \
	client/renderer/ItemInHandRenderer.cpp \
	client/renderer/LevelRenderer.cpp \
	client/renderer/RenderChunk.cpp \
	client/renderer/RenderList.cpp \
	client/renderer/Tesselator.cpp \
	client/renderer/Textures.cpp \
	client/renderer/TileRenderer.cpp \
	client/renderer/gles.cpp \
	client/renderer/culling/Frustum.cpp \
	client/renderer/entity/ArrowRenderer.cpp \
	client/renderer/entity/ChickenRenderer.cpp \
	client/renderer/entity/EntityRenderDispatcher.cpp \
	client/renderer/entity/EntityRenderer.cpp \
	client/renderer/entity/FallingTileRenderer.cpp \
	client/renderer/entity/HumanoidMobRenderer.cpp \
	client/renderer/entity/ItemRenderer.cpp \
	client/renderer/entity/ItemSpriteRenderer.cpp \
	client/renderer/entity/MobRenderer.cpp \
	client/renderer/entity/PaintingRenderer.cpp \
	client/renderer/entity/PlayerRenderer.cpp \
	client/renderer/entity/SheepRenderer.cpp \
	client/renderer/entity/TntRenderer.cpp \
	client/renderer/entity/TripodCameraRenderer.cpp \
	client/renderer/ptexture/DynamicTexture.cpp \
	client/renderer/tileentity/ChestRenderer.cpp \
	client/renderer/tileentity/SignRenderer.cpp \
	client/renderer/tileentity/TileEntityRenderDispatcher.cpp \
	client/renderer/tileentity/TileEntityRenderer.cpp \
	client/sound/Sound.cpp \
	client/sound/SoundEngine.cpp \
	locale/I18n.cpp \
	nbt/Tag.cpp \
	network/command/CommandServer.cpp \
	network/ClientSideNetworkHandler.cpp \
	network/NetEventCallback.cpp \
	network/Packet.cpp \
	network/RakNetInstance.cpp \
	network/ServerSideNetworkHandler.cpp \
	server/CreatorLevel.cpp \
	server/ServerLevel.cpp \
	server/ServerPlayer.cpp \
	util/DataIO.cpp \
	util/Mth.cpp \
	util/StringUtils.cpp \
	util/PerfTimer.cpp \
	util/PerfRenderer.cpp \
	world/Difficulty.cpp \
	world/Direction.cpp \
	world/entity/AgableMob.cpp \
	world/entity/Entity.cpp \
	world/entity/EntityFactory.cpp \
	world/entity/FlyingMob.cpp \
	world/entity/HangingEntity.cpp \
	world/entity/Mob.cpp \
	world/entity/MobCategory.cpp \
	world/entity/Motive.cpp \
	world/entity/Painting.cpp \
	world/entity/PathfinderMob.cpp \
	world/entity/SynchedEntityData.cpp \
	world/entity/ai/control/MoveControl.cpp \
	world/entity/animal/Animal.cpp \
	world/entity/animal/Chicken.cpp \
	world/entity/animal/Cow.cpp \
	world/entity/animal/Pig.cpp \
	world/entity/animal/Sheep.cpp \
	world/entity/animal/WaterAnimal.cpp \
	world/entity/item/FallingTile.cpp \
	world/entity/item/ItemEntity.cpp \
	world/entity/item/PrimedTnt.cpp \
	world/entity/item/TripodCamera.cpp \
	world/entity/monster/Creeper.cpp \
	world/entity/monster/Monster.cpp \
	world/entity/monster/PigZombie.cpp \
	world/entity/monster/Skeleton.cpp \
	world/entity/monster/Spider.cpp \
	world/entity/monster/Zombie.cpp \
	world/entity/projectile/Arrow.cpp \
	world/entity/projectile/Throwable.cpp \
	world/entity/player/Inventory.cpp \
	world/entity/player/Player.cpp \
	world/food/SimpleFoodData.cpp \
	world/inventory/BaseContainerMenu.cpp \
	world/inventory/ContainerMenu.cpp \
	world/inventory/FillingContainer.cpp \
	world/inventory/FurnaceMenu.cpp \
	world/item/ArmorItem.cpp \
	world/item/BedItem.cpp \
	world/item/DyePowderItem.cpp \
	world/item/Item.cpp \
	world/item/ItemInstance.cpp \
	world/item/HangingEntityItem.cpp \
	world/item/HatchetItem.cpp \
	world/item/HoeItem.cpp \
	world/item/PickaxeItem.cpp \
	world/item/ShovelItem.cpp \
	world/item/crafting/Recipe.cpp \
	world/item/crafting/ArmorRecipes.cpp \
	world/item/crafting/Recipes.cpp \
	world/item/crafting/FurnaceRecipes.cpp \
	world/item/crafting/OreRecipes.cpp \
	world/item/crafting/StructureRecipes.cpp \
	world/item/crafting/ToolRecipes.cpp \
	world/item/crafting/WeaponRecipes.cpp \
	world/level/Explosion.cpp \
	world/level/Level.cpp \
	world/level/LightLayer.cpp \
	world/level/LightUpdate.cpp \
	world/level/MobSpawner.cpp \
	world/level/Region.cpp \
	world/level/TickNextTickData.cpp \
	world/level/biome/Biome.cpp \
	world/level/biome/BiomeSource.cpp \
	world/level/chunk/LevelChunk.cpp \
	world/level/dimension/Dimension.cpp \
	world/level/levelgen/CanyonFeature.cpp \
	world/level/levelgen/DungeonFeature.cpp \
	world/level/levelgen/LargeCaveFeature.cpp \
	world/level/levelgen/LargeFeature.cpp \
	world/level/levelgen/RandomLevelSource.cpp \
	world/level/levelgen/feature/Feature.cpp \
	world/level/levelgen/synth/ImprovedNoise.cpp \
	world/level/levelgen/synth/PerlinNoise.cpp \
	world/level/levelgen/synth/Synth.cpp \
	world/level/material/Material.cpp \
	world/level/pathfinder/Path.cpp \
	world/level/storage/ExternalFileLevelStorage.cpp \
	world/level/storage/ExternalFileLevelStorageSource.cpp \
	world/level/storage/FolderMethods.cpp \
	world/level/storage/LevelData.cpp \
	world/level/storage/LevelStorageSource.cpp \
	world/level/storage/RegionFile.cpp \
	world/level/tile/BedTile.cpp \
	world/level/tile/ChestTile.cpp \
	world/level/tile/CropTile.cpp \
	world/level/tile/DoorTile.cpp \
	world/level/tile/EntityTile.cpp \
	world/level/tile/FurnaceTile.cpp \
	world/level/tile/GrassTile.cpp \
	world/level/tile/HeavyTile.cpp \
	world/level/tile/LightGemTile.cpp \
	world/level/tile/MelonTile.cpp \
	world/level/tile/Mushroom.cpp \
	world/level/tile/NetherReactor.cpp \
	world/level/tile/NetherReactorPattern.cpp \
	world/level/tile/StairTile.cpp \
	world/level/tile/StemTile.cpp \
	world/level/tile/StoneSlabTile.cpp \
	world/level/tile/TallGrass.cpp \
	world/level/tile/Tile.cpp \
	world/level/tile/TrapDoorTile.cpp \
	world/level/tile/entity/ChestTileEntity.cpp \
	world/level/tile/entity/NetherReactorTileEntity.cpp \
	world/level/tile/entity/SignTileEntity.cpp \
	world/level/tile/entity/TileEntity.cpp \
	world/level/tile/entity/FurnaceTileEntity.cpp \
	world/phys/HitResult.cpp

# Platform layer, relative to ./src.  main_winmobile.h is header-only and is
# pulled in by ../../src/main.cpp, so it is not listed here.
PLAT_SRCS := \
	AppPlatform_winmobile.cpp \
	png_decode.cpp \
	wince_bigalloc.cpp \
	wince_compat.cpp \
	wince_vibrate.cpp

# RakNet, relative to ../../src/raknet.  Compiled from source -- unlike the Pi
# build, which links a prebuilt libRakNet_static_O1.a that does not exist for
# ARM/CE.
#
# Wildcarded rather than curated, then linked as an archive rather than as loose
# objects.  ld only pulls the members something actually references, and measured
# on the first successful link that is 27 of the 93: everything MCPE never
# touches -- Cloud*, NatPunchthrough*, NatTypeDetection*, UDPProxy*, UDPForwarder,
# ReplicaManager3, Router2, RPC4Plugin, ReadyEvent, MessageFilter,
# FullyConnectedMesh2, DirectoryDeltaTransfer, PacketLogger, ConsoleServer,
# RakNetTransport2, TelnetTransport, LogCommandParser, TwoWayAuthentication,
# Rackspace, EmailSender, DynDNS, TeamBalancer, rdlmalloc -- costs build time and
# not a byte of the 32 MB process budget.
#
# So there is deliberately no _RAKNET_SUPPORT_* trimming in
# src/raknet/NativeFeatureIncludesOverrides.h: the linker already achieves what
# those defines would, without touching a shared header whose own "take care of
# dependencies" block force-enables several plugins regardless.  Check the map
# (make LDFLAGS="-static -Wl,--gc-sections -Wl,-Map=mc.map") before assuming a
# plugin got in.
#
# Getche.cpp is excluded: it is the only file that needs <conio.h>, which CeGCC
# ships as a dead #include_next stub, and its Kbhit/getche are console helpers
# with no caller in the game.
RAKNET_SRCS := $(filter-out Getche.cpp, $(notdir $(wildcard ../../src/raknet/*.cpp)))

# Resource script, relative to ./src.  One resource, and it is not decoration:
# without HI_RES_AWARE the OS hands a WVGA device a virtual QVGA coordinate
# space, which GL ignores and the touch driver does not.  See src/hires.rc.
RC_SRCS := hires.rc

#
# Flags
#
# _WIN32_WCE and WINVER are NOT predefined by arm-mingw32ce-gcc -- only WIN32,
# _WIN32, UNICODE and __COREDLL__ are -- so the SDK headers need to be told
# which CE this is.  0x0502 is Windows CE 5.2, i.e. Windows Mobile 6.x.
#
# GFX_SMALLER_CHUNKS keeps the reduced chunk-loading radius and smaller render
# caches shared with the iOS ARMv6 and Pi builds; on a 32 MB budget it is not
# optional.  OPENGL_ES is not passed: client/renderer/gles.h derives it from
# WINMOBILE, along with USE_VBO.
DEFINES := \
	-DWINMOBILE \
	-D_WIN32_WCE=0x0502 \
	-DWINVER=0x0502 \
	-DGFX_SMALLER_CHUNKS

# include/ must come first: it holds replacements for sysroot headers that are
# unusable as shipped (errno.h, direct.h -- see include/README.md) plus the
# generated GLES/gl.h and EGL/egl.h, and those must win over anything else.
INCLUDES := -Iinclude -Isrc -I../../src -I../../src/raknet

# Left at the toolchain default -- ARMv4, soft float -- deliberately, even
# though the HD2 is an ARMv7 with VFP.  libstdc++.a, libgcc.a and libmingwex.a
# are all prebuilt soft-float ARMv4, so raising -march here would mix float ABIs
# at every library boundary, and Windows CE 5.2 does not save VFP registers
# across all context switches.  Both are worth revisiting *after* the game runs;
# neither is worth debugging before it does.  To try it:
#   make ARM_FLAGS="-march=armv6 -mfloat-abi=softfp -mfpu=vfp"
ARM_FLAGS ?=

# -fno-exceptions/-fno-rtti: nothing in the game or in RakNet throws, catches,
# dynamic_casts or uses typeid (the only throw/catch in src/ are commented-out
# leftovers from the Java original).  Worth switching off on a toolchain that
# uses SJLJ exceptions, where the entry/exit cost is paid per function.
# -fno-strict-aliasing: the NBT and packet code reads one type through a pointer
# to another all over, which GCC 4.4 is happy to miscompile at -Os.
CXXFLAGS := \
	-std=gnu++98 \
	-fno-exceptions \
	-fno-rtti \
	-fno-strict-aliasing \
	-ffunction-sections \
	-fdata-sections \
	-Wno-psabi \
	$(ARM_FLAGS) $(DEFINES) $(INCLUDES)

# Force-included into every translation unit, game and RakNet alike, so the CRT
# redirects it installs (fopen/_access/_mkdir -> path-fixing versions) need no
# edits at any call site.  See src/wince_compat.h.
CXXFLAGS += -include src/wince_compat.h

# ws2 is CE's Winsock 2 (RakNet, and CommandServer's remote console).  libcoredll
# is the C runtime and the Win32 API both; aygshell is SHFullScreen.  -lGLES_CM
# resolves the egl* entry points as well -- the HD2's driver exports them from
# the same DLL, so there is no -lEGL here.
#
# --stack halves the default 2 MB SizeOfStackReserve, and it is one of the
# cheapest megabytes available on this platform.  Windows CE reserves each
# thread's stack *inside* the process's 32 MB slot, and the header value is what
# every thread created with dwStackSize == 0 gets -- the main thread, RakNet's
# peer threads, all of them.  A resident level is already ~21 MB of that slot, so
# 2 MB per thread of address space that is never touched is not affordable, and
# nothing here comes close to needing it (the only 64 KB frames in the tree are
# in RakNet's TableSerializer and SuperFastHash, both in members --gc-sections
# discards).  Only the reserve is given: this ld takes --stack <size> as a single
# number and -Wl, splits on commas anyway, so a "reserve,commit" pair would
# arrive as a stray filename -- harmless to lose, because SizeOfStackCommit is
# already 4 KB by default.
# The level-generation thread asks for 256 KB explicitly -- see CThread.cpp.
#
# --subsystem: the subsystem itself is unchanged (9, Windows CE), only the
# version it claims.  CeGCC defaults to 3.0, where eVC 4 and Visual Studio stamp
# Windows Mobile binaries 4.20.  Set to match, because a binary that announces
# itself as built for Windows CE 3.0 is exactly the one an OS four versions later
# keeps legacy-compatibility paths for, and this port has already been bitten
# once by being treated as a legacy app (see src/hires.rc).
LDFLAGS := -static -Wl,--gc-sections \
           -Wl,--stack,0x100000 \
           -Wl,--subsystem,wince:4.20
LDLIBS  := -Llib -lGLES_CM -laygshell -lws2 -lcoredll

#
# Build mode
#
ifeq ($(BUILD),release)
	CXXFLAGS += -Os -DNDEBUG -DPUBLISH
	OBJDIR := obj/release
else
	CXXFLAGS += -O0 -g
	OBJDIR := obj/debug
endif

RAKNET_LIB := $(OBJDIR)/libraknet_wince.a

# Both configurations link to the same Minecraft.exe, and objects live in
# separate trees -- so after `make release`, a plain `make` would find the exe
# newer than every object in obj/debug and print "Nothing to be done", leaving
# the other configuration's binary sitting there under the expected name.  This
# stamp lives in the *shared* obj/ and is exclusive -- linking removes the other
# mode's -- so switching mode makes it missing, which makes it new, which forces
# the relink.  Same mode twice finds it already there and rebuilds nothing.
BUILD_STAMP := obj/.linked-as-$(if $(filter release,$(BUILD)),release,debug)

GAME_OBJS   := $(addprefix $(OBJDIR)/game/,$(GAME_SRCS:.cpp=.o))
PLAT_OBJS   := $(addprefix $(OBJDIR)/plat/,$(PLAT_SRCS:.cpp=.o))
RAKNET_OBJS := $(addprefix $(OBJDIR)/raknet/,$(RAKNET_SRCS:.cpp=.o))
RC_OBJS     := $(addprefix $(OBJDIR)/res/,$(RC_SRCS:.rc=.o))

OBJS := $(GAME_OBJS) $(PLAT_OBJS)
DEPS := $(OBJS:.o=.d) $(RAKNET_OBJS:.o=.d)

#
# Targets
#
.PHONY: all release clean

all: $(MODULE)

release:
	@$(MAKE) BUILD=release

# -MMD -MP generates the .d as a side effect of the compile, rather than in a
# second preprocessor pass like the Pi Makefile does.  -MP adds a phony target
# per header so a deleted header does not wedge the build.
$(OBJDIR)/game/%.o: ../../src/%.cpp
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -MMD -MP $< -o $@

$(OBJDIR)/plat/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -MMD -MP $< -o $@

$(OBJDIR)/raknet/%.o: ../../src/raknet/%.cpp
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -MMD -MP $< -o $@

# -O coff, not the default: windres would otherwise try to produce a .res, which
# ld cannot link.  No -I/-D are needed -- the script has no #include and no
# conditionals -- and no --target either, since this windres is already
# pe-arm-wince-little.
$(OBJDIR)/res/%.o: src/%.rc
	@mkdir -p $(@D)
	$(RC) -O coff -i $< -o $@

$(RAKNET_LIB): $(RAKNET_OBJS)
	@rm -f $@
	$(AR) rcs $@ $^

$(BUILD_STAMP):
	@mkdir -p $(@D)
	@rm -f obj/.linked-as-*
	@touch $@

# The archive goes last, after the objects that reference it.  The resource
# object goes with the objects rather than into the archive: nothing references
# it by symbol, so as an archive member ld would never pull it in, and even as a
# loose object --gc-sections would drop it if it were an ordinary section -- .rsrc
# survives because ld's PE backend treats resource sections as roots.  Verified
# by the objdump below.
$(MODULE): $(OBJS) $(RC_OBJS) $(RAKNET_LIB) $(BUILD_STAMP)
	$(CXX) $(LDFLAGS) $(OBJS) $(RC_OBJS) $(RAKNET_LIB) $(LDLIBS) -o $@
ifeq ($(BUILD),release)
	$(STRIP) $@
endif
	@echo
	@echo "$@: $$(stat -c %s $@) bytes"
	@$(CROSS)objdump -p $@ | grep -i "DLL Name" | sort -u
	@# HI_RES_AWARE is load-bearing for the display size; fail loudly if the
	@# resource did not make it into the image rather than shipping a build that
	@# silently renders into a quarter of the panel again.
	@$(CROSS)objdump -h $@ | grep -q '\.rsrc' \
		&& echo "HI_RES_AWARE: present (.rsrc in image)" \
		|| { echo "HI_RES_AWARE: MISSING -- .rsrc was not linked in" >&2; exit 1; }

clean:
	rm -rf obj $(MODULE)

-include $(DEPS)
