/* chombo-discharge
 * Copyright © 2021 SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @file   CD_ItoPlasmaStepper.cpp
  @brief  Implementation of CD_ItoPlasmaStepper.H
  @author Robert Marskar
*/

// Std includes
#include <limits>

// Chombo includes
#include <ParmParse.H>

// Our includes
#include <CD_ItoPlasmaStepper.H>
#include <CD_ParticleOps.H>
#include <CD_DataOps.H>
#include <CD_ParallelOps.H>
#include <CD_Units.H>
#include <CD_Timer.H>
#include <CD_Location.H>
#include <CD_NamespaceHeader.H>

using namespace Physics::ItoPlasma;

ItoPlasmaStepper::ItoPlasmaStepper()
{
  CH_TIME("ItoPlasmaStepper::ItoPlasmaStepper");

  // Default settings.
  m_verbosity               = -1;
  m_profile                 = false;
  m_name                    = "ItoPlasmaStepper";
  m_plasmaPhase             = phase::gas;
  m_dt                      = 0.0;
  m_time                    = 0.0;
  m_timeStep                = 0;
  m_loadPerCell             = 1.0;
  m_useNewReactionAlgorithm = true;
  m_regridSuperparticles    = true;
  m_fluidRealm              = Realm::Primal;
  m_particleRealm           = Realm::Primal;
  m_advectionCFL            = 1.0;
  m_diffusionCFL            = std::numeric_limits<Real>::max();
  m_advectionDiffusionCFL   = std::numeric_limits<Real>::max();
  m_relaxTimeFactor         = std::numeric_limits<Real>::max();
  m_minDt                   = std::numeric_limits<Real>::min();
  m_maxDt                   = std::numeric_limits<Real>::max();
}

ItoPlasmaStepper::ItoPlasmaStepper(RefCountedPtr<ItoPlasmaPhysics>& a_physics) : ItoPlasmaStepper()
{
  CH_TIME("ItoPlasmaStepper::ItoPlasmaStepper(RefCountrPtr<ItoPlasmaPhysics>)");

  m_physics = a_physics;
}

ItoPlasmaStepper::~ItoPlasmaStepper() { CH_TIME("ItoPlasmaStepper::~ItoPlasmaStepper"); }

void
ItoPlasmaStepper::parseOptions()
{
  CH_TIME("ItoPlasmaStepper::parseOptions");
  if (m_verbosity > 5) {
    pout() << m_name + "::parseOptions" << endl;
  }

  this->parseVerbosity();
  this->parsePlotVariables();
  this->parseSuperParticles();
  this->parseDualGrid();
  this->parseLoadBalance();
  this->parseTimeStepRestrictions();
  this->parseParametersEB();
}

void
ItoPlasmaStepper::parseRuntimeOptions()
{
  CH_TIME("ItoPlasmaStepper::parseRuntimeOptions");
  if (m_verbosity > 5) {
    pout() << m_name + "::parseRuntimeOptions" << endl;
  }

  this->parseVerbosity();
  this->parsePlotVariables();
  this->parseSuperParticles();
  this->parseLoadBalance();
  this->parseTimeStepRestrictions();
  this->parseParametersEB();

  m_ito->parseRuntimeOptions();
  m_fieldSolver->parseRuntimeOptions();
  m_rte->parseRuntimeOptions();
  m_sigmaSolver->parseRuntimeOptions();

  m_physics->parseRuntimeOptions();
}

void
ItoPlasmaStepper::parseVerbosity() noexcept
{
  CH_TIME("ItoPlasmaStepper::parseVerbosity");
  if (m_verbosity > 5) {
    pout() << m_name + "::parseVerbosity" << endl;
  }

  ParmParse pp(m_name.c_str());

  pp.get("verbosity", m_verbosity);
  pp.get("profile", m_profile);
}

void
ItoPlasmaStepper::parsePlotVariables() noexcept
{
  CH_TIME("ItoPlasmaStepper::parsePlotVariables");
  if (m_verbosity > 5) {
    pout() << m_name + "::parsePlotVariables" << endl;
  }

  m_plotConductivity      = false;
  m_plotCurrentDensity    = false;
  m_plotParticlesPerPatch = false;

  // Read in plot variables.
  ParmParse pp(m_name.c_str());
  const int num = pp.countval("plt_vars");

  if (num > 0) {
    Vector<std::string> str(num);
    pp.getarr("plt_vars", str, 0, num);

    // Set plot variables
    for (int i = 0; i < num; i++) {
      if (str[i] == "conductivity") {
        m_plotConductivity = true;
      }
      else if (str[i] == "current_density") {
        m_plotCurrentDensity = true;
      }
      else if (str[i] == "particles_per_patch") {
        m_plotParticlesPerPatch = true;
      }
    }
  }
}

void
ItoPlasmaStepper::parseSuperParticles() noexcept
{
  CH_TIME("ItoPlasmaStepper::parseSuperParticles");
  if (m_verbosity > 5) {
    pout() << m_name + "::parseSuperParticles" << endl;
  }

  ParmParse pp(m_name.c_str());

  pp.get("particles_per_cell", m_particlesPerCell);
  pp.get("merge_interval", m_mergeInterval);
  pp.get("regrid_superparticles", m_regridSuperparticles);

  if (m_particlesPerCell <= 0) {
    MayDay::Error("ItoPlasmaStepper::parseSuperParticles -- must have 'particles_per_cell' > 0");
  }

  if (m_mergeInterval <= 1) {
    m_mergeInterval = 1;
  }
}

void
ItoPlasmaStepper::parseDualGrid() noexcept
{
  CH_TIME("ItoPlasmaStepper::parseDualGrid");
  if (m_verbosity > 5) {
    pout() << m_name + "::parseDualGrid" << endl;
  }

  ParmParse pp(m_name.c_str());

  pp.get("dual_grid", m_dualGrid);

  if (m_dualGrid) {
    m_particleRealm = "ParticleRealm";

    CH_assert(m_particleRealm != m_fluidRealm);
  }
  else {
    m_particleRealm = m_fluidRealm;
  }
}

void
ItoPlasmaStepper::parseLoadBalance() noexcept
{
  CH_TIME("ItoPlasmaStepper::parseLoadBalance");
  if (m_verbosity > 5) {
    pout() << m_name + "::parseLoadBalance" << endl;
  }

  ParmParse pp(m_name.c_str());

  std::string str;

  pp.get("load_balance", m_loadBalance);
  pp.get("load_index", m_loadBalanceIndex);
  pp.get("load_per_cell", m_loadPerCell);

  // Box sorting for load balancing
  pp.get("box_sorting", str);
  if (str == "none") {
    m_boxSort = BoxSorting::None;
  }
  else if (str == "std") {
    m_boxSort = BoxSorting::Std;
  }
  else if (str == "shuffle") {
    m_boxSort = BoxSorting::Shuffle;
  }
  else if (str == "morton") {
    m_boxSort = BoxSorting::Morton;
  }
  else {
    const std::string err = "ItoPlasmaStepper::parseLoadBalance - 'box_sorting = " + str + "' not recognized";

    MayDay::Error(err.c_str());
  }
}

void
ItoPlasmaStepper::parseTimeStepRestrictions() noexcept
{
  CH_TIME("ItoPlasmaStepper::parseTimeStepRestrictions");
  if (m_verbosity > 5) {
    pout() << m_name + "::parseTimeStepRestrictions" << endl;
  }

  ParmParse pp(m_name.c_str());

  pp.get("advection_cfl", m_advectionCFL);
  pp.get("diffusion_cfl", m_diffusionCFL);
  pp.get("advection_diffusion_cfl", m_advectionDiffusionCFL);
  pp.get("relax_dt_factor", m_relaxTimeFactor);
  pp.get("min_dt", m_minDt);
  pp.get("max_dt", m_maxDt);

  if (m_relaxTimeFactor <= 0.0) {
    MayDay::Error("ItoPlasmaStepper::parseTimeStepRestrictions() - must have relax_dt > 0.0");
  }

  if (m_minDt < 0.0) {
    MayDay::Error("ItoPlasmaStepper::parseTimeStepRestrictions() - must have min_dt >= 0.0");
  }

  if (m_maxDt < 0.0) {
    MayDay::Error("ItoPlasmaStepper::parseTimeStepRestrictions() - must have max_dt >= 0.0");
  }

  if (m_advectionCFL <= 0.0) {
    MayDay::Error("ItoPlasmaStepper::parseTimeStepRestrictions() - must have advection_cfl >= 0.0");
  }

  if (m_diffusionCFL <= 0.0) {
    MayDay::Error("ItoPlasmaStepper::parseTimeStepRestrictions() - must have diffusion_cfl >= 0.0");
  }

  if (m_advectionDiffusionCFL <= 0.0) {
    MayDay::Error("ItoPlasmaStepper::parseTimeStepRestrictions() - must have advection_diffusion_cfl >= 0.0");
  }
}

void
ItoPlasmaStepper::parseParametersEB() noexcept
{
  CH_TIME("ItoPlasmaStepper::parseTimeStepRestrictions");
  if (m_verbosity > 5) {
    pout() << m_name + "::parseTimeStepRestrictions" << endl;
  }

  ParmParse pp(m_name.c_str());

  pp.get("eb_tolerance", m_toleranceEB);
}

void
ItoPlasmaStepper::setupSolvers()
{
  CH_TIME("ItoPlasmaStepper::setupSolver");
  if (m_verbosity > 5) {
    pout() << m_name + "::setupSolvers" << endl;
  }

  // Parse class options
  this->parseOptions();

  // Set up solvers
  this->setupIto();
  this->setupPoisson();
  this->setupRadiativeTransfer();
  this->setupSigma();

  // Allocate internal stuff
  this->allocateInternals();
}

void
ItoPlasmaStepper::setupIto()
{
  CH_TIME("ItoPlasmaStepper::setupIto");
  if (m_verbosity > 5) {
    pout() << m_name + "::setupIto" << endl;
  }

  m_ito->parseOptions();
  m_ito->setAmr(m_amr);
  m_ito->setPhase(m_plasmaPhase);
  m_ito->setComputationalGeometry(m_computationalGeometry);
  m_ito->setRealm(m_particleRealm);
}

void
ItoPlasmaStepper::setupPoisson()
{
  CH_TIME("ItoPlasmaStepper::setupPoisson");
  if (m_verbosity > 5) {
    pout() << m_name + "::setupPoisson" << endl;
  }

  m_fieldSolver->parseOptions();
  m_fieldSolver->setAmr(m_amr);
  m_fieldSolver->setComputationalGeometry(m_computationalGeometry);
  m_fieldSolver->setVoltage(m_voltage);
  m_fieldSolver->setRealm(m_fluidRealm);
}

void
ItoPlasmaStepper::setupRadiativeTransfer()
{
  CH_TIME("ItoPlasmaStepper::setupRadiativeTransfer");
  if (m_verbosity > 5) {
    pout() << m_name + "::setupRadiativeTransfer" << endl;
  }

  m_rte->parseOptions();
  m_rte->setPhase(m_plasmaPhase);
  m_rte->setAmr(m_amr);
  m_rte->setComputationalGeometry(m_computationalGeometry);
  m_rte->setRealm(m_particleRealm);
  m_rte->sanityCheck();
}

void
ItoPlasmaStepper::setupSigma()
{
  CH_TIME("ItoPlasmaStepper::setupSigma");
  if (m_verbosity > 5) {
    pout() << m_name + "::setupSigma" << endl;
  }

  m_sigmaSolver = RefCountedPtr<SurfaceODESolver<1>>(new SurfaceODESolver<1>(m_amr));
  m_sigmaSolver->parseOptions();
  m_sigmaSolver->setRealm(m_fluidRealm);
  m_sigmaSolver->setPhase(m_plasmaPhase);
  m_sigmaSolver->setName("Surface charge");
  m_sigmaSolver->setTime(0, 0.0, 0.0);
}

void
ItoPlasmaStepper::allocate()
{
  CH_TIME("ItoPlasmaStepper::allocate");
  if (m_verbosity > 5) {
    pout() << m_name + "::allocate" << endl;
  }

  // TLDR: The difference between 'allocate' and 'allocateInternals' is that 'allocate' is used by Driver when setting up the simulation, while
  //       'allocateInternals' is used by this class also during regrids.

  // Solver allocation.
  m_ito->allocateInternals();
  m_rte->allocateInternals();
  m_fieldSolver->allocateInternals();
  m_sigmaSolver->allocate();

  this->allocateInternals();
}

void
ItoPlasmaStepper::allocateInternals()
{
  CH_TIME("ItoPlasmaStepper::allocateInternals");
  if (m_verbosity > 5) {
    pout() << m_name + "::allocateInternals" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();
  const int numPhotonSpecies = m_physics->getNumPhotonSpecies();

  if (numPhotonSpecies <= 0) {
    MayDay::Warning("ItoPlasmaStepper::allocate -- how to handle case with no photon species?");
  }
  if (numPlasmaSpecies <= 0) {
    MayDay::Warning("ItoPlasmaStepper::allocate -- how to handle case with no plasma species?");
  }

  // Scratch data.
  m_amr->allocate(m_fluidScratch1, m_fluidRealm, m_plasmaPhase, 1);
  m_amr->allocate(m_fluidScratchD, m_fluidRealm, m_plasmaPhase, SpaceDim);

  m_amr->allocate(m_particleScratch1, m_particleRealm, m_plasmaPhase, 1);
  m_amr->allocate(m_particleScratchD, m_particleRealm, m_plasmaPhase, SpaceDim);

  // Allocate for energy sources
  m_energySources.resize(numPlasmaSpecies);
  for (int i = 0; i < m_energySources.size(); i++) {
    m_amr->allocate(m_energySources[i], m_particleRealm, m_plasmaPhase, 1);
  }

  // Conductivity things. Defined on the fluid realm.
  m_amr->allocate(m_conductivityCell, m_fluidRealm, m_plasmaPhase, 1);
  m_amr->allocate(m_conductivityFace, m_fluidRealm, m_plasmaPhase, 1);
  m_amr->allocate(m_conductivityEB, m_fluidRealm, m_plasmaPhase, 1);

  // Electric field data.
  m_amr->allocate(m_electricFieldParticle, m_particleRealm, m_plasmaPhase, SpaceDim);
  m_amr->allocate(m_electricFieldFluid, m_fluidRealm, m_plasmaPhase, SpaceDim);

  // Particles and photons per cell on the realms.
  m_amr->allocate(m_particlePPC, m_particleRealm, m_plasmaPhase, numPlasmaSpecies);
  m_amr->allocate(m_particleEPS, m_particleRealm, m_plasmaPhase, numPlasmaSpecies);
  m_amr->allocate(m_particleOldPPC, m_particleRealm, m_plasmaPhase, numPlasmaSpecies);
  m_amr->allocate(m_particleYPC, m_particleRealm, m_plasmaPhase, numPhotonSpecies);

  m_amr->allocate(m_fluidPPC, m_fluidRealm, m_plasmaPhase, numPlasmaSpecies);
  m_amr->allocate(m_fluidYPC, m_fluidRealm, m_plasmaPhase, numPhotonSpecies);
  m_amr->allocate(m_fluidEPS, m_fluidRealm, m_plasmaPhase, numPlasmaSpecies);

  // Aux things for current density, source energy source terms etc.
  m_amr->allocate(m_currentDensity, m_fluidRealm, m_plasmaPhase, SpaceDim);
  m_amr->allocate(m_EdotJ, m_fluidRealm, m_plasmaPhase, numPlasmaSpecies);
}

void
ItoPlasmaStepper::postInitialize()
{
  CH_TIME("ItoPlasmaStepper::postInitialize");
  if (m_verbosity > 5) {
    pout() << m_name + "::postInitialize" << endl;
  }
}

void
ItoPlasmaStepper::initialData()
{
  CH_TIME("ItoPlasmaStepper::initialData");
  if (m_verbosity > 5) {
    pout() << m_name + "::initialData" << endl;
  }

  m_fieldSolver->setPermittivities();
  m_ito->initialData();
  m_rte->initialData();
  this->initialSigma();

  // Make superparticles.
  m_ito->sortParticlesByCell(ItoSolver::WhichContainer::Bulk);
  m_ito->makeSuperparticles(ItoSolver::WhichContainer::Bulk, m_particlesPerCell);
  m_ito->sortParticlesByPatch(ItoSolver::WhichContainer::Bulk);

  // Solve Poisson equation and compute the E-field
  this->solvePoisson();

  // Fill solvers with velocities and diffusion coefficients
  this->computeItoVelocities();
  this->computeItoDiffusion();
}

void
ItoPlasmaStepper::initialSigma()
{
  CH_TIME("ItoPlasmaStepper::initialSigma");
  if (m_verbosity > 5) {
    pout() << m_name + "::initialSigma" << endl;
  }

  const RealVect probLo = m_amr->getProbLo();

  EBAMRIVData& sigma = m_sigmaSolver->getPhi();

  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++) {
    const DisjointBoxLayout& dbl   = m_amr->getGrids(m_sigmaSolver->getRealm())[lvl];
    const EBISLayout&        ebisl = m_amr->getEBISLayout(m_sigmaSolver->getRealm(), m_sigmaSolver->getPhase())[lvl];
    const Real               dx    = m_amr->getDx()[lvl];

    for (DataIterator dit(dbl); dit.ok(); ++dit) {
      BaseIVFAB<Real>& phi     = (*sigma[lvl])[dit()];
      const EBISBox&   ebisbox = ebisl[dit()];

      CH_assert(phi.nComp() == 1);

      auto kernel = [&](const VolIndex& vof) -> void {
        const RealVect pos = probLo + Location::position(Location::Cell::Boundary, vof, ebisbox, dx);

        phi(vof, 0) = m_physics->initialSigma(m_time, pos);
      };

      VoFIterator& vofit = (*m_amr->getVofIterator(m_sigmaSolver->getRealm(), m_sigmaSolver->getPhase())[lvl])[dit()];

      BoxLoops::loop(vofit, kernel);
    }
  }

  // Coarsen throughout the AMR hierarchy.
  m_amr->conservativeAverage(sigma, m_fluidRealm, m_sigmaSolver->getPhase());

  // Set surface charge to zero on electrode cut-cells.
  m_sigmaSolver->resetElectrodes(sigma, 0.0);
}

