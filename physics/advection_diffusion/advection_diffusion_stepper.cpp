/*!
  @file   advection_diffusion_stepper.cpp
  @brief  Implementation of advection_diffusion_stepper
  @author Robert Marskar
  @data   March 2020
*/

#include <ParmParse.H>

#include "advection_diffusion_stepper.H"
#include "advection_diffusion_species.H"
#include "data_ops.H"

#include "CD_NamespaceHeader.H"
  
using namespace physics::advection_diffusion;



advection_diffusion_stepper::advection_diffusion_stepper(){
  ParmParse pp("advection_diffusion");

  m_phase = phase::gas;

  pp.get("Realm",      m_Realm);
  pp.get("verbosity",  m_verbosity); 
  pp.get("fhd",        m_fhd); 
  pp.get("diffco",     m_faceCenteredDiffusionCoefficient);
  pp.get("omega",      m_omega);
  pp.get("cfl",        m_cfl);
  pp.get("integrator", m_integrator);
}

advection_diffusion_stepper::advection_diffusion_stepper(RefCountedPtr<CdrSolver>& a_solver) : advection_diffusion_stepper() {
  m_solver = a_solver;
}

advection_diffusion_stepper::~advection_diffusion_stepper(){
}

void advection_diffusion_stepper::parseRuntimeOptions() {
  ParmParse pp("advection_diffusion");

  pp.get("verbosity",  m_verbosity);
  pp.get("cfl",        m_cfl);
  pp.get("integrator", m_integrator);

  m_solver->parseRuntimeOptions();
}

void advection_diffusion_stepper::setup_solvers(){
  m_species = RefCountedPtr<cdr_species> (new advection_diffusion_species());

  // Solver setup
  m_solver->setVerbosity(m_verbosity);
  m_solver->setSpecies(m_species);
  m_solver->parseOptions();
  m_solver->setPhase(m_phase);
  m_solver->setAmr(m_amr);
  m_solver->setComputationalGeometry(m_computationalGeometry);
  m_solver->sanityCheck();
  m_solver->setRealm(m_Realm);
  
  if(!m_solver->isMobile() && !m_solver->isDiffusive()){
    MayDay::Abort("advection_diffusion_stepper::setup_solvers - can't turn off both advection AND diffusion");
  }
}

void advection_diffusion_stepper::postInitialize() {
}

void advection_diffusion_stepper::registerRealms() {
  m_amr->registerRealm(m_Realm);
}

void advection_diffusion_stepper::registerOperators(){
  m_solver->registerOperators();
}

void advection_diffusion_stepper::allocate() {
  m_solver->allocateInternals();

  m_amr->allocate(m_tmp, m_Realm, m_phase, 1);
  m_amr->allocate(m_k1,  m_Realm, m_phase, 1);
  m_amr->allocate(m_k2,  m_Realm, m_phase, 1);
}

void advection_diffusion_stepper::initialData(){
  m_solver->initialData();       // Fill initial through the cdr species

  m_solver->setSource(0.0);
  m_solver->setEbFlux(0.0);
  m_solver->setDomainFlux(0.0);
  if(m_solver->isDiffusive()){
    m_solver->setDiffusionCoefficient(m_faceCenteredDiffusionCoefficient);
  }
  if(m_solver->isMobile()){
    this->setVelocity();
  }

}

void advection_diffusion_stepper::setVelocity(){
  const int finest_level = m_amr->getFinestLevel();
  for (int lvl = 0; lvl <= finest_level; lvl++){
    this->setVelocity(lvl);
  }

  EBAMRCellData& vel = m_solver->getCellCenteredVelocity();
  m_amr->averageDown(vel, m_Realm, m_phase);
  m_amr->interpGhost(vel, m_Realm, m_phase);
}

void advection_diffusion_stepper::setVelocity(const int a_level){
  // TLDR: This code goes down to each cell on grid level a_level and sets the velocity to omega*r
    
  const DisjointBoxLayout& dbl = m_amr->getGrids(m_Realm)[a_level];
  for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
    const Box& box = dbl.get(dit());

    EBCellFAB& vel = (*(m_solver->getCellCenteredVelocity())[a_level])[dit()];
    BaseFab<Real>& vel_reg = vel.getSingleValuedFAB();

    vel.setVal(0.0);
      
    // Regular cells
    for (BoxIterator bit(box); bit.ok(); ++bit){
      const IntVect iv = bit();
      const RealVect pos = m_amr->getProbLo() + (RealVect(iv) + 0.5*RealVect::Unit)*m_amr->getDx()[a_level];

      const Real r     = sqrt(pos[0]*pos[0] + pos[1]*pos[1]);
      const Real theta = atan2(pos[1],pos[0]);

      vel_reg(iv,0) = -r*m_omega*sin(theta);
      vel_reg(iv,1) =  r*m_omega*cos(theta);
    }

    // Irregular and multicells
    VoFIterator& vofit = (*m_amr->getVofIterator(m_Realm, m_phase)[a_level])[dit()];
    for (vofit.reset(); vofit.ok(); ++vofit){

      const VolIndex vof = vofit();
      const IntVect iv   = vof.gridIndex();
      const RealVect pos = m_amr->getProbLo() + (RealVect(iv) + 0.5*RealVect::Unit)*m_amr->getDx()[a_level];

      const Real r     = sqrt(pos[0]*pos[0] + pos[1]*pos[1]);
      const Real theta = atan2(pos[1],pos[0]);

      vel(vof,0) = -r*m_omega*sin(theta);
      vel(vof,1) =  r*m_omega*cos(theta);
    }
  }
}

