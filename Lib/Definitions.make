# These are the names for the various chombo-discharge components. Because we build various components into libraries,
# it makes sense to give the libraries a name through the makefile system. Here, SOURCE_LIB is the library name for the
# the library compiled from $DISCHARGE_HOME/Source, GEOMETRY_LIB is the library name for the files compiled from
# $DISCHARGE_HOME/Geometries and so on.
SOURCE_LIB     = Source
GEOMETRIES_LIB = Geometries

# Physics libraries
ADVDIFF_LIB        = PhysicsAdvectionDiffusionPhysics
BROWNIAN_LIB       = BrownianWalkerPhysics
CDRPLASMA_LIB      = CdrPlasmaPhysics
ELECTROSTATICS_LIB = ElectrostaticPhysics
GEOMETRYONLY_LIB   = GeometryPhysics
ITOPLASMA_LIB      = ItoPlasmaPhysics
RADTRANSFER_LIB    = RadiativeTransferPhysics

# Headers where the chombo-discharge source code is located. This should all folders
# under $DISCHARGE_HOME/Source
SOURCE_DIRS     := $(shell find $(DISCHARGE_HOME)/Source                     -type d -print)
GEOMETRIES_DIRS := $(shell find $(DISCHARGE_HOME)/Geometries                 -type d -print)

SOURCE_INCLUDE          := $(foreach dir, $(SOURCE_DIRS),          $(addprefix -I, $(dir)))
GEOMETRIES_INCLUDE      := $(foreach dir, $(GEOMETRIES_DIRS),      $(addprefix -I, $(dir)))

# Same as for source and geometries dirs/includes, but for the physics modules. 
ADVDIFF_DIRS         := $(shell find $(DISCHARGE_HOME)/Physics/AdvectionDiffusion -type d -print)
BROWNIAN_DIRS        := $(shell find $(DISCHARGE_HOME)/Physics/BrownianWalker     -type d -print)
CDRPLASMA_DIRS       := $(shell find $(DISCHARGE_HOME)/Physics/CdrPlasma          -type d -print)
ELECTROSTATICS_DIRS  := $(shell find $(DISCHARGE_HOME)/Physics/Electrostatics     -type d -print)
GEOMETRYONLY_DIRS    := $(shell find $(DISCHARGE_HOME)/Physics/Geometry           -type d -print)
ITOPLASMA_DIRS       := $(shell find $(DISCHARGE_HOME)/Physics/ItoPlasma          -type d -print)
RADTRANSFER_DIRS     := $(shell find $(DISCHARGE_HOME)/Physics/RadiativeTransfer  -type d -print)

ADVDIFF_INCLUDE         := $(foreach dir, $(ADVDIFF_DIRS),         $(addprefix -I, $(dir)))
BROWNIAN_INCLUDE        := $(foreach dir, $(BROWNIAN_DIRS),        $(addprefix -I, $(dir)))
CDRPLASMA_INCLUDE       := $(foreach dir, $(CDRPLASMA_DIRS),       $(addprefix -I, $(dir)))
ELECTROSTATICS_INCLUDE  := $(foreach dir, $(ELECTROSTATICS_DIRS),  $(addprefix -I, $(dir)))
GEOMETRYONLY_INCLUDE    := $(foreach dir, $(GEOMETRYONLY_DIRS),    $(addprefix -I, $(dir)))
ITOPLASMA_INCLUDE       := $(foreach dir, $(ITOPLASMA_DIRS),       $(addprefix -I, $(dir)))
RADTRANSFER_INCLUDE     := $(foreach dir, $(RADTRANSFER_DIRS),     $(addprefix -I, $(dir)))

# Source and Geometries headers should always be visible. 
XTRACPPFLAGS += $(SOURCE_INCLUDE) 
XTRACPPFLAGS += $(GEOMETRIES_INCLUDE)

# Source and Geometries libraries should always be visible. 
XTRALIBFLAGS += $(addprefix -l, $(SOURCE_LIB))$(config)
XTRALIBFLAGS += $(addprefix -l, $(GEOMETRIES_LIB))$(config)
XTRALIBFLAGS += -L/$(DISCHARGE_HOME)/Lib

# As a rule we always use EB (embedded boundaries) and MF (multi-fluid) from
# Chombo
USE_EB = TRUE
USE_MF = TRUE

# Chombo libraries needed for building chombo-discharge
LibNames = MFElliptic MFTools EBAMRTimeDependent EBAMRElliptic EBAMRTools EBTools AMRElliptic AMRTools \
	AMRTimeDependent BaseTools BoxTools Workshop ParticleTools