void
ItoPlasmaStepper::postCheckpointSetup()
{
  CH_TIME("ItoPlasmaStepper::postCheckpointSetup");
  if (m_verbosity > 5) {
    pout() << m_name + "::postCheckpointSetup" << endl;
  }

  // Allocate internal storage.
  this->allocateInternals();

  m_ito->remap();

  // Recompute the electric field.
  this->postCheckpointPoisson();

  // Compute velocities and diffusion coefficients so we're prepared for the next time step.
  this->computeItoVelocities();
  this->computeItoDiffusion();
}

void
ItoPlasmaStepper::postCheckpointPoisson()
{
  CH_TIME("ItoPlasmaStepper::postCheckpointPoisson");
  if (m_verbosity > 5) {
    pout() << m_name + "::postCheckpointPoisson" << endl;
  }

  // Do some post checkpointing stuff.
  m_fieldSolver->postCheckpoint();

  // Update ghost cells and re-compute the electric field from the HDF5 data.
  MFAMRCellData& potential = m_fieldSolver->getPotential();

  m_amr->conservativeAverage(potential, m_fluidRealm);
  m_amr->interpGhostMG(potential, m_fluidRealm);

  m_fieldSolver->computeElectricField();

  // Fetch the electric field data on the plasma phase.
  const EBAMRCellData E = m_amr->alias(m_plasmaPhase, m_fieldSolver->getElectricField());

  // Copy onto the storage holding the electric field on the fluid realm. Then interpolate to centroids.
  m_electricFieldFluid.copy(E);
  m_amr->conservativeAverage(m_electricFieldFluid, m_fluidRealm, m_plasmaPhase);
  m_amr->interpGhostPwl(m_electricFieldFluid, m_fluidRealm, m_plasmaPhase);
  m_amr->interpToCentroids(m_electricFieldFluid, m_fluidRealm, m_plasmaPhase);

  // Copy onto the storage holding the electric field on the particle realm.
  m_electricFieldParticle.copy(E);
  m_amr->conservativeAverage(m_electricFieldParticle, m_particleRealm, m_plasmaPhase);
  m_amr->interpGhostPwl(m_electricFieldParticle, m_particleRealm, m_plasmaPhase);
  m_amr->interpToCentroids(m_electricFieldParticle, m_particleRealm, m_plasmaPhase);

  // Set up the Poisson solver
  m_fieldSolver->setupSolver();
}

#ifdef CH_USE_HDF5
void
ItoPlasmaStepper::writeCheckpointData(HDF5Handle& a_handle, const int a_lvl) const
{
  CH_TIME("ItoPlasmaStepper::writeCheckpointData");
  if (m_verbosity > 5) {
    pout() << m_name + "::writeCheckpointData" << endl;
  }

  for (ItoIterator<ItoSolver> solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    solverIt()->writeCheckpointLevel(a_handle, a_lvl);
  }

  for (RtIterator<McPhoto> solverIt = m_rte->iterator(); solverIt.ok(); ++solverIt) {
    solverIt()->writeCheckpointLevel(a_handle, a_lvl);
  }

  m_fieldSolver->writeCheckpointLevel(a_handle, a_lvl);
  m_sigmaSolver->writeCheckpointLevel(a_handle, a_lvl);
}
#endif

#ifdef CH_USE_HDF5
void
ItoPlasmaStepper::readCheckpointData(HDF5Handle& a_handle, const int a_lvl)
{
  CH_TIME("ItoPlasmaStepper::readCheckpointData");
  if (m_verbosity > 5) {
    pout() << m_name + "::readCheckpointData" << endl;
  }

  for (ItoIterator<ItoSolver> solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    solverIt()->readCheckpointLevel(a_handle, a_lvl);
  }

  for (RtIterator<McPhoto> solverIt = m_rte->iterator(); solverIt.ok(); ++solverIt) {
    solverIt()->readCheckpointLevel(a_handle, a_lvl);
  }

  m_fieldSolver->readCheckpointLevel(a_handle, a_lvl);
  m_sigmaSolver->readCheckpointLevel(a_handle, a_lvl);
}
#endif

void
ItoPlasmaStepper::writePlotData(EBAMRCellData& a_output, Vector<std::string>& a_plotVariableNames, int& a_icomp) const
{
  CH_TIME("ItoPlasmaStepper::writePlotData");
  if (m_verbosity > 5) {
    pout() << m_name + "::writePlotData" << endl;
  }

  // Poisson solver copies over its output data
  a_plotVariableNames.append(m_fieldSolver->getPlotVariableNames());
  m_fieldSolver->writePlotData(a_output, a_icomp);

  // Surface charge solver writes
  a_plotVariableNames.append(m_sigmaSolver->getPlotVariableNames());
  m_sigmaSolver->writePlotData(a_output, a_icomp);

  // Ito solvers copy their output data
  m_ito->depositParticles();
  for (ItoIterator<ItoSolver> solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>& solver = solverIt();

    a_plotVariableNames.append(solver->getPlotVariableNames());
    solver->writePlotData(a_output, a_icomp);
  }

  // RTE solvers copy their output data
  for (RtIterator<McPhoto> solverIt = m_rte->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<McPhoto>& solver = solverIt();

    a_plotVariableNames.append(solver->getPlotVariableNames());
    solver->writePlotData(a_output, a_icomp);
  }

  // Write the conductivity to the output
  if (m_plotConductivity) {
    a_output.copy(Interval(0, 0), m_conductivityCell, Interval(a_icomp, a_icomp));
    a_plotVariableNames.push_back("Conductivity");
    a_icomp++;
  }

  // Write the current to the output
  if (m_plotCurrentDensity) {
    const Interval srcInterv(0, SpaceDim - 1);
    const Interval dstInterv(a_icomp, a_icomp + SpaceDim - 1);
    a_output.copy(srcInterv, m_currentDensity, dstInterv);

    a_plotVariableNames.push_back("x-J");
    a_plotVariableNames.push_back("y-J");
    if (SpaceDim == 3) {
      a_plotVariableNames.push_back("z-J");
    }

    a_icomp += SpaceDim;
  }

  // Write the number of particles per patch
  if (m_plotParticlesPerPatch) {
    this->writeNumParticlesPerPatch(a_output, a_icomp);
    a_plotVariableNames.push_back("Particles per patch");
    a_icomp++;
  }
}

void
ItoPlasmaStepper::writeNumParticlesPerPatch(EBAMRCellData& a_output, int& a_icomp) const
{
  CH_TIME("ItoPlasmaStepper::writeNumParticlesPerPatch");
  if (m_verbosity > 5) {
    pout() << m_name + "::writeNumParticlesPerPatch" << endl;
  }

  DataOps::setValue(m_particleScratch1, 0.0);

  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    const ParticleContainer<ItoParticle>& particles = solverIt()->getParticles(ItoSolver::WhichContainer::Bulk);

    for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++) {
      for (DataIterator dit(m_amr->getGrids(m_particleRealm)[lvl]); dit.ok(); ++dit) {
        (*m_particleScratch1[lvl])[dit()] += particles[lvl][dit].numItems();
      }
    }
  }

  a_output.copy(Interval(0, 0), m_particleScratch1, Interval(a_icomp, a_icomp));

  a_icomp += 1;
}

void
ItoPlasmaStepper::synchronizeSolverTimes(const int a_step, const Real a_time, const Real a_dt)
{
  CH_TIME("ItoPlasmaStepper::synchronizeSolverTimes");
  if (m_verbosity > 5) {
    pout() << m_name + "::synchronizeSolverTimes" << endl;
  }

  m_timeStep = a_step;
  m_time     = a_time;
  m_dt       = a_dt;

  m_ito->setTime(a_step, a_time, a_dt);
  m_fieldSolver->setTime(a_step, a_time, a_dt);
  m_rte->setTime(a_step, a_time, a_dt);
  m_sigmaSolver->setTime(a_step, a_time, a_dt);
}

void
ItoPlasmaStepper::printStepReport()
{
  CH_TIME("ItoPlasmaStepper::printStepReport");
  if (m_verbosity > 5) {
    pout() << m_name + "::printStepReport" << endl;
  }

  const Real Emax = this->computeMaxElectricField(m_plasmaPhase);

  const size_t localParticlesBulk    = m_ito->getNumParticles(ItoSolver::WhichContainer::Bulk, true);
  const size_t globalParticlesBulk   = m_ito->getNumParticles(ItoSolver::WhichContainer::Bulk, false);
  const size_t localParticlesEB      = m_ito->getNumParticles(ItoSolver::WhichContainer::EB, true);
  const size_t globalParticlesEB     = m_ito->getNumParticles(ItoSolver::WhichContainer::EB, false);
  const size_t localParticlesDomain  = m_ito->getNumParticles(ItoSolver::WhichContainer::Domain, true);
  const size_t globalParticlesDomain = m_ito->getNumParticles(ItoSolver::WhichContainer::Domain, false);
  const size_t localParticlesSource  = m_ito->getNumParticles(ItoSolver::WhichContainer::Source, true);
  const size_t globalParticlesSource = m_ito->getNumParticles(ItoSolver::WhichContainer::Source, false);

  // Compute some global particle statistics
  Real avgParticles = 0.0;
  Real stdDev       = 0.0;

  Real minParticles = 0.0;
  Real maxParticles = 0.0;

  int minRank = 0;
  int maxRank = 0;

  this->getParticleStatistics(avgParticles, stdDev, minParticles, maxParticles, minRank, maxRank);

  Real maxDensity = 0.0;
  Real minDensity = 0.0;

  std::string maxSolver = "invalid solver";
  std::string minSolver = "invalid solver";

  this->getMaxMinItoDensity(maxDensity, minDensity, maxSolver, minSolver);

  // How was the time step restricted
  std::string str;
  switch (m_timeCode) {
  case TimeCode::Physics: {
    str = "dt restricted by 'Physics'";

    break;
  }
  case TimeCode::Advection: {
    str = "dt restricted by 'Advection'";

    break;
  }
  case TimeCode::Diffusion: {
    str = "dt restricted by 'Diffusion'";

    break;
  }
  case TimeCode::AdvectionDiffusion: {
    str = "dt restricted by 'AdvectionDiffusion'";

    break;
  }
  case TimeCode::RelaxationTime: {
    str = "dt restricted by 'Relaxation time'";

    break;
  }
  case TimeCode::Hardcap: {
    str = "dt restricted by 'Hardcap'";

    break;
  }
  default: {
    str = "dt restricted by 'Unspecified'";

    break;
  }
  }

  // Print the step report.
  pout() << "                                   " + str << endl;
  pout() << "                                   Emax        = " << Emax << endl
         << "                                   Max density = " << maxDensity << " (" << maxSolver << ")" << endl
         << "                                   CFL         = " << m_dt / m_advectionDiffusionDt << endl
         << "                                   Relax time  = " << m_dt / m_relaxationTime << endl
         << "                                   #Particles  = " << localParticlesBulk << " (" << globalParticlesBulk
         << ")" << endl
         << "                                   #EB part.   = " << localParticlesEB << " (" << globalParticlesEB << ")"
         << endl
         << "                                   #Dom. part. = " << localParticlesDomain << " (" << globalParticlesDomain
         << ")" << endl
         << "                                   #Src. part. = " << localParticlesSource << " (" << globalParticlesSource
         << ")" << endl
         << "                                   #Min part.  = " << minParticles << " (on rank = " << minRank << ")"
         << endl
         << "                                   #Max part.  = " << maxParticles << " (on rank = " << maxRank << ")"
         << endl
         << "                                   #Avg. part. = " << avgParticles << endl
         << "                                   #Dev. part. = " << stdDev << " (" << 100. * stdDev / avgParticles
         << "%)" << endl;
}

void
ItoPlasmaStepper::getMaxMinItoDensity(Real&        a_maxDensity,
                                      Real&        a_minDensity,
                                      std::string& a_maxSolver,
                                      std::string& a_minSolver) const noexcept
{
  CH_TIME("ItoPlasmaStepper::getMaxMinDensity(Realx2, std::string2x)");
  if (m_verbosity > 5) {
    pout() << m_name + "::getMaxMinDensity(Realx2, std::string2x)" << endl;
  }

  // Need to initialize.
  a_minDensity = std::numeric_limits<Real>::max();
  a_maxDensity = -std::numeric_limits<Real>::max();

  std::string maxSolver = "invalid solver";
  std::string minSolver = "invalid solver";

  // Go through each solver and find the max/min values.
  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    const RefCountedPtr<ItoSolver>& solver = solverIt();

    Real curMin = std::numeric_limits<Real>::max();
    Real curMax = -std::numeric_limits<Real>::max();

    DataOps::getMaxMin(curMax, curMin, solverIt()->getPhi(), 0);

    if (curMax > a_maxDensity) {
      a_maxDensity = curMax;
      a_maxSolver  = solver->getName();
    }

    if (curMin < a_minDensity) {
      a_minDensity = curMin;
      a_minSolver  = solver->getName();
    }
  }
}

void
ItoPlasmaStepper::getParticleStatistics(Real& a_avgParticles,
                                        Real& a_sigma,
                                        Real& a_minParticles,
                                        Real& a_maxParticles,
                                        int&  a_minRank,
                                        int&  a_maxRank)
{
  CH_TIME("ItoPlasmaStepper::getParticleStatistics");
  if (m_verbosity > 5) {
    pout() << m_name + "::getParticleStatistics" << endl;
  }

  // TLDR: We compute the number of particles, the standard deviation of the number of particles, as well
  //       as the ranks having the smallest/largest number of particles.

  const Real numParticles = 1.0 * m_ito->getNumParticles(ItoSolver::WhichContainer::Bulk, true);

  const std::pair<Real, int> minParticles = ParallelOps::minRank(numParticles);
  const std::pair<Real, int> maxParticles = ParallelOps::maxRank(numParticles);

  a_avgParticles = ParallelOps::average(numParticles);
  a_sigma        = ParallelOps::standardDeviation(numParticles);

  a_minParticles = minParticles.first;
  a_maxParticles = maxParticles.first;

  a_minRank = minParticles.second;
  a_maxRank = maxParticles.second;
}

Real
ItoPlasmaStepper::computeDt()
{
  CH_TIME("ItoPlasmaStepper::computeDt");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeDt" << endl;
  }

  Timer timer(m_name + "::computeDt");

  Real dt = std::numeric_limits<Real>::max();

  // Compute various time steps.
  timer.startEvent("Advection");
  m_advectionDt = m_ito->computeAdvectiveDt();
  timer.stopEvent("Advection");

  timer.startEvent("Diffusion");
  m_diffusionDt = m_ito->computeDiffusiveDt();
  timer.stopEvent("Diffusion");

  timer.startEvent("AdvectionDiffusion");
  m_advectionDiffusionDt = m_ito->computeDt();
  timer.stopEvent("AdvectionDiffusion");

  timer.startEvent("Physics");
  m_physicsDt = this->computePhysicsDt();
  timer.stopEvent("Physics");

  timer.startEvent("Relaxation");
  m_relaxationTime = this->computeRelaxationTime();
  timer.stopEvent("Relaxation");

  if (m_advectionCFL * m_advectionDt < dt) {
    dt         = m_advectionCFL * m_advectionDt;
    m_timeCode = TimeCode::Advection;
  }

  if (m_relaxTimeFactor * m_relaxationTime < dt) {
    dt         = m_relaxTimeFactor * m_relaxationTime;
    m_timeCode = TimeCode::RelaxationTime;
  }

  if (m_diffusionCFL * m_diffusionDt < dt) {
    dt         = m_diffusionCFL * m_diffusionDt;
    m_timeCode = TimeCode::Diffusion;
  }

  if (m_advectionDiffusionCFL * m_advectionDiffusionDt < dt) {
    dt         = m_advectionDiffusionCFL * m_advectionDiffusionDt;
    m_timeCode = TimeCode::AdvectionDiffusion;
  }

  if (m_physicsDt < dt) {
    dt         = m_physicsDt;
    m_timeCode = TimeCode::Physics;
  }

  if (m_minDt > dt) {
    dt         = m_minDt;
    m_timeCode = TimeCode::Hardcap;
  }

  if (m_maxDt < dt) {
    dt         = m_maxDt;
    m_timeCode = TimeCode::Hardcap;
  }

  if (m_profile) {
    timer.eventReport(pout(), false);
  }

  return dt;
}