void advection_diffusion_stepper::writeCheckpointData(HDF5Handle& a_handle, const int a_lvl) const {
  m_solver->writeCheckpointLevel(a_handle, a_lvl);
}

void advection_diffusion_stepper::readCheckpointData(HDF5Handle& a_handle, const int a_lvl){
  m_solver->readCheckpointLevel(a_handle, a_lvl);
}

void advection_diffusion_stepper::postCheckpointSetup(){
  m_solver->setSource(0.0);
  m_solver->setEbFlux(0.0);
  m_solver->setDomainFlux(0.0);
  if(m_solver->isDiffusive()){
    m_solver->setDiffusionCoefficient(m_faceCenteredDiffusionCoefficient);
  }
  if(m_solver->isMobile()){
    this->setVelocity();
  }
}

int advection_diffusion_stepper::getNumberOfPlotVariables() const{
  return m_solver->getNumberOfPlotVariables();
}

void advection_diffusion_stepper::writePlotData(EBAMRCellData&       a_output,
						  Vector<std::string>& a_plotVariableNames,
						  int&                 a_icomp) const {
  a_plotVariableNames.append(m_solver->get_plotVariableNames());
  m_solver->writePlotData(a_output, a_icomp);
}

void advection_diffusion_stepper::computeDt(Real& a_dt, TimeCode& a_timeCode){
  if(m_integrator == 0){      // Explicit diffusion. 
    const Real dt = m_solver->computeAdvectionDiffusionDt();
    
    a_dt       = m_cfl*dt;
    a_timeCode = TimeCode::AdvectionDiffusion;
  }
  else if(m_integrator == 1){ // Implicit diffusion
    const Real dt = m_solver->computeAdvectionDt();

    a_dt       = m_cfl*dt;
    a_timeCode = TimeCode::Advection;
  }
  else{
    MayDay::Abort("advection_diffusion_stepper::computeDt - logic bust");
  }
}

Real advection_diffusion_stepper::advance(const Real a_dt){
  EBAMRCellData& state = m_solver->getPhi();
  
  if(m_integrator == 0){ //   Use Heun's method
    m_solver->computeDivJ(m_k1, state, 0.0);
    data_ops::copy(m_tmp, state);
    data_ops::incr(m_tmp, m_k1, -a_dt); // m_tmp = phi - dt*div(J)

    m_solver->computeDivJ(m_k2, m_tmp, 0.0);
    data_ops::incr(state, m_k1, -0.5*a_dt);
    data_ops::incr(state, m_k2, -0.5*a_dt); // Done with deterministic update.

    //m_solver->make_non_negative(state);

    // Add random diffusion flux. This is equivalent to a 1st order. Godunov splitting
    if(m_fhd){
      m_solver->gwnDiffusionSource(m_k1, state); // k1 holds random diffusion
      data_ops::incr(state, m_k1, a_dt);
    }
  
    m_amr->averageDown(state, m_Realm, m_phase);
    m_amr->interpGhost(state, m_Realm, m_phase);
  }
  else if(m_integrator == 1){
    m_solver->computeDivF(m_k1, state, a_dt);
    data_ops::incr(state, m_k1, -a_dt);
    m_amr->averageDown(state, m_Realm, m_phase);

    if(m_solver->isDiffusive()){
      data_ops::copy(m_k2, state); // Now holds phiOld - dt*div(F)
      if(m_fhd){
	m_solver->gwnDiffusionSource(m_k1, state); // k1 holds random diffusion
	data_ops::incr(m_k2, m_k1, a_dt);
      }
      data_ops::set_value(m_k1, 0.0);
      m_solver->advanceEuler(state, m_k2, m_k1, a_dt);
    }

    m_amr->averageDown(state, m_Realm, m_phase);
    m_amr->averageDown(state, m_Realm, m_phase);
  }
  else{
    MayDay::Abort("advection_diffusion_stepper - unknown integrator requested");
  }
  
  return a_dt;
}

void advection_diffusion_stepper::synchronizeSolverTimes(const int a_step, const Real a_time, const Real a_dt){
  m_timeStep = a_step;
  m_time = a_time;
  m_dt   = a_dt;
  
  m_solver->setTime(a_step, a_time, a_dt);
}

void advection_diffusion_stepper::printStepReport(){

}

bool advection_diffusion_stepper::needToRegrid(){
  return false;
}

void advection_diffusion_stepper::preRegrid(const int a_lbase, const int a_oldFinestLevel){
  m_solver->preRegrid(a_lbase, a_oldFinestLevel);
}

void advection_diffusion_stepper::deallocate(){
  m_solver->deallocateInternals();
}

void advection_diffusion_stepper::regrid(const int a_lmin, const int a_oldFinestLevel, const int a_newFinestLevel){

  // Regrid CDR solver
  m_solver->regrid(a_lmin, a_oldFinestLevel, a_newFinestLevel);
  m_solver->setSource(0.0);
  m_solver->setEbFlux(0.0);
  m_solver->setDomainFlux(0.0);
  if(m_solver->isDiffusive()){
    m_solver->setDiffusionCoefficient(m_faceCenteredDiffusionCoefficient);
  }
  if(m_solver->isMobile()){
    this->setVelocity();
  }


  // Allocate memory for RK steps
  m_amr->allocate(m_tmp, m_Realm, m_phase, 1);
  m_amr->allocate(m_k1,  m_Realm, m_phase, 1);
  m_amr->allocate(m_k2,  m_Realm, m_phase, 1);
}

void advection_diffusion_stepper::postRegrid() {
}
#include "CD_NamespaceFooter.H"