void
ItoPlasmaStepper::registerRealms()
{
  CH_TIME("ItoPlasmaStepper::registerRealms");
  if (m_verbosity > 5) {
    pout() << m_name + "::registerRealms" << endl;
  }

  // TLDR: If using dual grid then m_particleRealm != m_fluidRealm and we'll have two realms.
  m_amr->registerRealm(m_fluidRealm);
  m_amr->registerRealm(m_particleRealm);
}

void
ItoPlasmaStepper::registerOperators()
{
  CH_TIME("ItoPlasmaStepper::registerOperators");
  if (m_verbosity > 5) {
    pout() << m_name + "::registerOperators" << endl;
  }

  m_ito->registerOperators();
  m_fieldSolver->registerOperators();
  m_rte->registerOperators();
  m_sigmaSolver->registerOperators();
}

void
ItoPlasmaStepper::preRegrid(const int a_lmin, const int a_oldFinestLevel)
{
  CH_TIME("ItoPlasmaStepper::preRegrid");
  if (m_verbosity > 5) {
    pout() << m_name + "::preRegrid" << endl;
  }

  m_ito->preRegrid(a_lmin, a_oldFinestLevel);
  m_fieldSolver->preRegrid(a_lmin, a_oldFinestLevel);
  m_rte->preRegrid(a_lmin, a_oldFinestLevel);
  m_sigmaSolver->preRegrid(a_lmin, a_oldFinestLevel);
}

void
ItoPlasmaStepper::regrid(const int a_lmin, const int a_oldFinestLevel, const int a_newFinestLevel)
{
  CH_TIME("ItoPlasmaStepper::regrid");
  if (m_verbosity > 5) {
    pout() << m_name + "::regrid" << endl;
  }

  // Allocate new memory
  this->allocateInternals();

  // Regrid solvers
  m_ito->regrid(a_lmin, a_oldFinestLevel, a_newFinestLevel);
  m_fieldSolver->regrid(a_lmin, a_oldFinestLevel, a_newFinestLevel);
  m_rte->regrid(a_lmin, a_oldFinestLevel, a_newFinestLevel);
  m_sigmaSolver->regrid(a_lmin, a_oldFinestLevel, a_newFinestLevel);

  if (m_regridSuperparticles) {
    m_ito->sortParticlesByCell(ItoSolver::WhichContainer::Bulk);
    m_ito->makeSuperparticles(ItoSolver::WhichContainer::Bulk, m_particlesPerCell);
    m_ito->sortParticlesByPatch(ItoSolver::WhichContainer::Bulk);
  }

  // Redeposit particles
  m_ito->depositParticles();

  // Recompute the electric field
  const bool converged = this->solvePoisson();
  if (!converged) {
    MayDay::Abort("ItoPlasmaStepper::regrid - Poisson solve did not converge after regrid!!!");
  }

  // Recompute new velocities and diffusion coefficients
  this->computeItoVelocities();
  this->computeItoDiffusion();
}

void
ItoPlasmaStepper::postRegrid()
{
  CH_TIME("ItoPlasmaStepper::postRegrid");
}

int
ItoPlasmaStepper::getNumberOfPlotVariables() const
{
  CH_TIME("ItoPlasmaStepper::getNumberOfPlotVariables");
  if (m_verbosity > 5) {
    pout() << m_name + "::getNumberOfPlotVariables" << endl;
  }

  int ncomp = 0;

  // Ito solver variables.
  for (ItoIterator<ItoSolver> solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    ncomp += solverIt()->getNumberOfPlotVariables();
  }

  // RTE solver variables.
  for (RtIterator<McPhoto> solverIt = m_rte->iterator(); solverIt.ok(); ++solverIt) {
    ncomp += solverIt()->getNumberOfPlotVariables();
  }

  // Field solver variables.
  ncomp += m_fieldSolver->getNumberOfPlotVariables();

  // Surface charge solver variables.
  ncomp += m_sigmaSolver->getNumberOfPlotVariables();

  // Conductivity
  if (m_plotConductivity) {
    ncomp += 1;
  }

  // Current density.
  if (m_plotCurrentDensity) {
    ncomp += SpaceDim;
  }

  // Number of particles per patch
  if (m_plotParticlesPerPatch) {
    ncomp += 1;
  }

  return ncomp;
}

void
ItoPlasmaStepper::setIto(RefCountedPtr<ItoLayout<ItoSolver>>& a_ito) noexcept
{
  CH_TIME("ItoPlasmaStepper::setIto");
  if (m_verbosity > 5) {
    pout() << m_name + "::setIto" << endl;
  }

  m_ito = a_ito;
}

void
ItoPlasmaStepper::setFieldSolver(RefCountedPtr<FieldSolver>& a_fieldSolver) noexcept
{
  CH_TIME("ItoPlasmaStepper::setFieldSolver");
  if (m_verbosity > 5) {
    pout() << m_name + "::setFieldSolver" << endl;
  }

  m_fieldSolver = a_fieldSolver;
}

void
ItoPlasmaStepper::setRadiativeTransferSolvers(RefCountedPtr<RtLayout<McPhoto>>& a_rteLayout) noexcept
{
  CH_TIME("ItoPlasmaStepper::setRadiativeTransferSolvers");
  if (m_verbosity > 5) {
    pout() << m_name + "::setRadiativeTransferSolvers" << endl;
  }

  m_rte = a_rteLayout;
}

void
ItoPlasmaStepper::setVoltage(const std::function<Real(const Real a_time)>& a_voltage) noexcept
{
  CH_TIME("ItoPlasmaStepper::setVoltage");
  if (m_verbosity > 5) {
    pout() << m_name + "::setVoltage" << endl;
  }

  m_voltage = a_voltage;
}

Real
ItoPlasmaStepper::computeMaxElectricField(const phase::which_phase a_phase) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeMaxElectricField");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeMaxElectricField" << endl;
  }

  // Get a handle to the E-field. Note that this is the cell-centered field!
  const EBAMRCellData cellCenteredE = m_amr->alias(a_phase, m_fieldSolver->getElectricField());

  // Interpolate to centroids
  EBAMRCellData centroidCenteredE;
  m_amr->allocate(centroidCenteredE, m_fluidRealm, a_phase, SpaceDim);

  DataOps::copy(centroidCenteredE, cellCenteredE);

  m_amr->interpToCentroids(centroidCenteredE, m_fluidRealm, m_plasmaPhase);

  Real max = 0.0;
  Real min = 0.0;

  DataOps::getMaxMinNorm(max, min, centroidCenteredE);

  return max;
}

void
ItoPlasmaStepper::computeElectricField(EBAMRCellData& a_electricField, const phase::which_phase a_phase) const noexcept
{
  CH_TIME("ItoPlasmaStepper::computeElectricField(EBAMRCellData, phase)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeElectricField(EBAMRCellData, phase)" << endl;
  }

  CH_assert(a_electricField.getRealm() == m_fluidRealm);

  m_fieldSolver->computeElectricField(a_electricField, a_phase, m_fieldSolver->getPotential());
}

Real
ItoPlasmaStepper::getTime() const noexcept
{
  CH_TIME("ItoPlasmaStepper::getTime");
  if (m_verbosity > 5) {
    pout() << m_name + "::getTime" << endl;
  }

  return m_time;
}

void
ItoPlasmaStepper::computeSpaceChargeDensity() noexcept
{
  CH_TIME("ItoPlasmaStepper::computeSpaceChargeDensity()");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeSpaceChargeDensity()" << endl;
  }

  this->computeSpaceChargeDensity(m_fieldSolver->getRho(), m_ito->getDensities());
}

void
ItoPlasmaStepper::computeSpaceChargeDensity(MFAMRCellData& a_rho, const Vector<EBAMRCellData*>& a_densities) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeSpaceChargeDensity(rho, densities)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeSpaceChargeDensity(rho, densities)" << endl;
  }

  // TLDR: a_densities is from the Ito solvers so it is defined over the particle realm. This CAN be equal to
  //       the same realm on which a_rho is defined, but not necessarily. So, we use m_fluidScratch1 as temporary
  //       scratch storage.

  // Reset
  DataOps::setValue(a_rho, 0.0);

  // Alias for the plasma phase.
  EBAMRCellData rhoPhase = m_amr->alias(m_plasmaPhase, a_rho);

  // Increment each solver
  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    const RefCountedPtr<ItoSolver>&  solver  = solverIt();
    const RefCountedPtr<ItoSpecies>& species = solver->getSpecies();
    const int                        idx     = solverIt.index();
    const int                        Z       = species->getChargeNumber();

    if (Z != 0) {
      m_fluidScratch1.copy(*a_densities[idx]);
      DataOps::incr(rhoPhase, m_fluidScratch1, 1.0 * Z);
    }
  }

  DataOps::scale(a_rho, Units::Qe);

  m_amr->conservativeAverage(a_rho, m_fluidRealm);
  m_amr->interpGhostMG(a_rho, m_fluidRealm);

  // Interpolate to centroids.
  m_amr->interpToCentroids(rhoPhase, m_fluidRealm, m_plasmaPhase);
}

void
ItoPlasmaStepper::computeConductivityCell(EBAMRCellData& a_conductivity) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeConductivityCell(EBAMRCellData)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeConductivityCell(EBAMRCellData)" << endl;
  }

  this->computeConductivityCell(a_conductivity, m_ito->getParticles(ItoSolver::WhichContainer::Bulk));
}

void
ItoPlasmaStepper::computeConductivityCell(EBAMRCellData&                                 a_conductivity,
                                          const Vector<ParticleContainer<ItoParticle>*>& a_particles) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeConductivityCell(EBAMRCellData, Particles)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeConductivityCell(EBAMRCellData, Particles)" << endl;
  }

  // TLDR: This will deposit the particle conductivity on the mesh (onto m_particleScratch1) which is
  //       then added to the total conductivity.

  DataOps::setValue(a_conductivity, 0.0);

  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>&        solver  = solverIt();
    const RefCountedPtr<ItoSpecies>& species = solver->getSpecies();

    const int idx = solverIt.index();
    const int Z   = species->getChargeNumber();

    if (Z != 0 && solver->isMobile()) {
      solver->depositConductivity(m_particleScratch1, *a_particles[idx]);

      // Add to the fluid realm.
      m_fluidScratch1.copy(m_particleScratch1);
      DataOps::incr(a_conductivity, m_fluidScratch1, 1.0 * std::abs(Z));
    }
  }

  DataOps::scale(a_conductivity, Units::Qe);

  m_amr->conservativeAverage(a_conductivity, m_fluidRealm, m_plasmaPhase);
  m_amr->interpGhostMG(a_conductivity, m_fluidRealm, m_plasmaPhase);

  // Interpolate to centroids.
  m_amr->interpToCentroids(a_conductivity, m_fluidRealm, m_plasmaPhase);
}

void
ItoPlasmaStepper::computeCurrentDensity(EBAMRCellData& a_J) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeCurrentDensity(EBAMRCellData)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeCurrentDensity(EBAMRCellData)" << endl;
  }

  CH_assert(a_J[0]->nComp() == SpaceDim);

  // TLDR: a_J is defined over the fluid Realm but the computation takes place on the particle Realm.
  //       If the Realms are different we compute on a scratch storage instead

  this->computeConductivityCell(m_fluidScratch1);
  DataOps::copy(a_J, m_electricFieldFluid);

  DataOps::multiplyScalar(a_J, m_fluidScratch1);
}

Real
ItoPlasmaStepper::computeRelaxationTime() noexcept
{
  CH_TIME("ItoPlasmaStepper::computeRelaxationTime()");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeRelaxationTime()" << endl;
  }

  // TLDR: We compute eps0/conductivity directly.

  EBAMRCellData conductivity;
  EBAMRCellData relaxTime;

  m_amr->allocate(conductivity, m_fluidRealm, m_plasmaPhase, 1);
  m_amr->allocate(relaxTime, m_fluidRealm, m_plasmaPhase, 1);

  this->computeConductivityCell(conductivity);

  DataOps::setValue(relaxTime, Units::eps0);
  DataOps::divideFallback(relaxTime, conductivity, std::numeric_limits<Real>::max());

  m_amr->conservativeAverage(relaxTime, m_fluidRealm, m_plasmaPhase);

  Real min = 0.0;
  Real max = 0.0;

  DataOps::getMaxMinNorm(max, min, relaxTime);

  return min;
}

bool
ItoPlasmaStepper::solvePoisson() noexcept
{
  CH_TIME("ItoPlasmaStepper::solvePoisson()");
  if (m_verbosity > 5) {
    pout() << m_name + "::solvePoisson()" << endl;
  }

  // Compute the space charge density.
  this->computeSpaceChargeDensity();

  // Solve the Poisson equation and compute the cell-centered electric field.
  MFAMRCellData& phi   = m_fieldSolver->getPotential();
  MFAMRCellData& rho   = m_fieldSolver->getRho();
  EBAMRIVData&   sigma = m_sigmaSolver->getPhi();

  const bool converged = m_fieldSolver->solve(phi, rho, sigma, false);

  m_fieldSolver->computeElectricField();

  // Copy the electric field to appropriate data holders and perform center-to-centroid
  // interpolation.
  EBAMRCellData E;
  m_amr->allocatePointer(E);
  m_amr->alias(E, m_plasmaPhase, m_fieldSolver->getElectricField());

  // Fluid realm
  m_electricFieldFluid.copy(E);
  m_amr->conservativeAverage(m_electricFieldFluid, m_fluidRealm, m_plasmaPhase);
  m_amr->interpGhostPwl(m_electricFieldFluid, m_fluidRealm, m_plasmaPhase);
  m_amr->interpToCentroids(m_electricFieldFluid, m_fluidRealm, m_plasmaPhase);

  // Particle realm
  m_electricFieldParticle.copy(E);
  m_amr->conservativeAverage(m_electricFieldParticle, m_particleRealm, m_plasmaPhase);
  m_amr->interpGhostPwl(m_electricFieldParticle, m_particleRealm, m_plasmaPhase);
  m_amr->interpToCentroids(m_electricFieldParticle, m_particleRealm, m_plasmaPhase);

  return converged;
}

void
ItoPlasmaStepper::intersectParticles(const SpeciesSubset  a_speciesSubset,
                                     const EBIntersection a_intersectionAlg,
                                     const bool           a_delete) noexcept
{
  CH_TIME("ItoPlasmaStepper::intersectParticles(SpeciesSubset, EBRepresentation, bool)");
  if (m_verbosity > 5) {
    pout() << m_name + "::intersectParticles(SpeciesSubset, EBRepresentation, bool)" << endl;
  }

  this->intersectParticles(a_speciesSubset,
                           ItoSolver::WhichContainer::Bulk,
                           ItoSolver::WhichContainer::EB,
                           ItoSolver::WhichContainer::Domain,
                           a_intersectionAlg,
                           a_delete);
}

void
ItoPlasmaStepper::intersectParticles(const SpeciesSubset             a_speciesSubset,
                                     const ItoSolver::WhichContainer a_containerBulk,
                                     const ItoSolver::WhichContainer a_containerEB,
                                     const ItoSolver::WhichContainer a_containerDomain,
                                     const EBIntersection            a_intersectionAlg,
                                     const bool                      a_delete) noexcept
{
  CH_TIME("ItoPlasmaStepper::intersectParticles(SpeciesSubset, Containerx3, EBRepresentation, bool)");
  if (m_verbosity > 5) {
    pout() << m_name + "::intersectParticles(SpeciesSubset, Containerx3, EBRepresentation, bool)" << endl;
  }

  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>&        solver  = solverIt();
    const RefCountedPtr<ItoSpecies>& species = solver->getSpecies();

    const bool mobile    = solver->isMobile();
    const bool diffusive = solver->isDiffusive();
    const bool charged   = (species->getChargeNumber() != 0);

    switch (a_speciesSubset) {
    case SpeciesSubset::All: {
      solver->intersectParticles(a_containerBulk, a_containerEB, a_containerDomain, a_intersectionAlg, a_delete);

      break;
    }
    case SpeciesSubset::AllMobile: {
      if (mobile) {
        solver->intersectParticles(a_containerBulk, a_containerEB, a_containerDomain, a_intersectionAlg, a_delete);
      }

      break;
    }
    case SpeciesSubset::AllDiffusive: {
      if (diffusive) {
        solver->intersectParticles(a_containerBulk, a_containerEB, a_containerDomain, a_intersectionAlg, a_delete);
      }

      break;
    }
    case SpeciesSubset::AllMobileOrDiffusive: {
      if (mobile || diffusive) {
        solver->intersectParticles(a_containerBulk, a_containerEB, a_containerDomain, a_intersectionAlg, a_delete);
      }

      break;
    }
    case SpeciesSubset::AllMobileAndDiffusive: {
      if (mobile && diffusive) {
        solver->intersectParticles(a_containerBulk, a_containerEB, a_containerDomain, a_intersectionAlg, a_delete);
      }

      break;
    }
    case SpeciesSubset::Charged: {
      if (charged) {
        solver->intersectParticles(a_containerBulk, a_containerEB, a_containerDomain, a_intersectionAlg, a_delete);
      }

      break;
    }
    case SpeciesSubset::ChargedMobile: {
      if (charged && mobile) {
        solver->intersectParticles(a_containerBulk, a_containerEB, a_containerDomain, a_intersectionAlg, a_delete);
      }

      break;
    }
    case SpeciesSubset::ChargedDiffusive: {
      if (charged && diffusive) {
        solver->intersectParticles(a_containerBulk, a_containerEB, a_containerDomain, a_intersectionAlg, a_delete);
      }

      break;
    }
    case SpeciesSubset::ChargedMobileOrDiffusive: {
      if (charged && (mobile || diffusive)) {
        solver->intersectParticles(a_containerBulk, a_containerEB, a_containerDomain, a_intersectionAlg, a_delete);
      }

      break;
    }
    case SpeciesSubset::ChargedMobileAndDiffusive: {
      if (charged && (mobile && diffusive)) {
        solver->intersectParticles(a_containerBulk, a_containerEB, a_containerDomain, a_intersectionAlg, a_delete);
      }

      break;
    }
    case SpeciesSubset::Stationary: {
      if (!mobile && !diffusive) {
        solver->intersectParticles(a_containerBulk, a_containerEB, a_containerDomain, a_intersectionAlg, a_delete);
      }

      break;
    }
    default: {
      MayDay::Abort("ItoPlasmaStepper::intersectParticles - logic bust");

      break;
    }
    }
  }
}

void
ItoPlasmaStepper::removeCoveredParticles(const SpeciesSubset    a_speciesSubset,
                                         const EBRepresentation a_representation,
                                         const Real             a_tolerance) noexcept
{
  CH_TIME("ItoPlasmaStepper::removeCoveredParticles(SpeciesSubset, EBRepresentation, Real)");
  if (m_verbosity > 5) {
    pout() << m_name + "::removeCoveredParticles(SpeciesSubset, EBRepresentation, Real)" << endl;
  }

  this->removeCoveredParticles(a_speciesSubset, ItoSolver::WhichContainer::Bulk, a_representation, a_tolerance);
}

void
ItoPlasmaStepper::removeCoveredParticles(const SpeciesSubset             a_which,
                                         const ItoSolver::WhichContainer a_container,
                                         const EBRepresentation          a_representation,
                                         const Real                      a_tolerance) noexcept
{
  CH_TIME("ItoPlasmaStepper::removeCoveredParticles(SpeciesSubset, container, EBRepresentation, tolerance)");
  if (m_verbosity > 5) {
    pout() << m_name + "::removeCoveredParticles(SpeciesSubset, container, EBRepresentation, tolerance)" << endl;
  }

  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>&        solver  = solverIt();
    const RefCountedPtr<ItoSpecies>& species = solver->getSpecies();

    const bool mobile    = solver->isMobile();
    const bool diffusive = solver->isDiffusive();
    const bool charged   = (species->getChargeNumber() != 0);

    switch (a_which) {
    case SpeciesSubset::All: {
      solver->removeCoveredParticles(a_container, a_representation, a_tolerance);

      break;
    }
    case SpeciesSubset::AllMobile: {
      if (mobile) {
        solver->removeCoveredParticles(a_container, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::AllDiffusive: {
      if (diffusive) {
        solver->removeCoveredParticles(a_container, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::AllMobileOrDiffusive: {
      if (mobile || diffusive) {
        solver->removeCoveredParticles(a_container, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::AllMobileAndDiffusive: {
      if (mobile && diffusive) {
        solver->removeCoveredParticles(a_container, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::Charged: {
      if (charged) {
        solver->removeCoveredParticles(a_container, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::ChargedMobile: {
      if (charged && mobile) {
        solver->removeCoveredParticles(a_container, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::ChargedDiffusive: {
      if (charged && diffusive) {
        solver->removeCoveredParticles(a_container, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::ChargedMobileOrDiffusive: {
      if (charged && (mobile || diffusive)) {
        solver->removeCoveredParticles(a_container, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::ChargedMobileAndDiffusive: {
      if (charged && (mobile && diffusive)) {
        solver->removeCoveredParticles(a_container, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::Stationary: {
      if (!mobile && !diffusive) {
        solver->removeCoveredParticles(a_container, a_representation, a_tolerance);
      }

      break;
    }
    default: {
      MayDay::Abort("ItoPlasmaStepper::removeCoveredParticles - logic bust");

      break;
    }
    }
  }
}

void
ItoPlasmaStepper::transferCoveredParticles(const SpeciesSubset    a_speciesSubset,
                                           const EBRepresentation a_representation,
                                           const Real             a_tolerance) noexcept
{
  CH_TIME("ItoPlasmaStepper::transferCoveredParticles(SpeciesSubset, EBRepresentation, Real)");
  if (m_verbosity > 5) {
    pout() << m_name + "::transferCoveredParticles(SpeciesSubset, EBRepresentation, Real)" << endl;
  }

  this->transferCoveredParticles(a_speciesSubset,
                                 ItoSolver::WhichContainer::Bulk,
                                 ItoSolver::WhichContainer::Covered,
                                 a_representation,
                                 a_tolerance);
}

void
ItoPlasmaStepper::transferCoveredParticles(const SpeciesSubset             a_speciesSubset,
                                           const ItoSolver::WhichContainer a_containerFrom,
                                           const ItoSolver::WhichContainer a_containerTo,
                                           const EBRepresentation          a_representation,
                                           const Real                      a_tolerance) noexcept
{
  CH_TIME("ItoPlasmaStepper::transferCoveredParticles(SpeciesSubset, Containerx2, EBRepresentation, Real)");
  if (m_verbosity > 5) {
    pout() << m_name + "::transferCoveredParticles(SpeciesSubset, Containerx2, EBRepresentation, Real)" << endl;
  }

  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>&        solver  = solverIt();
    const RefCountedPtr<ItoSpecies>& species = solver->getSpecies();

    const bool mobile    = solver->isMobile();
    const bool diffusive = solver->isDiffusive();
    const bool charged   = (species->getChargeNumber() != 0);

    switch (a_speciesSubset) {
    case SpeciesSubset::All: {
      solver->transferCoveredParticles(a_containerFrom, a_containerTo, a_representation, a_tolerance);

      break;
    }
    case SpeciesSubset::AllMobile: {
      if (mobile) {
        solver->transferCoveredParticles(a_containerFrom, a_containerTo, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::AllDiffusive: {
      if (diffusive) {
        solver->transferCoveredParticles(a_containerFrom, a_containerTo, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::AllMobileOrDiffusive: {
      if (mobile || diffusive) {
        solver->transferCoveredParticles(a_containerFrom, a_containerTo, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::AllMobileAndDiffusive: {
      if (mobile && diffusive) {
        solver->transferCoveredParticles(a_containerFrom, a_containerTo, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::Charged: {
      if (charged) {
        solver->transferCoveredParticles(a_containerFrom, a_containerTo, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::ChargedMobile: {
      if (charged && mobile) {
        solver->transferCoveredParticles(a_containerFrom, a_containerTo, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::ChargedDiffusive: {
      if (charged && diffusive) {
        solver->transferCoveredParticles(a_containerFrom, a_containerTo, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::ChargedMobileOrDiffusive: {
      if (charged && (mobile || diffusive)) {
        solver->transferCoveredParticles(a_containerFrom, a_containerTo, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::ChargedMobileAndDiffusive: {
      if (charged && (mobile && diffusive)) {
        solver->transferCoveredParticles(a_containerFrom, a_containerTo, a_representation, a_tolerance);
      }

      break;
    }
    case SpeciesSubset::Stationary: {
      if (!mobile && !diffusive) {
        solver->transferCoveredParticles(a_containerFrom, a_containerTo, a_representation, a_tolerance);
      }

      break;
    }
    default: {
      MayDay::Abort("ItoPlasmaStepper::transferCoveredParticles - logic bust");

      break;
    }
    }
  }
}

void
ItoPlasmaStepper::remapParticles(const SpeciesSubset a_speciesSubset) noexcept
{
  CH_TIME("ItoPlasmaStepper::remapParticles(SpeciesSubset)");
  if (m_verbosity > 5) {
    pout() << m_name + "::remapParticles(SpeciesSubset)" << endl;
  }

  this->remapParticles(a_speciesSubset, ItoSolver::WhichContainer::Bulk);
}

void
ItoPlasmaStepper::remapParticles(const SpeciesSubset             a_speciesSubset,
                                 const ItoSolver::WhichContainer a_container) noexcept
{
  CH_TIME("ItoPlasmaStepper::remapParticles(SpeciesSubset, WhichContainer)");
  if (m_verbosity > 5) {
    pout() << m_name + "::remapParticles(SpeciesSubset, WhichContainer)" << endl;
  }

  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>&        solver  = solverIt();
    const RefCountedPtr<ItoSpecies>& species = solver->getSpecies();

    const int idx = solverIt.index();

    const bool mobile    = solver->isMobile();
    const bool diffusive = solver->isDiffusive();
    const bool charged   = (species->getChargeNumber() != 0);

    switch (a_speciesSubset) {
    case SpeciesSubset::All: {
      solver->remap(a_container);

      break;
    }
    case SpeciesSubset::AllMobile: {
      if (mobile) {
        solver->remap(a_container);
      }

      break;
    }
    case SpeciesSubset::AllDiffusive: {
      if (diffusive) {
        solver->remap(a_container);
      }

      break;
    }
    case SpeciesSubset::AllMobileOrDiffusive: {
      if (mobile || diffusive) {
        solver->remap(a_container);
      }

      break;
    }
    case SpeciesSubset::AllMobileAndDiffusive: {
      if (mobile && diffusive) {
        solver->remap(a_container);
      }

      break;
    }
    case SpeciesSubset::Charged: {
      if (charged) {
        solver->remap(a_container);
      }

      break;
    }
    case SpeciesSubset::ChargedMobile: {
      if (charged && mobile) {
        solver->remap(a_container);
      }

      break;
    }
    case SpeciesSubset::ChargedDiffusive: {
      if (charged && diffusive) {
        solver->remap(a_container);
      }

      break;
    }
    case SpeciesSubset::ChargedMobileOrDiffusive: {
      if (charged && (mobile || diffusive)) {
        solver->remap(a_container);
      }

      break;
    }
    case SpeciesSubset::ChargedMobileAndDiffusive: {
      if (charged && (mobile && diffusive)) {
        solver->remap(a_container);
      }

      break;
    }
    case SpeciesSubset::Stationary: {
      if (!mobile && !diffusive) {
        solver->remap(a_container);
      }

      break;
    }
    default: {
      MayDay::Abort("ItoPlasmaStepper::remapParticles - logic bust");

      break;
    }
    }
  }
}

void
ItoPlasmaStepper::depositParticles(const SpeciesSubset a_speciesSubset) noexcept
{
  CH_TIME("ItoPlasmaStepper::depositParticles(SpeciesSubset)");
  if (m_verbosity > 5) {
    pout() << m_name + "::depositParticles(SpeciesSubset)" << endl;
  }

  this->depositParticles(a_speciesSubset, ItoSolver::WhichContainer::Bulk);
}

void
ItoPlasmaStepper::depositParticles(const SpeciesSubset             a_speciesSubset,
                                   const ItoSolver::WhichContainer a_container) noexcept
{
  CH_TIME("ItoPlasmaStepper::depositParticles(SpeciesSubset)");
  if (m_verbosity > 5) {
    pout() << m_name + "::depositParticles(SpeciesSubset)" << endl;
  }

  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>&        solver  = solverIt();
    const RefCountedPtr<ItoSpecies>& species = solver->getSpecies();

    const int idx = solverIt.index();

    const bool mobile    = solver->isMobile();
    const bool diffusive = solver->isDiffusive();
    const bool charged   = (species->getChargeNumber() != 0);

    switch (a_speciesSubset) {
    case SpeciesSubset::All: {
      solver->depositParticles(a_container);

      break;
    }
    case SpeciesSubset::AllMobile: {
      if (mobile) {
        solver->depositParticles(a_container);
      }

      break;
    }
    case SpeciesSubset::AllDiffusive: {
      if (diffusive) {
        solver->depositParticles(a_container);
      }

      break;
    }
    case SpeciesSubset::AllMobileOrDiffusive: {
      if (mobile || diffusive) {
        solver->depositParticles(a_container);
      }

      break;
    }
    case SpeciesSubset::AllMobileAndDiffusive: {
      if (mobile && diffusive) {
        solver->depositParticles(a_container);
      }

      break;
    }
    case SpeciesSubset::Charged: {
      if (charged) {
        solver->depositParticles(a_container);
      }

      break;
    }
    case SpeciesSubset::ChargedMobile: {
      if (charged && mobile) {
        solver->depositParticles(a_container);
      }

      break;
    }
    case SpeciesSubset::ChargedDiffusive: {
      if (charged && diffusive) {
        solver->depositParticles(a_container);
      }

      break;
    }
    case SpeciesSubset::ChargedMobileOrDiffusive: {
      if (charged && (mobile || diffusive)) {
        solver->depositParticles(a_container);
      }

      break;
    }
    case SpeciesSubset::ChargedMobileAndDiffusive: {
      if (charged && (mobile && diffusive)) {
        solver->depositParticles(a_container);
      }

      break;
    }
    case SpeciesSubset::Stationary: {
      if (!mobile && !diffusive) {
        solver->depositParticles(a_container);
      }

      break;
    }
    default: {
      MayDay::Abort("ItoPlasmaStepper::depositParticles - logic bust");

      break;
    }
    }
  }
}

void
ItoPlasmaStepper::setItoVelocityFunctions() noexcept
{
  CH_TIME("ItoPlasmaStepper::setItoVelocityFunctions");
  if (m_verbosity > 5) {
    pout() << m_name + "::setItoVelocityFunctions" << endl;
  }

  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>&        solver  = solverIt();
    const RefCountedPtr<ItoSpecies>& species = solver->getSpecies();

    if (solver->isMobile()) {
      EBAMRCellData& velocityFunction = solver->getVelocityFunction();
      velocityFunction.copy(m_electricFieldParticle);

      const int Z = species->getChargeNumber();

      if (Z < 0) {
        DataOps::scale(velocityFunction, -1.0);
      }
      else if (Z == 0) {
        MayDay::Warning("ItoPlasmaStepper::setItoVelocityFunctions -- what to do about sign for neutral species?");
      }

      // Coarsen and update ghost cells.
      m_amr->conservativeAverage(velocityFunction, m_particleRealm, m_plasmaPhase);
      m_amr->interpGhostMG(velocityFunction, m_particleRealm, m_plasmaPhase);
    }
  }
}

void
ItoPlasmaStepper::computeItoVelocities() noexcept
{
  CH_TIME("ItoPlasmaStepper::computeItoVelocities()");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeItoVelocities()" << endl;
  }

  // Set the ItoSolver velocity functions.
  this->setItoVelocityFunctions();

  // Compute mobilities.
  this->computeItoMobilitiesLFA();

  // Interpolate velocity function to particle position so that particles get velocity v = +/- mu*E
  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    solverIt()->interpolateVelocities();
  }
}

void
ItoPlasmaStepper::computeItoDiffusion() noexcept
{
  CH_TIME("ItoPlasmaStepper::computeItoDiffusion()");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeItoDiffusion()" << endl;
  }

  this->computeItoDiffusionLFA();
}

void
ItoPlasmaStepper::computeItoMobilitiesLFA() noexcept
{
  CH_TIME("ItoPlasmaStepper::computeItoMobilitiesLFA()");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeItoMobilitiesLFA()" << endl;
  }

  Vector<EBAMRCellData*> meshMobilities = m_ito->getMobilityFunctions();

  this->computeItoMobilitiesLFA(meshMobilities, m_electricFieldFluid, m_time);
}

void
ItoPlasmaStepper::computeItoMobilitiesLFA(Vector<EBAMRCellData*>& a_meshMobilities,
                                          const EBAMRCellData&    a_electricField,
                                          const Real              a_time) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeItoMobilitiesLFA(mobilities, E, time)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeItoMobilitiesLFA(mobilities, E, time)" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();

  CH_assert(a_electricField.getRealm() == m_fluidRealm);
  CH_assert(a_meshMobilities.size() == numPlasmaSpecies);

  // The mesh mobilities belong on the particle realm (they are the ItoSolver mobilities) but we need to run
  // the computation on the fluid realm. So, create some transient storage for that.
  Vector<EBAMRCellData> fluidScratchMobilities(numPlasmaSpecies);
  for (int i = 0; i < numPlasmaSpecies; i++) {
    m_amr->allocate(fluidScratchMobilities[i], m_fluidRealm, m_plasmaPhase, 1);

    CH_assert(a_meshMobilities[i]->getRealm() == m_particleRealm);
  }

  // Now run the computation on the fluid realm, computing the mobilities into fluidScratchMobilities
  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++) {
    Vector<LevelData<EBCellFAB>*> mobilities(numPlasmaSpecies);

    for (int i = 0; i < numPlasmaSpecies; i++) {
      mobilities[i] = &(*(fluidScratchMobilities[i])[lvl]);
    }

    // Run the level computation, which will fill mobilities aka fluidScratchMobilities.
    this->computeItoMobilitiesLFA(mobilities, *a_electricField[lvl], lvl, a_time);
  }

  // Copy fluid realm data into particle realm and interpolate mobilities to the particle position.
  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>& solver = solverIt();

    if (solver->isMobile()) {
      const int idx = solverIt.index();

      a_meshMobilities[idx]->copy(fluidScratchMobilities[idx]);

      m_amr->conservativeAverage(*a_meshMobilities[idx], m_particleRealm, m_plasmaPhase);
      m_amr->interpGhostMG(*a_meshMobilities[idx], m_particleRealm, m_plasmaPhase);

      solver->interpolateMobilities();
    }
  }
}

void
ItoPlasmaStepper::computeItoMobilitiesLFA(Vector<LevelData<EBCellFAB>*>& a_meshMobilities,
                                          const LevelData<EBCellFAB>&    a_electricField,
                                          const int                      a_level,
                                          const Real                     a_time) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeItoMobilitiesLFA(mobilities, E, level, time)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeItoMobilitiesLFA(mobilities, E, level, time)" << endl;
  }

  const DisjointBoxLayout& dbl = m_amr->getGrids(m_fluidRealm)[a_level];

  for (DataIterator dit(dbl); dit.ok(); ++dit) {
    const EBCellFAB& E       = a_electricField[dit()];
    const Box        cellBox = dbl[dit()];

    Vector<EBCellFAB*> meshMobilities;
    for (int i = 0; i < a_meshMobilities.size(); i++) {
      meshMobilities.push_back(&((*a_meshMobilities[i])[dit()]));
    }

    this->computeItoMobilitiesLFA(meshMobilities, E, a_level, dit(), cellBox, a_time);
  }
}

void
ItoPlasmaStepper::computeItoMobilitiesLFA(Vector<EBCellFAB*>& a_meshMobilities,
                                          const EBCellFAB&    a_electricField,
                                          const int           a_level,
                                          const DataIndex     a_dit,
                                          const Box           a_box,
                                          const Real          a_time) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeItoMobilitiesLFA(meshMobilities, E, level, dit, box, time)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeItoMobilitiesLFA(meshMobilities, E, level, dit, box, time)" << endl;
  }

  // TLDR: We go through each and every cell and call the physics interface. This includes cells covered by a finer grid
  //       but data is coarsened later anyways.

  const Real     dx      = m_amr->getDx()[a_level];
  const RealVect probLo  = m_amr->getProbLo();
  const EBISBox& ebisbox = m_amr->getEBISLayout(m_fluidRealm, m_plasmaPhase)[a_level][a_dit];

  // Handle to regular data.
  const FArrayBox&   electricFieldReg = a_electricField.getFArrayBox();
  Vector<FArrayBox*> meshMobilitiesReg;
  for (int i = 0; i < a_meshMobilities.size(); i++) {
    meshMobilitiesReg.push_back(&(a_meshMobilities[i]->getFArrayBox()));
  }

  // Regular kernel
  auto regularKernel = [&](const IntVect& iv) -> void {
    const RealVect pos = m_amr->getProbLo() + dx * (RealVect(iv) + 0.5 * RealVect::Unit);
    const RealVect E   = RealVect(D_DECL(electricFieldReg(iv, 0), electricFieldReg(iv, 1), electricFieldReg(iv, 2)));

    // Call ito_physics and compute mobilities for each particle species
    const Vector<Real> mobilities = m_physics->computeItoMobilities(a_time, pos, E);

    // Put mobilities in appropriate data holder
    for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
      const int idx = solverIt.index();

      (*meshMobilitiesReg[idx])(iv, 0) = mobilities[idx];
    }
  };

  // Irregular kernel.
  auto irregularKernel = [&](const VolIndex& vof) -> void {
    const RealVect e   = RealVect(D_DECL(a_electricField(vof, 0), a_electricField(vof, 1), a_electricField(vof, 2)));
    const RealVect pos = probLo + Location::position(Location::Cell::Centroid, vof, ebisbox, dx);

    // Compute diffusion
    const Vector<Real> mobilities = m_physics->computeItoMobilities(a_time, pos, e);

    // Put diffusion in the appropriate place.
    for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
      const int idx = solverIt.index();

      (*a_meshMobilities[idx])(vof, 0) = mobilities[idx];
    }
  };

  VoFIterator& vofit = (*m_amr->getVofIterator(m_fluidRealm, m_plasmaPhase)[a_level])[a_dit];

  // Run the kernels.
  BoxLoops::loop(a_box, regularKernel);
  BoxLoops::loop(vofit, irregularKernel);

  // Covered is bogus.
  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    const int idx = solverIt.index();
    a_meshMobilities[idx]->setCoveredCellVal(0.0, 0);
  }
}

void
ItoPlasmaStepper::computeItoMobilitiesLEA() noexcept
{
  CH_TIME("ItoPlasmaStepper::computeItoMobilitiesLEA()");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeItoMobilitiesLEA()" << endl;
  }

  // This is really simple because the solvers do this directly...
  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    solverIt()->updateMobilities();
  }
}

void
ItoPlasmaStepper::computeItoDiffusionLFA() noexcept
{
  CH_TIME("ItoPlasmaStepper::computeItoDiffusionLFA()");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeItoDiffusionLFA()" << endl;
  }

  Vector<EBAMRCellData*> diffusionCoefficients = m_ito->getDiffusionFunctions();

  this->computeItoDiffusionLFA(diffusionCoefficients, m_electricFieldFluid, m_time);
}

void
ItoPlasmaStepper::computeItoDiffusionLFA(Vector<EBAMRCellData*>& a_diffusionCoefficients,
                                         const EBAMRCellData&    a_electricField,
                                         const Real              a_time) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeItoDiffusionLFA(Vector<EBAMRCellData*>, EBAMRCellData, Real)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeItoDiffusionLFA(Vector<EBAMRCellData*>, EBAMRCellData, Real)" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();

  CH_assert(a_electricField.getRealm() == m_fluidRealm);
  CH_assert(a_diffusionCoefficients.size() == numPlasmaSpecies);
  CH_assert(a_densities.size() == numPlasmaSpecies);

  // The mesh diffusion coefficients belong on the particle realm (they are the ItoSolver diffusion coefficients) but we need to run
  // the computation on the fluid realm. So, create some transient storage for that.
  Vector<EBAMRCellData> fluidScratchDiffusion(numPlasmaSpecies);
  for (int i = 0; i < numPlasmaSpecies; i++) {
    m_amr->allocate(fluidScratchDiffusion[i], m_fluidRealm, m_plasmaPhase, 1);

    CH_assert(a_diffusionCoefficients[i].getRealm() == m_particleRealm);
  }

  // Compute mesh-based diffusion coefficients on the fluid realm.
  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++) {
    Vector<LevelData<EBCellFAB>*> diffusionCoefficients(numPlasmaSpecies);

    for (int i = 0; i < numPlasmaSpecies; i++) {
      diffusionCoefficients[i] = &(*(fluidScratchDiffusion[i])[lvl]);
    }

    this->computeItoDiffusionLFA(diffusionCoefficients, *a_electricField[lvl], lvl, a_time);
  }

  // Copy the fluid realm data over to the particle realm data and then coarsen and interpolate diffusion coefficients
  // to particle positions.
  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>& solver = solverIt();

    if (solver->isDiffusive()) {
      const int idx = solverIt.index();

      a_diffusionCoefficients[idx]->copy(fluidScratchDiffusion[idx]);

      m_amr->conservativeAverage(*a_diffusionCoefficients[idx], m_particleRealm, m_plasmaPhase);
      m_amr->interpGhostMG(*a_diffusionCoefficients[idx], m_particleRealm, m_plasmaPhase);

      solver->interpolateDiffusion();
    }
  }
}

void
ItoPlasmaStepper::computeItoDiffusionLFA(Vector<LevelData<EBCellFAB>*>& a_diffusionCoefficients,
                                         const LevelData<EBCellFAB>&    a_electricField,
                                         const int                      a_level,
                                         const Real                     a_time) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeItoDiffusionLFA(Vector<LD<EBCellFAB>*>, LD<EBCellFAB>, int, Real)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeItoDiffusionLFA(Vector<LD<EBCellFAB>*>, LD<EBCellFAB>, int, Real)" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();

  CH_assert(a_diffusionCoefficients.size() == numPlasmaSpecies);

  const DisjointBoxLayout& dbl = m_amr->getGrids(m_fluidRealm)[a_level];

  for (DataIterator dit(dbl); dit.ok(); ++dit) {

    // Populate the patch data.
    Vector<EBCellFAB*> diffusionCoefficients(numPlasmaSpecies);

    for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
      const int idx = solverIt.index();

      if (solverIt()->isDiffusive()) {
        diffusionCoefficients[idx] = &(*a_diffusionCoefficients[idx])[dit()];
      }
      else {
        diffusionCoefficients[idx] = nullptr;
      }
    }

    this->computeItoDiffusionLFA(diffusionCoefficients, a_electricField[dit()], a_level, dit(), dbl[dit()], a_time);
  }
}

void
ItoPlasmaStepper::computeItoDiffusionLFA(Vector<EBCellFAB*>& a_diffusionCoefficients,
                                         const EBCellFAB&    a_electricField,
                                         const int           a_level,
                                         const DataIndex     a_dit,
                                         const Box           a_box,
                                         const Real          a_time) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeItoDiffusionLFA(Patch)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeItoDiffusionLFA(Patch)" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();

  CH_assert(a_diffusionCoefficients.size() == numPlasmaSpecies);
  CH_assert(a_electricField.nComp() == SpaceDim);

  // Geometric information that we need.
  const Real     dx      = m_amr->getDx()[a_level];
  const RealVect probLo  = m_amr->getProbLo();
  const EBISBox& ebisbox = m_amr->getEBISLayout(m_fluidRealm, m_plasmaPhase)[a_level][a_dit];

  // Definition of single-valued aka regular data.
  const FArrayBox& electricFieldReg = a_electricField.getFArrayBox();

  Vector<FArrayBox*> diffCoReg(numPlasmaSpecies);

  for (int i = 0; i < numPlasmaSpecies; i++) {
    diffCoReg[i] = &(a_diffusionCoefficients[i]->getFArrayBox());
  }

  // Regular kernel definition.
  auto regularKernel = [&](const IntVect& iv) -> void {
    const RealVect pos = probLo + dx * (RealVect(iv) + 0.5 * RealVect::Unit);
    const RealVect E   = RealVect(D_DECL(electricFieldReg(iv, 0), electricFieldReg(iv, 1), electricFieldReg(iv, 2)));

    // Compute diffusion coefficients.
    const Vector<Real> diffusion = m_physics->computeItoDiffusion(a_time, pos, E);

    // Put diffusion coefficients into correct storage.
    for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
      RefCountedPtr<ItoSolver>& solver = solverIt();

      if (solver->isDiffusive()) {
        const int idx = solverIt.index();

        (*diffCoReg[idx])(iv, 0) = diffusion[idx];
      }
    }
  };

  // Irregular kernel.
  auto irregularKernel = [&](const VolIndex& vof) -> void {
    const RealVect E   = RealVect(D_DECL(a_electricField(vof, 0), a_electricField(vof, 1), a_electricField(vof, 2)));
    const RealVect pos = probLo + Location::position(Location::Cell::Centroid, vof, ebisbox, dx);

    // Compute diffusion coefficients.
    const Vector<Real> diffusion = m_physics->computeItoDiffusion(a_time, pos, E);

    // Put diffusion in the appropriate place.
    for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
      if (solverIt()->isDiffusive()) {
        const int idx = solverIt.index();

        (*a_diffusionCoefficients[idx])(vof, 0) = diffusion[idx];
      }
    }
  };

  // Run kernels.
  VoFIterator& vofit = (*m_amr->getVofIterator(m_fluidRealm, m_plasmaPhase)[a_level])[a_dit];

  BoxLoops::loop(a_box, regularKernel);
  BoxLoops::loop(vofit, irregularKernel);

  // Covered is bogus.
  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    if (solverIt()->isDiffusive()) {
      const int idx = solverIt.index();

      a_diffusionCoefficients[idx]->setCoveredCellVal(0.0, 0);
    }
  }
}

void
ItoPlasmaStepper::computeItoDiffusionLEA() noexcept
{
  CH_TIME("ItoPlasmaStepper::computeItoDiffusionLEA()");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeItoDiffusionLEA()" << endl;
  }

  // This is really simple because the solvers do this directly... No monkeying with interpolations or anything.
  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    solverIt()->updateDiffusion();
  }
}

void
ItoPlasmaStepper::computeReactiveParticlesPerCell(EBAMRCellData& a_ppc) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeReactiveParticlesPerCell(EBAMRCellData)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeReactiveParticlesPerCell(EBAMRCellData)" << endl;
  }

  CH_assert(a_ppc.getRealm() == m_particleRealm);

  DataOps::setValue(a_ppc, 0.0);

  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++) {
    this->computeReactiveParticlesPerCell(*a_ppc[lvl], lvl);
  }
}

void
ItoPlasmaStepper::computeReactiveParticlesPerCell(LevelData<EBCellFAB>& a_ppc, const int a_level) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeReactiveParticlesPerCell(LD<EBCellFAB>, int)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeReactiveParticlesPerCell(LD<EBCellFAB>, int)" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();

  CH_assert(a_ppc.nComp() == numPlasmaSpecies);

  const DisjointBoxLayout& dbl   = m_amr->getGrids(m_particleRealm)[a_level];
  const EBISLayout&        ebisl = m_amr->getEBISLayout(m_particleRealm, m_plasmaPhase)[a_level];

  for (DataIterator dit(dbl); dit.ok(); ++dit) {
    const Box      box     = dbl[dit()];
    const EBISBox& ebisbox = ebisl[dit()];

    this->computeReactiveParticlesPerCell(a_ppc[dit()], a_level, dit(), box, ebisbox);
  }
}

void
ItoPlasmaStepper::computeReactiveParticlesPerCell(EBCellFAB&      a_ppc,
                                                  const int       a_level,
                                                  const DataIndex a_dit,
                                                  const Box       a_box,
                                                  const EBISBox&  a_ebisbox) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeReactiveParticlesPerCell(EBCellFAB, int, DataIndex, Box, EBISBox)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeReactiveParticlesPerCell(EBCellFAB, int, DataIndex, Box, EBISBox)" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();

  CH_assert(a_ppc.nComp() == numPlasmaSpecies);

  // TLDR: We go through each solver and add the number of PHYSICAL particles per cell to a_ppc.

  FArrayBox& ppcRegular = a_ppc.getFArrayBox();

  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>& solver = solverIt();
    const int                 idx    = solverIt.index();

    // Get the cell-sorted particles. This will issue an error if the user has not
    // sorted the particles by cell before calling this routine.
    const ParticleContainer<ItoParticle>& particles     = solver->getParticles(ItoSolver::WhichContainer::Bulk);
    const BinFab<ItoParticle>&            cellParticles = particles.getCellParticles(a_level, a_dit);

    // Regular cells kernel.
    auto regularKernel = [&](const IntVect& iv) -> void {
      Real num = 0.0;

      if (a_ebisbox.isRegular(iv)) {
        for (ListIterator<ItoParticle> lit(cellParticles(iv, 0)); lit.ok(); ++lit) {
          num += lit().weight();
        }
      }

      ppcRegular(iv, idx) = num;
    };

    // Irregular kernel -- note that only particles that lie inside the domain get to react.
    auto irregularKernel = [&](const VolIndex& vof) -> void {
      const IntVect  iv         = vof.gridIndex();
      const RealVect normal     = a_ebisbox.normal(vof);
      const RealVect ebCentroid = a_ebisbox.bndryCentroid(vof);

      Real num = 0.0;

      for (ListIterator<ItoParticle> lit(cellParticles(iv, 0)); lit.ok(); ++lit) {
        const RealVect& pos = lit().position();

        if ((pos - ebCentroid).dotProduct(normal) >= 0.0) {
          num += lit().weight();
        }
      }

      ppcRegular(iv, idx) = num;
    };

    // Run the kernels.
    VoFIterator& vofit = (*m_amr->getVofIterator(m_particleRealm, m_plasmaPhase)[a_level])[a_dit];

    BoxLoops::loop(a_box, regularKernel);
    BoxLoops::loop(vofit, irregularKernel);
  }
}

void
ItoPlasmaStepper::computeReactiveMeanEnergiesPerCell(EBAMRCellData& a_meanEnergies) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeReactiveMaeanEnergiesPerCell(EBAMRCellData)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeReactiveMaeanEnergiesPerCell(EBAMRCellData)" << endl;
  }

  CH_assert(meanEnergies.getRealm() == m_particleRealm);

  DataOps::setValue(a_meanEnergies, 0.0);

  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++) {
    this->computeReactiveMeanEnergiesPerCell(*a_meanEnergies[lvl], lvl);
  }
}

void
ItoPlasmaStepper::computeReactiveMeanEnergiesPerCell(LevelData<EBCellFAB>& a_meanEnergies, const int a_level) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeReactiveMeanEnergiesPerCell(LD<EBCellFAB>, int)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeReactiveMeanEnergiesPerCell(LD<EBCellFAB>, int)" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();

  CH_assert(a_meanEnergies.nComp() == numPlasmaSpecies);

  const DisjointBoxLayout& dbl   = m_amr->getGrids(m_particleRealm)[a_level];
  const EBISLayout&        ebisl = m_amr->getEBISLayout(m_particleRealm, m_plasmaPhase)[a_level];

  for (DataIterator dit(dbl); dit.ok(); ++dit) {
    const Box      box     = dbl[dit()];
    const EBISBox& ebisbox = ebisl[dit()];

    this->computeReactiveMeanEnergiesPerCell(a_meanEnergies[dit()], a_level, dit(), box, ebisbox);
  }
}

void
ItoPlasmaStepper::computeReactiveMeanEnergiesPerCell(EBCellFAB&      a_meanEnergies,
                                                     const int       a_level,
                                                     const DataIndex a_dit,
                                                     const Box       a_box,
                                                     const EBISBox&  a_ebisbox) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeReactiveMeanEnergiesPerCell(EBCellFABint, DataIndex, Box, EBISBox)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeReactiveMeanEnergiesPerCell(EBCellFABint, DataIndex, Box, EBISBox))" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();

  CH_assert(a_meanEnergies.nComp() == numPlasmaSpecies);

  // Get single-valued data.
  FArrayBox& meanEnergiesReg = a_meanEnergies.getFArrayBox();

  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>& solver = solverIt();
    const int                 idx    = solverIt.index();

    // Get the cell-sorted particles. This will issue an error if the user has not
    // sorted the particles by cell before calling this routine.
    const ParticleContainer<ItoParticle>& particles     = solver->getParticles(ItoSolver::WhichContainer::Bulk);
    const BinFab<ItoParticle>&            cellParticles = particles.getCellParticles(a_level, a_dit);

    // Regular grid cells.
    auto regularKernel = [&](const IntVect& iv) -> void {
      if (a_ebisbox.isRegular(iv)) {
        Real totalWeight = 0.0;
        Real totalEnergy = 0.0;

        for (ListIterator<ItoParticle> lit(cellParticles(iv, 0)); lit.ok(); ++lit) {
          totalWeight += lit().weight();
          totalEnergy += lit().weight() * lit().energy();
        }

        if (totalWeight > 0.0) {
          meanEnergiesReg(iv, idx) = totalEnergy / totalWeight;
        }
        else {
          meanEnergiesReg(iv, idx) = 0.0;
        }
      }
    };

    // Irregular cells -- note that only valid particles get to play with us.
    auto irregularKernel = [&](const VolIndex& vof) -> void {
      const IntVect  iv         = vof.gridIndex();
      const RealVect normal     = a_ebisbox.normal(vof);
      const RealVect ebCentroid = a_ebisbox.bndryCentroid(vof);

      Real totalWeight = 0.0;
      Real totalEnergy = 0.0;

      for (ListIterator<ItoParticle> lit(cellParticles(iv, 0)); lit.ok(); ++lit) {
        const RealVect& pos = lit().position();

        if ((pos - ebCentroid).dotProduct(normal) >= 0.0) {
          totalWeight += lit().weight();
          totalEnergy += lit().weight() * lit().energy();
        }
      }

      if (totalWeight > 0.0) {
        meanEnergiesReg(iv, idx) = totalEnergy / totalWeight;
      }
      else {
        meanEnergiesReg(iv, idx) = 0.0;
      }
    };

    // Run the kernels.
    VoFIterator& vofit = (*m_amr->getVofIterator(m_particleRealm, m_plasmaPhase)[a_level])[a_dit];

    BoxLoops::loop(a_box, regularKernel);
    BoxLoops::loop(vofit, irregularKernel);
  }
}

void
ItoPlasmaStepper::advanceReactionNetwork(const Real a_dt) noexcept
{
  CH_TIME("ItoPlasmaStepper::advanceReactionNetwork(dt)");
  if (m_verbosity > 5) {
    pout() << m_name + "::advanceReactionNetwork(dt)" << endl;
  }

  this->advanceReactionNetwork(m_electricFieldFluid, m_EdotJ, a_dt);
}

void
ItoPlasmaStepper::advanceReactionNetwork(const EBAMRCellData& a_electricField,
                                         const EBAMRCellData& a_EdotJ,
                                         const Real           a_dt) noexcept
{
  CH_TIME("ItoPlasmaStepper::advanceReactionNetwork(ppc, ypc, E, sources, dt)");
  if (m_verbosity > 5) {
    pout() << m_name + "::advanceReactionNetwork(ppc, ypc, E, sources, dt)" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();

  CH_assert(a_electricField.getRealm() == m_fluidRealm);
  CH_assert(a_EdotJ.getRealm() == m_fluidRealm);
  CH_assert(a_dt > 0.0);
  CH_assert(a_EdotJ[0]->nComp() == numPlasmaSpecies);

  // Compute the number of particles per cell and the mean energy of the particles. Copy the
  // result to the fluid realm because it is load balanced using number of grid cells and can
  // run the algorithms a bit more efficiently.
  this->computeReactiveParticlesPerCell(m_particlePPC);
  this->computeReactiveMeanEnergiesPerCell(m_particleEPS);

  m_fluidPPC.copy(m_particlePPC);
  m_fluidEPS.copy(m_particleEPS);

  // m_fluidYPC and m_particleYPC are the number of photons that will be generated -- need to initialize
  // to zero here.
  DataOps::setValue(m_fluidYPC, 0.0);
  DataOps::setValue(m_particleYPC, 0.0);

  // Back up the old number of particles per cell.
  DataOps::copy(m_particleOldPPC, m_particlePPC);

  // Advance the reaction network which gives us a new number of particles per cell, as well as the number of
  // photons that need to be generated per cell.
  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++) {
    this->advanceReactionNetwork(*m_fluidPPC[lvl],
                                 *m_fluidYPC[lvl],
                                 *m_fluidEPS[lvl],
                                 *a_electricField[lvl],
                                 *a_EdotJ[lvl],
                                 lvl,
                                 a_dt);
  }

  // Copy the results to the particle realm -- then reconcile the particles on the particle realm. Note that ''reconcile''
  // means that we adjust the weights so we don't exceed the desired number of particles per cell.
  m_particlePPC.copy(m_fluidPPC);
  m_particleYPC.copy(m_fluidYPC);
  m_particleEPS.copy(m_fluidEPS);

  this->reconcileParticles(m_particlePPC, m_particleOldPPC, m_particleEPS, m_particleYPC);
}

void
ItoPlasmaStepper::advanceReactionNetwork(LevelData<EBCellFAB>&       a_particlesPerCell,
                                         LevelData<EBCellFAB>&       a_newPhotonsPerCell,
                                         LevelData<EBCellFAB>&       a_meanParticleEnergies,
                                         const LevelData<EBCellFAB>& a_electricField,
                                         const LevelData<EBCellFAB>& a_EdotJ,
                                         const int                   a_level,
                                         const Real                  a_dt) noexcept
{
  CH_TIME("ItoPlasmaStepper::advanceReactionNetwork(LD<EBCellFAB>x5, int, Real)");
  if (m_verbosity > 5) {
    pout() << m_name + "::advanceReactionNetwork(LD<EBCellFAB>x5, int, Real)" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();
  const int numPhotonSpecies = m_physics->getNumPhotonSpecies();

  CH_assert(a_particlesPerCell.nComp() == numPlasmaSpecies);
  CH_assert(a_newPhotonsPerCell.nComp() == numPhotonSpecies);
  CH_assert(a_meanParticleEnergies.nComp() == numPlasmaSpecies);
  CH_assert(a_electricField.nComp() == SpaceDim);
  CH_assert(a_EdotJ.nComp() == numPlasmaSpecies);

  const DisjointBoxLayout& dbl = m_amr->getGrids(m_fluidRealm)[a_level];

  for (DataIterator dit(dbl); dit.ok(); ++dit) {
    this->advanceReactionNetwork(a_particlesPerCell[dit()],
                                 a_newPhotonsPerCell[dit()],
                                 a_meanParticleEnergies[dit()],
                                 a_electricField[dit()],
                                 a_EdotJ[dit()],
                                 a_level,
                                 dit(),
                                 dbl[dit()],
                                 m_amr->getDx()[a_level],
                                 a_dt);
  }
}

void
ItoPlasmaStepper::advanceReactionNetwork(EBCellFAB&       a_particlesPerCell,
                                         EBCellFAB&       a_newPhotonsPerCell,
                                         EBCellFAB&       a_meanParticleEnergies,
                                         const EBCellFAB& a_electricField,
                                         const EBCellFAB& a_EdotJ,
                                         const int        a_level,
                                         const DataIndex  a_dit,
                                         const Box        a_box,
                                         const Real       a_dx,
                                         const Real       a_dt) noexcept
{
  CH_TIME("ItoPlasmaStepper::advanceReactionNetwork(EBCellFABx5, int, DataIndex, Box, Realx2)");
  if (m_verbosity > 5) {
    pout() << m_name + "::advanceReactionNetwork(EBCellFABx5, int, DataIndex, Box, Realx2)" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();
  const int numPhotonSpecies = m_physics->getNumPhotonSpecies();

  CH_assert(a_particlesPerCell.nComp() == numPlasmaSpecies);
  CH_assert(a_newPhotonsPerCell.nComp() == numPhotonSpecies);
  CH_assert(a_meanParticleEnergies.nComp() == numPlasmaSpecies);
  CH_assert(a_electricField.nComp() == SpaceDim);
  CH_assert(a_EdotJ.nComp() == numPlasmaSpecies);

  // Geometric information that we require.
  const RealVect probLo  = m_amr->getProbLo();
  const EBISBox& ebisbox = m_amr->getEBISLayout(m_fluidRealm, m_plasmaPhase)[a_level][a_dit];
  const EBISBox& ebgraph = m_amr->getEBISLayout(m_fluidRealm, m_plasmaPhase)[a_level][a_dit];

  const FArrayBox& electricFieldReg = a_electricField.getFArrayBox();

  // Storage used by physics interface.
  Vector<Physics::ItoPlasma::FPR> particles(numPlasmaSpecies);
  Vector<Physics::ItoPlasma::FPR> newPhotons(numPhotonSpecies);
  Vector<Real>                    meanEnergies(numPlasmaSpecies);
  Vector<Real>                    energySources(numPlasmaSpecies);

  const Real dV = std::pow(a_dx, SpaceDim);

  // Populate single-valued data.
  FArrayBox&       particlesPerCellReg     = a_particlesPerCell.getFArrayBox();
  FArrayBox&       newPhotonsReg           = a_newPhotonsPerCell.getFArrayBox();
  FArrayBox&       meanParticleEnergiesReg = a_meanParticleEnergies.getFArrayBox();
  const FArrayBox& energySourcesReg        = a_EdotJ.getFArrayBox();

  // Handle to valid grid cells.
  const BaseFab<bool>& validCells = (*m_amr->getValidCells(m_fluidRealm)[a_level])[a_dit];

  // Regular cells
  auto regularKernel = [&](const IntVect& iv) -> void {
    if (ebisbox.isRegular(iv) && validCells(iv, 0)) {
      const RealVect pos = probLo + a_dx * (RealVect(iv) + 0.5 * RealVect::Unit);
      const RealVect E   = RealVect(D_DECL(electricFieldReg(iv, 0), electricFieldReg(iv, 1), electricFieldReg(iv, 2)));

      // Populate the data holders that the physics interface requires.
      for (int i = 0; i < numPlasmaSpecies; i++) {
        particles[i]     = (long long)particlesPerCellReg(iv, i);
        meanEnergies[i]  = meanParticleEnergiesReg(iv, i);
        energySources[i] = energySourcesReg(iv, i) * dV / Units::Qe;
      }

      for (int i = 0; i < numPhotonSpecies; i++) {
        newPhotons[i] = 0LL;
      }

      // Do the physics advance.
      m_physics->advanceKMC(particles, newPhotons, a_dt, E, a_dx, 1.0);

      // Repopulate the input data holders with the new number of particles/photons per cell.
      for (int i = 0; i < numPlasmaSpecies; i++) {
        particlesPerCellReg(iv, i)     = 1.0 * particles[i];
        meanParticleEnergiesReg(iv, i) = 1.0 * meanEnergies[i];
      }

      for (int i = 0; i < numPhotonSpecies; i++) {
        newPhotonsReg(iv, i) = 1.0 * newPhotons[i];
      }
    }
  };

  // Irregular cells
  auto irregularKernel = [&](const VolIndex& vof) -> void {
    const IntVect iv = vof.gridIndex();

    if (validCells(iv, 0)) {
      const Real     kappa = ebisbox.volFrac(vof);
      const RealVect pos   = probLo + Location::position(Location::Cell::Centroid, vof, ebisbox, a_dx);
      const RealVect E = RealVect(D_DECL(a_electricField(vof, 0), a_electricField(vof, 1), a_electricField(vof, 2)));

      // Initialize for this cell.
      for (int i = 0; i < numPlasmaSpecies; i++) {
        particles[i]     = (long long)a_particlesPerCell(vof, i);
        meanEnergies[i]  = a_meanParticleEnergies(vof, i);
        energySources[i] = a_EdotJ(vof, i) * kappa * dV / Units::Qe;
      }

      for (int i = 0; i < numPhotonSpecies; i++) {
        newPhotons[i] = 0LL;
      }

      // Do the physics advance
      m_physics->advanceKMC(particles, newPhotons, a_dt, E, a_dx, kappa);

      // Repopulate the input data holders with the new number of particles/photons per cell.
      for (int i = 0; i < numPlasmaSpecies; i++) {
        a_particlesPerCell(vof, i)     = 1.0 * particles[i];
        a_meanParticleEnergies(vof, i) = 1.0 * meanEnergies[i];
      }

      for (int i = 0; i < numPhotonSpecies; i++) {
        a_newPhotonsPerCell(vof, i) = 1.0 * newPhotons[i];
      }
    }
  };

  // Run the kernels.
  VoFIterator& vofit = (*m_amr->getVofIterator(m_fluidRealm, m_plasmaPhase)[a_level])[a_dit];

  BoxLoops::loop(a_box, regularKernel);
  BoxLoops::loop(vofit, irregularKernel);
}

void
ItoPlasmaStepper::reconcileParticles(const EBAMRCellData& a_newParticlesPerCell,
                                     const EBAMRCellData& a_oldParticlesPerCell,
                                     const EBAMRCellData& a_meanParticleEnergies,
                                     const EBAMRCellData& a_newPhotonsPerCell) noexcept
{
  CH_TIME("ItoPlasmaStepper::reconcileParticles(EBAMRCellDatax4)");
  if (m_verbosity > 5) {
    pout() << m_name + "::reconcileParticles(EBAMRCellDatax4)";
  }

  CH_assert(a_newParticlesPerCell.getRealm() == m_particleRealm);
  CH_assert(a_oldParticlesPerCell.getRealm() == m_particleRealm);
  CH_assert(a_meanParticleEnergies.getRealm() == m_particleRealm);
  CH_assert(a_newPhotonsPerCell.getRealm() == m_particleRealm);

  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++) {
    this->reconcileParticles(*a_newParticlesPerCell[lvl],
                             *a_oldParticlesPerCell[lvl],
                             *a_meanParticleEnergies[lvl],
                             *a_newPhotonsPerCell[lvl],
                             lvl);
  }
}

void
ItoPlasmaStepper::reconcileParticles(const LevelData<EBCellFAB>& a_newParticlesPerCell,
                                     const LevelData<EBCellFAB>& a_oldParticlesPerCell,
                                     const LevelData<EBCellFAB>& a_meanParticleEnergies,
                                     const LevelData<EBCellFAB>& a_newPhotonsPerCell,
                                     const int                   a_level) noexcept
{
  CH_TIME("ItoPlasmaStepper::reconcileParticles(LevelData<EBCellFAB>x4, int)");
  if (m_verbosity > 5) {
    pout() << m_name + "::reconcileParticles(LevelData<EBCellFAB>x4, int)" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();
  const int numPhotonSpecies = m_physics->getNumPhotonSpecies();

  CH_assert(a_newParticlesPerCell.nComp() == numPlasmaSpecies);
  CH_assert(a_oldParticlesPerCell.nComp() == numPlasmaSpecies);
  CH_assert(a_meanParticleEnergies.nComp() == numPlasmaSpecies);
  CH_assert(a_newPhotonsPerCell.nComp() == numPhotonSpecies);

  const DisjointBoxLayout& dbl = m_amr->getGrids(m_particleRealm)[a_level];

  for (DataIterator dit(dbl); dit.ok(); ++dit) {
    this->reconcileParticles(a_newParticlesPerCell[dit()],
                             a_oldParticlesPerCell[dit()],
                             a_meanParticleEnergies[dit()],
                             a_newPhotonsPerCell[dit()],
                             a_level,
                             dit(),
                             dbl[dit()],
                             m_amr->getDx()[a_level]);
  }
}

void
ItoPlasmaStepper::reconcileParticles(const EBCellFAB& a_newParticlesPerCell,
                                     const EBCellFAB& a_oldParticlesPerCell,
                                     const EBCellFAB& a_meanParticleEnergies,
                                     const EBCellFAB& a_newPhotonsPerCell,
                                     const int        a_level,
                                     const DataIndex  a_dit,
                                     const Box        a_box,
                                     const Real       a_dx) noexcept
{
  CH_TIME("ItoPlasmaStepper::reconcileParticles(EBCellFABx3, int, DataIndex, Box, Real)");
  if (m_verbosity > 5) {
    pout() << m_name + "::reconcileParticles(EBCellFABx3, int, DataIndex, Box, Real)" << endl;
  }

  // TLDR: This is the main routine for generating new particles/photons after the chemistry advance have finished. We have
  //       already computed the number of particles in each grid cell, and we now need to generate them. To do that we use
  //       the reconciliation routines from the physics interface, which takes the per-cell responsibility for that. The main
  //       work done in this routine is to expose the per-patch data to per-cell data that the physics interface can then use.

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();
  const int numPhotonSpecies = m_physics->getNumPhotonSpecies();

  CH_assert(a_newParticlesPerCell.nComp() == numPlasmaSpecies);
  CH_assert(a_oldParticlesPerCell.nComp() == numPlasmaSpecies);
  CH_assert(a_meanParticleEnergies.nComp() == numPlasmaSpecies);
  CH_assert(a_newPhotonsPerCell.nComp() == numPhotonSpecies);

  // Geometric information that we need.
  const RealVect probLo  = m_amr->getProbLo();
  const EBISBox& ebisbox = m_amr->getEBISLayout(m_particleRealm, m_plasmaPhase)[a_level][a_dit];
  const EBISBox& ebgraph = m_amr->getEBISLayout(m_particleRealm, m_plasmaPhase)[a_level][a_dit];

  // List of valid grid cells
  const BaseFab<bool>& validCells = (*m_amr->getValidCells(m_particleRealm)[a_level])[a_dit];

  // Pointers to cell-centered particle storages. Note that the BinFabs hold a list of particles/photons
  // on each grid cell. We will manipulate these lists.
  Vector<BinFab<ItoParticle>*> particlesFAB(numPlasmaSpecies);
  Vector<BinFab<Photon>*>      sourcePhotonsFAB(numPhotonSpecies);
  Vector<BinFab<Photon>*>      bulkPhotonsFAB(numPhotonSpecies);

  // Get a handle to the cell-sorted particles from the Ito solvers.
  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>& solver = solverIt();

    const int idx = solverIt.index();

    ParticleContainer<ItoParticle>& solverParticles = solver->getParticles(ItoSolver::WhichContainer::Bulk);

    particlesFAB[idx] = &(solverParticles.getCellParticles(a_level, a_dit));
  }

  // Get a handle to the cell-sorted photons from the Monte Carlo photon solvers.
  for (auto solverIt = m_rte->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<McPhoto>& solver = solverIt();

    const int idx = solverIt.index();

    ParticleContainer<Photon>& solverBulkPhotons   = solver->getBulkPhotons();
    ParticleContainer<Photon>& solverSourcePhotons = solver->getSourcePhotons();

    bulkPhotonsFAB[idx]   = &(solverBulkPhotons.getCellParticles(a_level, a_dit));
    sourcePhotonsFAB[idx] = &(solverSourcePhotons.getCellParticles(a_level, a_dit));
  }

  // The physics interface takes the physical number of particles/photons as arguments
  // to the reconciliation routines. These need to be set from the input arguments; this is the
  // storage we use in the grid cells.
  Vector<Real>                    particleMeanEnergies(numPlasmaSpecies);
  Vector<Physics::ItoPlasma::FPR> numNewParticles(numPlasmaSpecies);
  Vector<Physics::ItoPlasma::FPR> numOldParticles(numPlasmaSpecies);
  Vector<Physics::ItoPlasma::FPR> numNewPhotons(numPhotonSpecies);

  // The physics interface also takes the actual particles/photons as argument to its reconciliation routines. This
  // is the storage we use for these; note that it is repopulated in every grid cell.
  Vector<List<ItoParticle>*> particles(numPlasmaSpecies);
  Vector<List<Photon>*>      bulkPhotons(numPhotonSpecies);
  Vector<List<Photon>*>      sourcePhotons(numPhotonSpecies);

  // Regular cells
  auto regularKernel = [&](const IntVect& iv) -> void {
    if (ebisbox.isRegular(iv) && validCells(iv)) {
      const RealVect cellPos       = probLo + a_dx * (RealVect(iv) + 0.5 * RealVect::Unit);
      const RealVect centroidPos   = cellPos;
      const RealVect lo            = -0.5 * RealVect::Unit;
      const RealVect hi            = 0.5 * RealVect::Unit;
      const RealVect bndryCentroid = RealVect::Zero;
      const RealVect bndryNormal   = RealVect::Zero;
      const Real     kappa         = 1.0;

      // Populate the per-cell particle data.
      for (int i = 0; i < numPlasmaSpecies; i++) {
        particles[i]            = &((*particlesFAB[i])(iv, 0));
        particleMeanEnergies[i] = a_meanParticleEnergies.getSingleValuedFAB()(iv, i);
        numNewParticles[i]      = (long long)(a_newParticlesPerCell.getSingleValuedFAB()(iv, i));
        numOldParticles[i]      = (long long)(a_oldParticlesPerCell.getSingleValuedFAB()(iv, i));
      }

      // Populate the per-cell photon data.
      for (int i = 0; i < numPhotonSpecies; i++) {
        bulkPhotons[i]   = &((*bulkPhotonsFAB[i])(iv, 0));
        sourcePhotons[i] = &((*sourcePhotonsFAB[i])(iv, 0));

        numNewPhotons[i] = (long long)(a_newPhotonsPerCell.getSingleValuedFAB()(iv, i));

        // sourcePhotons will hold the NEW number of photons to be generated -- it should already
        // have been cleared in upstream code but I'm leaving this in for safety.
        sourcePhotons[i]->clear();
      }

      // Reconcile the ItoSolver particles -- this either removes weight from the original particles (if we lost physical particles)
      // or adds new particles (if we gained physical particles)
      m_physics->reconcileParticles(particles,
                                    numNewParticles,
                                    numOldParticles,
                                    cellPos,
                                    centroidPos,
                                    lo,
                                    hi,
                                    bndryCentroid,
                                    bndryNormal,
                                    a_dx,
                                    kappa);

      // Reconcile the photon solver. This will generate new computational photons that are later added to the Monte Carlo photon
      // solvers.
      m_physics->reconcilePhotons(sourcePhotons,
                                  numNewPhotons,
                                  cellPos,
                                  centroidPos,
                                  lo,
                                  hi,
                                  bndryCentroid,
                                  bndryNormal,
                                  a_dx,
                                  kappa);

      // Add the photoionization term. This will adds new particles from the photoionization reactions.
      m_physics->reconcilePhotoionization(particles, bulkPhotons);
    }
  };

  // Irregular cells
  auto irregularKernel = [&](const VolIndex& vof) -> void {
    const IntVect iv = vof.gridIndex();
    if (ebisbox.isIrregular(iv) && validCells(iv, 0)) {
      const Real     kappa         = ebisbox.volFrac(vof);
      const RealVect cellPos       = probLo + Location::position(Location::Cell::Boundary, vof, ebisbox, a_dx);
      const RealVect centroidPos   = ebisbox.centroid(vof);
      const RealVect bndryCentroid = ebisbox.bndryCentroid(vof);
      const RealVect bndryNormal   = ebisbox.normal(vof);

      // Compute the minimum bounding box that encloses this cut-cell.
      RealVect lo = -0.5 * RealVect::Unit;
      RealVect hi = 0.5 * RealVect::Unit;
      if (kappa < 1.0) {
        DataOps::computeMinValidBox(lo, hi, bndryNormal, bndryCentroid);
      }

      // Populate the per-cell particle data.
      for (int i = 0; i < numPlasmaSpecies; i++) {
        particles[i]            = &((*particlesFAB[i])(iv, 0));
        particleMeanEnergies[i] = a_meanParticleEnergies(vof, i);
        numNewParticles[i]      = (long long)(a_newParticlesPerCell(vof, i));
        numOldParticles[i]      = (long long)(a_oldParticlesPerCell(vof, i));
      }

      // Populate the per-cell photon data.
      for (int i = 0; i < numPhotonSpecies; i++) {
        bulkPhotons[i]   = &((*bulkPhotonsFAB[i])(iv, 0));
        sourcePhotons[i] = &((*sourcePhotonsFAB[i])(iv, 0));

        numNewPhotons[i] = (long long)(a_newPhotonsPerCell(vof, i));

        // sourcePhotons will hold the NEW number of photons to be generated -- it should already
        // have been cleared in upstream code but I'm leaving this in for safety.
        sourcePhotons[i]->clear();
      }

      // Reconcile the ItoSolver particles -- this either removes weight from the original particles (if we lost physical particles)
      // or adds new particles (if we gained physical particles)
      m_physics->reconcileParticles(particles,
                                    numNewParticles,
                                    numOldParticles,
                                    cellPos,
                                    centroidPos,
                                    lo,
                                    hi,
                                    bndryCentroid,
                                    bndryNormal,
                                    a_dx,
                                    kappa);

      // Reconcile the photon solver. This will generate new computational photons that are later added to the Monte Carlo photon
      // solvers.
      m_physics->reconcilePhotons(sourcePhotons,
                                  numNewPhotons,
                                  cellPos,
                                  centroidPos,
                                  lo,
                                  hi,
                                  bndryCentroid,
                                  bndryNormal,
                                  a_dx,
                                  kappa);

      // Add the photoionization term. This will adds new particles from the photoionization reactions.
      m_physics->reconcilePhotoionization(particles, bulkPhotons);
    }
  };

  // Run the kernels.
  VoFIterator& vofit = (*m_amr->getVofIterator(m_particleRealm, m_plasmaPhase)[a_level])[a_dit];

  BoxLoops::loop(a_box, regularKernel);
  BoxLoops::loop(vofit, irregularKernel);
}

Real
ItoPlasmaStepper::computePhysicsDt() noexcept
{
  CH_TIME("ItoPlasmaStepper::computePhysicsDt()");
  if (m_verbosity > 5) {
    pout() << m_name + "::computePhysicsDt()" << endl;
  }

  const Real dt = this->computePhysicsDt(m_electricFieldFluid);

  return dt;
}

Real
ItoPlasmaStepper::computePhysicsDt(const EBAMRCellData& a_electricField) noexcept
{
  CH_TIME("ItoPlasmaStepper::computePhysicsDt(EBAMRCellFAB, Vector<EBAMRCellFAB*>)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computePhysicsDt(EBAMRCellFAB, Vector<EBAMRCellFAB*>)" << endl;
  }

  CH_assert(a_electricField.getRealm() == m_fluidRealm);

  Real minDt = std::numeric_limits<Real>::max();

  // Sort by cell so we can compute the number of particles.
  m_ito->sortParticlesByCell(ItoSolver::WhichContainer::Bulk);

  // Compute the number of reactive particles per cell and put the result on the fluid realm.
  if (m_dualGrid) {
    this->computeReactiveParticlesPerCell(m_particlePPC);

    m_fluidPPC.copy(m_particlePPC);
  }
  else {
    this->computeReactiveParticlesPerCell(m_fluidPPC);
  }

  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++) {
    const Real levelDt = this->computePhysicsDt(*a_electricField[lvl], *m_fluidPPC[lvl], lvl);

    minDt = std::min(minDt, levelDt);
  }

  // Sort by patch.
  m_ito->sortParticlesByPatch(ItoSolver::WhichContainer::Bulk);

  return minDt;
}

Real
ItoPlasmaStepper::computePhysicsDt(const LevelData<EBCellFAB>& a_electricField,
                                   const LevelData<EBCellFAB>& a_particlesPerCell,
                                   const int                   a_level) noexcept
{
  CH_TIME("ItoPlasmaStepper::computePhysicsDt(LD<EBCellFAB>, LD<EBCellFAB>, int)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computePhysicsDt(LD<EBCellFAB>, LD<EBCellFAB>, int)" << endl;
  }

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();

  CH_assert(a_particlesPerCell.nComp() == numPlasmaSpecies);
  CH_assert(a_electricField.nComp() == SpaceDim);

  Real minDt = std::numeric_limits<Real>::max();

  const DisjointBoxLayout& dbl = m_amr->getGrids(m_fluidRealm)[a_level];

  for (DataIterator dit(dbl); dit.ok(); ++dit) {
    const Real patchDt =
      this->computePhysicsDt(a_electricField[dit()], a_particlesPerCell[dit()], a_level, dit(), dbl[dit()]);

    minDt = std::min(minDt, patchDt);
  }

  return ParallelOps::min(minDt);
}

Real
ItoPlasmaStepper::computePhysicsDt(const EBCellFAB& a_electricField,
                                   const EBCellFAB& a_particlesPerCell,
                                   const int        a_level,
                                   const DataIndex  a_dit,
                                   const Box        a_box) noexcept
{
  CH_TIME("ItoPlasmaStepper::computePhysicsDt(EBCellFAB, EBCellFAB, int, DataIndex, Box)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computePhysicsDt(EBCellFAB, EBCellFAB, int, DataIndex, Box)" << endl;
  }

  Real minDt = std::numeric_limits<Real>::max();

  const int numPlasmaSpecies = m_physics->getNumPlasmaSpecies();

  CH_assert(a_electricField.nComp() == SpaceDim);
  CH_assert(a_particlesPerCell.nComp() == numPlasmaSpecies);

  // Geometric information that we need.
  const Real     dx      = m_amr->getDx()[a_level];
  const RealVect probLo  = m_amr->getProbLo();
  const EBISBox& ebisbox = m_amr->getEBISLayout(m_fluidRealm, m_plasmaPhase)[a_level][a_dit];

  // Valid grid cells.
  const BaseFab<bool>& validCells = (*m_amr->getValidCells(m_fluidRealm)[a_level])[a_dit];

  // Handles to regular grid data.
  const FArrayBox& electricFieldReg    = a_electricField.getFArrayBox();
  const FArrayBox& particlesPerCellReg = a_particlesPerCell.getFArrayBox();

  // The kernel require storage for the per-cell number of particles. This is
  // the storage we use for that
  Vector<Physics::ItoPlasma::FPR> ppc(numPlasmaSpecies);

  // Regular grid kernel.
  auto regularKernel = [&](const IntVect& iv) -> void {
    // Only regular cells not covered by a finer grid.
    if (ebisbox.isRegular(iv) && validCells(iv, 0)) {
      const RealVect E   = RealVect(D_DECL(electricFieldReg(iv, 0), electricFieldReg(iv, 1), electricFieldReg(iv, 2)));
      const RealVect pos = m_amr->getProbLo() + dx * (RealVect(iv) + 0.5 * RealVect::Unit);

      for (int i = 0; i < numPlasmaSpecies; i++) {
        ppc[i] = particlesPerCellReg(iv, i);
      }

      const Real cellDt = m_physics->computeDt(E, pos, ppc);

      minDt = std::min(minDt, cellDt);
    }
  };

  // Irregular grid kernel.
  auto irregularKernel = [&](const VolIndex& vof) -> void {
    const IntVect& iv = vof.gridIndex();

    // Only irregular cells not covered by a finer grid.
    if (ebisbox.isIrregular(iv) && validCells(iv, 0)) {
      const RealVect E   = RealVect(D_DECL(a_electricField(vof, 0), a_electricField(vof, 1), a_electricField(vof, 2)));
      const RealVect pos = probLo + Location::position(Location::Cell::Centroid, vof, ebisbox, dx);

      for (int i = 0; i < numPlasmaSpecies; i++) {
        ppc[i] = a_particlesPerCell(vof, i);
      }

      const Real cellDt = m_physics->computeDt(E, pos, ppc);

      minDt = std::min(minDt, cellDt);
    }
  };

  // Run the kernels.
  VoFIterator& vofit = (*m_amr->getVofIterator(m_fluidRealm, m_plasmaPhase)[a_level])[a_dit];

  BoxLoops::loop(a_box, regularKernel);
  BoxLoops::loop(vofit, irregularKernel);

  return minDt;
}

void
ItoPlasmaStepper::advancePhotons(const Real a_dt) noexcept
{
  CH_TIME("ItoPlasmaStepper::advancePhotons(Real)");
  if (m_verbosity > 5) {
    pout() << m_name + "::advancePhotons(Real)" << endl;
  }

  // TLDR: This will add the source photons to the "bulk" photons and then advance them. If the
  //       solver is a true transient solver then the photons are moved and some of them are eventually
  //       absorbed on the mesh. If the solver is an "instanteneous" solver then all source photons
  //       are absorbed on the mesh.

  for (auto solverIt = m_rte->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<McPhoto>& solver = solverIt();

    // To reiterate: photons are the photons that live in the solver and are moved around. bulkPhotons
    // are the solvers that were absorbed on the mesh, bbPhotons are the photons that collided with the EB
    // and domainPhotons are photons that moved out of the domain.
    ParticleContainer<Photon>& photons       = solver->getPhotons();
    ParticleContainer<Photon>& bulkPhotons   = solver->getBulkPhotons();
    ParticleContainer<Photon>& ebPhotons     = solver->getEbPhotons();
    ParticleContainer<Photon>& domainPhotons = solver->getDomainPhotons();
    ParticleContainer<Photon>& sourcePhotons = solver->getSourcePhotons();

    if (solver->isInstantaneous()) {
      solver->clear(photons);

      // Add source Photons
      photons.addParticles(sourcePhotons);
      solver->clear(sourcePhotons);

      // Instantaneous advance.
      solver->advancePhotonsInstantaneous(bulkPhotons, ebPhotons, domainPhotons, photons);
    }
    else {
      // Add source Photons
      photons.addParticles(sourcePhotons);
      solver->clear(sourcePhotons);

      // Stationary advance
      solver->advancePhotonsTransient(bulkPhotons, ebPhotons, domainPhotons, photons, a_dt);
    }
  }
}

void
ItoPlasmaStepper::sortPhotonsByCell(const McPhoto::WhichContainer a_which) noexcept
{
  CH_TIME("ItoPlasmaStepper::sortPhotonsByCell(McPhoto::WhichContainer)");
  if (m_verbosity > 5) {
    pout() << m_name + "::sortPhotonsByCell(McPhoto::WhichContainer)" << endl;
  }

  for (auto solverIt = m_rte->iterator(); solverIt.ok(); ++solverIt) {
    solverIt()->sortPhotonsByCell(a_which);
  }
}

void
ItoPlasmaStepper::sortPhotonsByPatch(const McPhoto::WhichContainer a_which) noexcept
{
  CH_TIME("ItoPlasmaStepper::sortPhotonsByPatch(McPhoto::WhichContainer)");
  if (m_verbosity > 5) {
    pout() << m_name + "::sortPhotonsByPatch(McPhoto::WhichContainer)" << endl;
  }

  for (auto solverIt = m_rte->iterator(); solverIt.ok(); ++solverIt) {
    solverIt()->sortPhotonsByPatch(a_which);
  }
}

bool
ItoPlasmaStepper::loadBalanceThisRealm(const std::string a_realm) const
{
  CH_TIME("TimeStepper::loadBalanceThisRealm");
  if (m_verbosity > 5) {
    pout() << "TimeStepper::loadBalanceThisRealm" << endl;
  }

  bool ret = false;

  if (a_realm == m_particleRealm && m_loadBalance) {
    ret = true;
  }

  return ret;
}

void
ItoPlasmaStepper::loadBalanceBoxes(Vector<Vector<int>>&             a_procs,
                                   Vector<Vector<Box>>&             a_boxes,
                                   const std::string                a_realm,
                                   const Vector<DisjointBoxLayout>& a_grids,
                                   const int                        a_lmin,
                                   const int                        a_finestLevel)
{
  CH_TIME("ItoPlasmaStepper::loadBalanceBoxes");
  if (m_verbosity > 5) {
    pout() << m_name + "::loadBalanceBoxes" << endl;
  }

  if (m_loadBalance && a_realm == m_particleRealm) {
    this->loadBalanceParticleRealm(a_procs, a_boxes, a_realm, a_grids, a_lmin, a_finestLevel);
  }
  else {
    MayDay::Abort("ItoPlasmaStepper::loadBalanceBoxes - shouldn't happen, how did you get here..?");
  }
}

Vector<long int>
ItoPlasmaStepper::getCheckpointLoads(const std::string a_realm, const int a_level) const
{
  CH_TIME("ItoPlasmaStepper::getCheckpointLoads(...)");
  if (m_verbosity > 5) {
    pout() << m_name + "::getCheckpointLoads(...)" << endl;
  }

  const DisjointBoxLayout& dbl  = m_amr->getGrids(a_realm)[a_level];
  const int                nbox = dbl.size();

  Vector<long int> loads(nbox, 0L);

  if (m_loadBalance && a_realm == m_particleRealm) {

    // If we're load balancing with particles, get the number of particles per patch
    // from the relevant particle solvers. Since these are Ito solvers, the loads
    // are equal to the number of computational particles in the grid patches.
    Vector<RefCountedPtr<ItoSolver>> loadBalanceProxySolvers = this->getLoadBalanceSolvers();

    for (int isolver = 0; isolver < loadBalanceProxySolvers.size(); isolver++) {

      // This solver computes loads -- there's a parallel gather operation
      // under the hood here.
      Vector<long int> solverLoads(nbox, 0L);
      loadBalanceProxySolvers[isolver]->computeLoads(solverLoads, dbl, a_level);

      // Add to total loads.
      for (int ibox = 0; ibox < nbox; ibox++) {
        loads[ibox] += solverLoads[ibox];
      }
    }

    // Add the "constant" loads -- these are computational loads due to the "mesh" part. We use
    // a heuristic where we have m_loadPerCell "cost".
    for (LayoutIterator lit = dbl.layoutIterator(); lit.ok(); ++lit) {
      const Box box = dbl[lit()];

      loads[lit().intCode()] += lround(m_loadPerCell * box.numPts());
    }
  }
  else {
    loads = TimeStepper::getCheckpointLoads(a_realm, a_level);
  }

  return loads;
}

void
ItoPlasmaStepper::loadBalanceParticleRealm(Vector<Vector<int>>&             a_procs,
                                           Vector<Vector<Box>>&             a_boxes,
                                           const std::string                a_realm,
                                           const Vector<DisjointBoxLayout>& a_grids,
                                           const int                        a_lmin,
                                           const int                        a_finestLevel) noexcept
{
  CH_TIME("ItoPlasmaStepper::loadBalanceParticleRealm(...)");
  if (m_verbosity > 5) {
    pout() << m_name + "::loadBalanceParticleRealm(...)" << endl;
  }

  // Decompose the DisjointBoxLayout
  a_procs.resize(1 + a_finestLevel);
  a_boxes.resize(1 + a_finestLevel);

  for (int lvl = a_lmin; lvl <= a_finestLevel; lvl++) {
    a_procs[lvl] = a_grids[lvl].procIDs();
    a_boxes[lvl] = a_grids[lvl].boxArray();
  }

  // Get the particles that we will use for load balancing.
  Vector<RefCountedPtr<ItoSolver>> loadBalanceProxySolvers = this->getLoadBalanceSolvers();

  // Regrid particles onto the "dummy grids" a_grids
  for (int i = 0; i < loadBalanceProxySolvers.size(); i++) {
    ParticleContainer<ItoParticle>& particles = loadBalanceProxySolvers[i]->getParticles(
      ItoSolver::WhichContainer::Bulk);

    m_amr->remapToNewGrids(particles, a_lmin, a_finestLevel);

    // If we make superparticles during regrids, do it here so we can better estimate the computational loads for each patch. This way, if a grid is removed the realistic
    // load estimate of the underlying grid(s) is improved.
    if (m_regridSuperparticles) {
      particles.sortParticlesByCell();
      loadBalanceProxySolvers[i]->makeSuperparticles(ItoSolver::WhichContainer::Bulk, m_particlesPerCell);
      particles.sortParticlesByPatch();
    }
  }

  // Get loads on each level
  Vector<Vector<long int>> loads(1 + a_finestLevel);
  for (int lvl = 0; lvl <= a_finestLevel; lvl++) {
    loads[lvl] = this->getCheckpointLoads(a_realm, lvl);
  }

  // Do the actual load balancing
  LoadBalancing::sort(a_boxes, loads, m_boxSort);
  LoadBalancing::balanceLevelByLevel(a_procs, loads, a_boxes);
  //  LoadBalancing::hierarchy(a_procs, loads, a_boxes); If you want to try something crazy...

  // Go back to "pre-regrid" mode so we can get particles to the correct patches after load balancing.
  for (int i = 0; i < loadBalanceProxySolvers.size(); i++) {
    ParticleContainer<ItoParticle>& particles = loadBalanceProxySolvers[i]->getParticles(
      ItoSolver::WhichContainer::Bulk);
    particles.preRegrid(a_lmin);
  }
}

Vector<RefCountedPtr<ItoSolver>>
ItoPlasmaStepper::getLoadBalanceSolvers() const noexcept
{
  CH_TIME("ItoPlasmaStepper::getLoadBalanceSolvers()");
  if (m_verbosity > 5) {
    pout() << m_name + "::getLoadBalanceSolvers()" << endl;
  }

  Vector<RefCountedPtr<ItoSolver>> loadBalanceProxySolvers;

  if (m_loadBalanceIndex < 0) {
    for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
      loadBalanceProxySolvers.push_back(solverIt());
    }
  }
  else {
    RefCountedPtr<ItoSolver>& solver = m_ito->getSolvers()[m_loadBalanceIndex];
    loadBalanceProxySolvers.push_back(solver);
  }

  return loadBalanceProxySolvers;
}

void
ItoPlasmaStepper::computeEdotJSource(const Real a_dt) noexcept
{
  CH_TIME("ItoPlasmaStepper::computeEdotJSource(a_dt)");
  if (m_verbosity > 5) {
    pout() << m_name + "::computeEdotJSource(a_dt)" << endl;
  }

  CH_assert(a_dt > 0.0);

  DataOps::setValue(m_EdotJ, 0.0);

  CH_assert(m_EdotJ.getRealm() == m_fluidRealm);

  // TLDR: EdotJ is an energy term for the various species, i.e. it is the rate of energy increase as the particle moves from
  //       position A to position B, excluding friction from collision with other molecules. We compute this energy increase as
  //
  //          q * V(B) - V(A)
  //
  //       which means that the energy rate is q*(V(B) - V(A))/a_dt.
  //
  //       We simply assign this factor to the particles and then deposit them on the mesh. However, this is more complex than
  //       it sounds because the particles m_EdotJ live on different realms. The way we do this is that we copy the potential
  //       over into the particle realm and we interpolate V(B) and V(A) onto some storage in the particle container. We then
  //       assign an effective weight w * [V(B) - V(A)] to the particles which we deposit onto the mesh using the appropriate
  //       deposition scheme that the user has assgned.
  //

  // Allocate a particle data holder with three scalar storage spaces; these are the weight, V(A), and V(B). We also
  // need the positions A and B so they're in here as well.
  using CompParticle = GenericParticle<3, 1>;

  ParticleContainer<CompParticle> computationParticles;
  m_amr->allocate(computationParticles, m_particleRealm);

  // Electrostatic potential on appropriate phase. This is defined on the fluid realm
  // but we need it on the particle realm.
  const EBAMRCellData potentialPhase = m_amr->alias(m_plasmaPhase, m_fieldSolver->getPotential());
  m_particleScratch1.copy(potentialPhase);

  m_amr->conservativeAverage(m_particleScratch1, m_particleRealm, m_plasmaPhase);
  m_amr->interpGhost(m_particleScratch1, m_particleRealm, m_plasmaPhase);

  for (auto solverIt = m_ito->iterator(); solverIt.ok(); ++solverIt) {
    RefCountedPtr<ItoSolver>&        solver  = solverIt();
    const RefCountedPtr<ItoSpecies>& species = solver->getSpecies();

    const int  idx       = solverIt.index();
    const int  Z         = species->getChargeNumber();
    const bool mobile    = solver->isMobile();
    const bool diffusive = solver->isDiffusive();

    if (Z != 0 && (mobile || diffusive)) {

      const ParticleContainer<ItoParticle>& particles = solver->getParticles(ItoSolver::WhichContainer::Bulk);

      // Copy the ItoParticles to the transient particles we use for computing these things.
      for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++) {
        const DisjointBoxLayout& dbl = m_amr->getGrids(m_particleRealm)[lvl];

        for (DataIterator dit(dbl); dit.ok(); ++dit) {

          List<CompParticle>&      patchCompParticles = computationParticles[lvl][dit()].listItems();
          const List<ItoParticle>& patchItoParticles  = particles[lvl][dit()].listItems();

          for (ListIterator<ItoParticle> lit(patchItoParticles); lit.ok(); ++lit) {
            const ItoParticle& itoParticle = lit();

            CompParticle p;

            // vect<0> holds the starting position. real<0> holds the weight and
            // real<1> is used for V(A) and real<2> is used for V(B)
            p.position()         = itoParticle.position();
            p.template real<0>() = itoParticle.weight();
            p.template real<1>() = 0.0;
            p.template real<2>() = 0.0;
            p.template vect<0>() = itoParticle.oldPosition();

            patchCompParticles.add(p);
          }
        }
      }

      // Interpolate the potential to the current particle position which gives us V(B) for the particles.
      m_amr->interpolateParticles<CompParticle, &CompParticle::template real<1>>(computationParticles,
                                                                                 m_particleRealm,
                                                                                 m_plasmaPhase,
                                                                                 m_particleScratch1,
                                                                                 solver->getDeposition(),
                                                                                 false);

      // Move the particles back to their old positions and interpolate the potential there.
      for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++) {
        const DisjointBoxLayout& dbl = m_amr->getGrids(m_particleRealm)[lvl];

        for (DataIterator dit(dbl); dit.ok(); ++dit) {
          List<CompParticle>& particles = computationParticles[lvl][dit()].listItems();

          for (ListIterator<CompParticle> lit(particles); lit.ok(); ++lit) {

            // Swap positions.
            const RealVect tmp = lit().position();

            // vect<0> holds the end position.
            lit().position()         = lit().template vect<0>();
            lit().template vect<0>() = tmp;
          }
        }
      }

      computationParticles.remap();

      // Interpolate the potential to the previous particle position which gives us V(A) for the particles.
      m_amr->interpolateParticles<CompParticle, &CompParticle::template real<2>>(computationParticles,
                                                                                 m_particleRealm,
                                                                                 m_plasmaPhase,
                                                                                 m_particleScratch1,
                                                                                 solver->getDeposition());

      // Move the particles back again and multiply the weight by Z * (V(A) - V(B))/a_dt
      for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++) {
        const DisjointBoxLayout& dbl = m_amr->getGrids(m_particleRealm)[lvl];

        for (DataIterator dit(dbl); dit.ok(); ++dit) {
          List<CompParticle>& particles = computationParticles[lvl][dit()].listItems();

          for (ListIterator<CompParticle> lit(particles); lit.ok(); ++lit) {
            lit().position() = lit().template vect<0>();

            // Compute weight * V(B) - V(A) where V(B) is stored in real<1> and V(A) is stored in real<2>
            lit().template real<0>() *= (lit().template real<1>() - lit().template real<2>());
          }
        }
      }

      computationParticles.remap();

      // Deposit the particles
      m_amr->depositParticles<CompParticle, &CompParticle::template real<0>>(m_particleScratch1,
                                                                             m_particleRealm,
                                                                             m_plasmaPhase,
                                                                             computationParticles,
                                                                             solver->getDeposition(),
                                                                             solver->getCoarseFineDeposition());

      // Copy data back onto the fluid realm.
      m_fluidScratch1.copy(m_particleScratch1);
      DataOps::scale(m_fluidScratch1, Z * Units::Qe / a_dt);
      DataOps::plus(m_EdotJ, m_fluidScratch1, 0, idx, 1);
    }

    computationParticles.clearParticles();
  }
}

#include <CD_NamespaceFooter.H>
