/*!
  @file   mc_photo.cpp
  @brief  Implementation of mc_photo.H
  @author Robert Marskar
  @date   Apr. 2018
*/

#include "mc_photo.H"
#include "data_ops.H"
#include "units.H"
#include "poly.H"
#include "particle_ops.H"

#include <time.h>
#include <chrono>

#include <PolyGeom.H>
#include <EBAlias.H>
#include <EBLevelDataOps.H>
#include <BoxIterator.H>
#include <EBArith.H>
#include <ParmParse.H>
#include <ParticleIO.H>

#define MC_PHOTO_DEBUG 0

#include "CD_NamespaceHeader.H"

mc_photo::mc_photo(){
  m_name       = "mc_photo";
  m_className = "mc_photo";

  m_stationary = false;
}

mc_photo::~mc_photo(){

}

bool mc_photo::advance(const Real a_dt, EBAMRCellData& a_phi, const EBAMRCellData& a_source, const bool a_zerophi){
  CH_TIME("mc_photo::advance");
  if(m_verbosity > 5){
    pout() << m_name + "::advance" << endl;
  }

  // Note: This routine does an on-the-fly generation of photons based on the contents in a_source. Pure particle methods
  //       will probably fill m_source_photons themselves, and this routine will probably not be used with a pure particle
  //       method. If you find yourself calling this routine with a pure particle method, you're probably something wrong. 

  // If stationary, do a safety cleanout first. Then generate new photons
  if(m_instantaneous){
    this->clear(m_photons);
  }

  // Generate new photons and add them to m_photons
  this->clear(m_source_photons);
  this->generate_photons(m_source_photons, a_source, a_dt);
  m_photons.addParticles(m_source_photons);

  // Advance stationary or transient
  if(m_instantaneous){
    this->advance_photons_stationary(m_bulk_photons, m_eb_photons, m_domain_photons, m_photons);
  }
  else{
    this->advance_photons_transient(m_bulk_photons, m_eb_photons, m_domain_photons, m_photons, a_dt);
    this->remap(m_photons);                                       
  }

  // Deposit volumetric photons. 
  this->deposit_photons(a_phi, m_bulk_photons, m_deposition);
  
  return true;
}

bool mc_photo::is_instantaneous(){
  return m_instantaneous;
}

void mc_photo::parseOptions(){
  CH_TIME("mc_photo::parseOptions");
  if(m_verbosity > 5){
    pout() << m_name + "::parseOptions" << endl;
  }
  
  this->parse_rng();
  this->parse_pseudophotons();
  this->parse_photogen();
  this->parse_source_type();
  this->parse_deposition();
  this->parse_bisect_step();
  this->parseDomainBc();
  this->parse_pvr_buffer();
  this->parsePlotVariables();
  this->parse_instantaneous();
  this->parseDivergenceComputation();
}

void mc_photo::parseRuntimeOptions(){
  CH_TIME("mc_photo::parseRuntimeOptions");
  if(m_verbosity > 5){
    pout() << m_name + "::parseRuntimeOptions" << endl;
  }
  
  this->parse_pseudophotons();
  this->parse_photogen();
  this->parse_source_type();
  this->parse_deposition();
  this->parse_bisect_step();
  this->parseDomainBc();
  this->parse_pvr_buffer();
  this->parsePlotVariables();
  this->parse_instantaneous();
  this->parseDivergenceComputation();
}

void mc_photo::parseDivergenceComputation(){
  CH_TIME("mc_photo::parseDivergenceComputation");
  if(m_verbosity > 5){
    pout() << m_name + "::parseDivergenceComputation" << endl;
  }

  ParmParse pp(m_className.c_str());

  pp.get("blend_conservation", m_blendConservation);
}

void mc_photo::parse_rng(){
  CH_TIME("mc_photo::parse_rng");
  if(m_verbosity > 5){
    pout() << m_name + "::parse_rng" << endl;
  }

  // Seed the RNG
  ParmParse pp(m_className.c_str());
  pp.get("seed", m_seed);
  pp.get("poiss_exp_swap", m_poiss_exp_swap);
  if(m_seed < 0) {
    m_seed = std::chrono::system_clock::now().time_since_epoch().count();
    m_seed += procID(); 
  }
  m_rng = new std::mt19937_64(m_seed);

  m_udist01 = new uniform_real_distribution<Real>( 0.0, 1.0);
  m_udist11 = new uniform_real_distribution<Real>(-1.0, 1.0);
}

void mc_photo::parse_instantaneous(){
  CH_TIME("mc_photo::parse_instantaneous");
  if(m_verbosity > 5){
    pout() << m_name + "::parse_instantaneous" << endl;
  }
  
  ParmParse pp(m_className.c_str());

  pp.get("instantaneous", m_instantaneous);
}

void mc_photo::parse_pseudophotons(){
  CH_TIME("mc_photo::parse_pseudophotons");
  if(m_verbosity > 5){
    pout() << m_name + "::parse_pseudophotons" << endl;
  }
  
  ParmParse pp(m_className.c_str());
  
  pp.get("max_photons", m_max_photons);
  if(m_max_photons <= 0){ // = -1 => no restriction
    m_max_photons = 99999999;
  }
}

void mc_photo::parse_photogen(){
  CH_TIME("mc_photo::parse_photogen");
  if(m_verbosity > 5){
    pout() << m_name + "::parse_photogen" << endl;
  }
  
  ParmParse pp(m_className.c_str());

  std::string str;
  pp.get("photon_generation", str);

  if(str == "deterministic"){
    m_photogen = photon_generation::deterministic;
  }
  else if(str == "stochastic"){
    m_photogen = photon_generation::stochastic;
  }
  else{
    MayDay::Abort("mc_photo::set_photon_generation - unknown photon generation type requested");
  }
}

void mc_photo::parse_source_type(){
  CH_TIME("mc_photo::parse_source_type");
  if(m_verbosity > 5){
    pout() << m_name + "::parse_source_type" << endl;
  }
  
  ParmParse pp(m_className.c_str());

  std::string str;
  pp.get("source_type", str);

  if(str == "number"){
    m_src_type = source_type::number;
  }
  else if(str == "volume"){
    m_src_type = source_type::per_vol;
  }
  else if(str == "volume_rate"){
    m_src_type = source_type::per_vols;
  }
  else if(str == "rate"){
    m_src_type = source_type::per_s;
  }
  else{
    MayDay::Abort("mc_photo::setSource_type - unknown source type requested");
  }
}

void mc_photo::parse_deposition(){
  CH_TIME("mc_photo::parse_deposition");
  if(m_verbosity > 5){
    pout() << m_name + "::parse_deposition" << endl;
  }
  
  ParmParse pp(m_className.c_str());

  std::string str;
  pp.get("deposition", str);

  m_deposit_numbers = false;
  if(str == "num"){
    m_deposition = DepositionType::NGP;
    m_deposit_numbers = true;
  }
  else if(str == "ngp"){
    m_deposition = DepositionType::NGP;
  }
  else if(str == "cic"){
    m_deposition = DepositionType::CIC;
  }
  else if(str == "tsc"){
    m_deposition = DepositionType::TSC;
  }
  else if(str == "w4"){
    m_deposition = DepositionType::W4;
  }
  else{
    MayDay::Abort("mc_photo::set_deposition_type - unknown interpolant requested");
  }

  pp.get("plot_deposition", str);
  m_plotNumbers = false;
  if(str == "num"){
    m_plot_deposition = DepositionType::NGP;
    m_plotNumbers = true;
  }
  else if(str == "ngp"){
    m_plot_deposition = DepositionType::NGP;
  }
  else if(str == "cic"){
    m_plot_deposition = DepositionType::CIC;
  }
  else if(str == "tsc"){
    m_plot_deposition = DepositionType::TSC;
  }
  else if(str == "w4"){
    m_plot_deposition = DepositionType::W4;
  }
  else{
    MayDay::Abort("mc_photo::set_deposition_type - unknown interpolant requested");
  }
}

void mc_photo::parse_bisect_step(){
  CH_TIME("mc_photo::parse_bisect_step");
  if(m_verbosity > 5){
    pout() << m_name + "::parse_bisect_step" << endl;
  }
  
  ParmParse pp(m_className.c_str());
  pp.get("bisect_step", m_bisect_step);
}

void mc_photo::parseDomainBc(){
  CH_TIME("mc_photo::parseDomainBc");
  if(m_verbosity > 5){
    pout() << m_name + "::parseDomainBc" << endl;
  }
  
  m_domainbc.resize(2*SpaceDim);
  for (int dir = 0; dir < SpaceDim; dir++){
    for (SideIterator sit; sit.ok(); ++sit){
      const Side::LoHiSide side = sit();
      const int idx = domainbc_map(dir, side);

      ParmParse pp(m_className.c_str());
      std::string str_dir;
      if(dir == 0){
	str_dir = "x";
      }
      else if(dir == 1){
	str_dir = "y";
      }
      else if(dir == 2){
	str_dir = "z";
      }

      std::string sidestr = (side == Side::Lo) ? "_low" : "_high";
      std::string bc_string = "bc_" + str_dir + sidestr;
      std::string type;
      pp.get(bc_string.c_str(), type);
      if(type == "outflow"){
	m_domainbc[idx] = wallbc::outflow;
      }
      else if(type == "symmetry"){
	m_domainbc[idx] = wallbc::symmetry;
      }
      else if(type == "wall"){
	m_domainbc[idx] = wallbc::wall;
      }
      else {
	std::string error = "mc_photo::setDomainBc - unsupported boundary condition requested: " + bc_string;
	MayDay::Abort(error.c_str());
      }
    }
  }
}

void mc_photo::parse_pvr_buffer(){
  CH_TIME("mc_photo::parse_pvr_buffer");
  if(m_verbosity > 5){
    pout() << m_name + "::parse_pvr_buffer" << endl;
  }
  
  ParmParse pp(m_className.c_str());
  pp.get("pvr_buffer",   m_pvr_buffer);
  pp.get("halo_buffer",  m_halo_buffer);
}

void mc_photo::parsePlotVariables(){
  CH_TIME("mc_photo::parsePlotVariables");
  if(m_verbosity > 5){
    pout() << m_name + "::parsePlotVariables" << endl;
  }

  m_plotPhi       = false;
  m_plotSource       = false;
  m_plot_phot      = false;
  m_plot_bulk_phot = false;
  m_plot_eb_phot   = false;
  m_plot_dom_phot  = false;
  m_plotSource_phot  = false;

  ParmParse pp(m_className.c_str());
  const int num = pp.countval("plt_vars");
  Vector<std::string> str(num);
  pp.getarr("plt_vars", str, 0, num);

  for (int i = 0; i < num; i++){
    if(     str[i] == "phi")       m_plotPhi       = true;
    else if(str[i] == "src")       m_plotSource       = true;
    else if(str[i] == "phot")      m_plot_phot      = true;
    else if(str[i] == "bulk_phot") m_plot_bulk_phot = true;
    else if(str[i] == "eb_phot")   m_plot_eb_phot   = true;
    else if(str[i] == "dom_phot")  m_plot_dom_phot  = true;
    else if(str[i] == "src_phot")  m_plotSource_phot  = true;
  }
}

void mc_photo::clear(){
  CH_TIME("mc_photo::clear()");
  if(m_verbosity > 5){
    pout() << m_name + "::clear()" << endl;
  }

  this->clear(m_photons);
}

void mc_photo::clear(ParticleContainer<photon>& a_photon){
  CH_TIME("mc_photo::clear(ParticleContainer)");
  if(m_verbosity > 5){
    pout() << m_name + "::clear(ParticleContainer)" << endl;
  }

  this->clear(a_photon.getParticles());
}

void mc_photo::clear(AMRParticles<photon>& a_photons){
  CH_TIME("mc_photo::clear");
  if(m_verbosity > 5){
    pout() << m_name + "::clear" << endl;
  }

  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++){
    a_photons[lvl]->clear();
  }
}
  
void mc_photo::allocateInternals(){
  CH_TIME("mc_photo::allocateInternals");
  if(m_verbosity > 5){
    pout() << m_name + "::allocateInternals" << endl;
  }

  const int ncomp  = 1;

  // Allocate mesh data
  m_amr->allocate(m_phi,        m_realm, m_phase, ncomp); 
  m_amr->allocate(m_source,       m_realm, m_phase, ncomp);
  m_amr->allocate(m_scratch,      m_realm, m_phase, ncomp);
  m_amr->allocate(m_depositionNC, m_realm, m_phase, ncomp);
  m_amr->allocate(m_massDiff,     m_realm, m_phase, ncomp);

  // Allocate particle data holders
  m_amr->allocate(m_photons,        m_pvr_buffer, m_realm);
  m_amr->allocate(m_bulk_photons,   m_pvr_buffer, m_realm);
  m_amr->allocate(m_eb_photons,     m_pvr_buffer, m_realm);
  m_amr->allocate(m_domain_photons, m_pvr_buffer, m_realm);
  m_amr->allocate(m_source_photons, m_pvr_buffer, m_realm);
}

void mc_photo::preRegrid(const int a_lmin, const int a_oldFinestLevel){
  CH_TIME("mc_photo::preRegrid");
  if(m_verbosity > 5){
    pout() << m_name + "::pre_grid" << endl;
  }

  m_photons.preRegrid(a_lmin);         // TLDR: This moves photons from l >= a_lmin to Max(a_lmin-1,0)
  m_bulk_photons.preRegrid(a_lmin);    // TLDR: This moves photons from l >= a_lmin to Max(a_lmin-1,0)
  m_eb_photons.preRegrid(a_lmin);      // TLDR: This moves photons from l >= a_lmin to Max(a_lmin-1,0)
  m_domain_photons.preRegrid(a_lmin);  // TLDR: This moves photons from l >= a_lmin to Max(a_lmin-1,0)
  m_source_photons.preRegrid(a_lmin);  // TLDR: This moves photons from l >= a_lmin to Max(a_lmin-1,0)
}

void mc_photo::deallocateInternals(){
  CH_TIME("mc_photo::deallocateInternals");
  if(m_verbosity > 5){ 
    pout() << m_name + "::deallocateInternals" << endl;
  }

  // Don't deallocate, instead, reallocate. 
  // m_amr->deallocate(m_phi);
  // m_amr->deallocate(m_source);
  // m_amr->deallocate(m_scratch);
  // m_amr->deallocate(m_depositionNC);
  // m_amr->deallocate(m_massDiff);
}

void mc_photo::regrid(const int a_lmin, const int a_oldFinestLevel, const int a_newFinestLevel){
  CH_TIME("mc_photo::regrid");
  if(m_verbosity > 5){ 
    pout() << m_name + "::regrid" << endl;
  }

  // Mesh data regrids
  m_amr->reallocate(m_phi,        m_phase, a_lmin); 
  m_amr->reallocate(m_source,       m_phase, a_lmin);
  m_amr->reallocate(m_scratch,      m_phase, a_lmin);
  m_amr->reallocate(m_depositionNC, m_phase, a_lmin);
  m_amr->reallocate(m_massDiff,     m_phase, a_lmin);

  // Particle data regrids
  const Vector<DisjointBoxLayout>& grids = m_amr->getGrids(m_realm);
  const Vector<ProblemDomain>& domains   = m_amr->getDomains();
  const Vector<Real>& dx                 = m_amr->getDx();
  const Vector<int>& ref_rat             = m_amr->getRefinementRatios();

  m_photons.regrid(       grids, domains, dx, ref_rat, a_lmin, a_newFinestLevel);
  m_bulk_photons.regrid(  grids, domains, dx, ref_rat, a_lmin, a_newFinestLevel);
  m_eb_photons.regrid(    grids, domains, dx, ref_rat, a_lmin, a_newFinestLevel);
  m_domain_photons.regrid(grids, domains, dx, ref_rat, a_lmin, a_newFinestLevel);
  m_source_photons.regrid(grids, domains, dx, ref_rat, a_lmin, a_newFinestLevel);

  // Deposit
  this->deposit_photons();
}

void mc_photo::sort_photons_by_cell(){
  CH_TIME("mc_photo::sort_photons_by_cell()");
  if(m_verbosity > 5){
    pout() << m_name + "::sort_photons_by_cell()" << endl;
  }

  m_photons.sortParticlesByCell();
}

void mc_photo::sort_photons_by_patch(){
  CH_TIME("mc_photo::sort_photons_by_patch()");
  if(m_verbosity > 5){
    pout() << m_name + "::sort_photons_by_patch()" << endl;
  }

  m_photons.sortParticlesByPatch();
}

void mc_photo::sort_bulk_photons_by_cell(){
  CH_TIME("mc_photo::sort_bulk_photons_by_cell()");
  if(m_verbosity > 5){
    pout() << m_name + "::sort_bulk_photons_by_cell()" << endl;
  }

  m_bulk_photons.sortParticlesByCell();
}

void mc_photo::sort_bulk_photons_by_patch(){
  CH_TIME("mc_photo::sort_bulk_photons_by_patch()");
  if(m_verbosity > 5){
    pout() << m_name + "::sort_bulk_photons_by_patch()" << endl;
  }

  m_bulk_photons.sortParticlesByPatch();
}

void mc_photo::sort_source_photons_by_cell(){
  CH_TIME("mc_photo::sort_source_photons_by_cell()");
  if(m_verbosity > 5){
    pout() << m_name + "::sort_source_photons_by_cell()" << endl;
  }

  m_source_photons.sortParticlesByCell();
}

void mc_photo::sort_source_photons_by_patch(){
  CH_TIME("mc_photo::sort_source_photons_by_patch()");
  if(m_verbosity > 5){
    pout() << m_name + "::sort_source_photons_by_patch()" << endl;
  }

  m_source_photons.sortParticlesByPatch();
}

void mc_photo::sort_domain_photons_by_cell(){
  CH_TIME("mc_photo::sort_domain_photons_by_cell()");
  if(m_verbosity > 5){
    pout() << m_name + "::sort_domain_photons_by_cell()" << endl;
  }

  m_domain_photons.sortParticlesByCell();
}

void mc_photo::sort_domain_photons_by_patch(){
  CH_TIME("mc_photo::sort_domain_photons_by_patch()");
  if(m_verbosity > 5){
    pout() << m_name + "::sort_domain_photons_by_patch()" << endl;
  }

  m_domain_photons.sortParticlesByPatch();
}

void mc_photo::sort_eb_photons_by_cell(){
  CH_TIME("mc_photo::sort_eb_photons_by_cell()");
  if(m_verbosity > 5){
    pout() << m_name + "::sort_eb_photons_by_cell()" << endl;
  }

  m_eb_photons.sortParticlesByCell();
}

void mc_photo::sort_eb_photons_by_patch(){
  CH_TIME("mc_photo::sort_eb_photons_by_patch()");
  if(m_verbosity > 5){
    pout() << m_name + "::sort_eb_photons_by_patch()" << endl;
  }

  m_eb_photons.sortParticlesByPatch();
}

void mc_photo::registerOperators(){
  CH_TIME("mc_photo::registerOperators");
  if(m_verbosity > 5){
    pout() << m_name + "::registerOperators" << endl;
  }

  if(m_amr.isNull()){
    MayDay::Abort("mc_photo::registerOperators - need to set AmrMesh!");
  }
  else{
    m_amr->registerOperator(s_eb_coar_ave,     m_realm, m_phase);
    m_amr->registerOperator(s_eb_fill_patch,   m_realm, m_phase);
    m_amr->registerOperator(s_eb_mg_interp,    m_realm, m_phase);
    m_amr->registerOperator(s_eb_redist,       m_realm, m_phase);
    m_amr->registerOperator(s_noncons_div,  m_realm, m_phase);
    m_amr->registerOperator(s_eb_copier,       m_realm, m_phase);
    if(m_pvr_buffer <= 0){
      m_amr->registerOperator(s_eb_ghostcloud, m_realm, m_phase);
    }
    if(m_halo_buffer > 0){
      m_amr->registerMask(s_particle_halo, m_halo_buffer, m_realm);
    }
  }
}

void mc_photo::compute_boundary_flux(EBAMRIVData& a_ebFlux, const EBAMRCellData& a_phi){
  CH_TIME("mc_photo::compute_boundary_flux");
  if(m_verbosity > 5){
    pout() << m_name + "::compute_boundary_flux" << endl;
  }

  data_ops::set_value(a_ebFlux, 0.0);
}

void mc_photo::compute_domain_flux(EBAMRIFData& a_domainflux, const EBAMRCellData& a_phi){
  CH_TIME("mc_photo::compute_domain_flux");
  if(m_verbosity > 5){
    pout() << m_name + "::compute_domain_flux" << endl;
  }
  
  data_ops::set_value(a_domainflux, 0.0);
}

void mc_photo::computeFlux(EBAMRCellData& a_flux, const EBAMRCellData& a_phi){
  const std::string str = "mc_photo::computeFlux - Fluid flux can't be computed with discrete photons. Calling this is an error";
  MayDay::Abort(str.c_str());
}

void mc_photo::compute_density(EBAMRCellData& a_isotropic, const EBAMRCellData& a_phi){
  MayDay::Abort("mc_photo::compute_density - Calling this is an error");
}

void mc_photo::writePlotFile(){
  CH_TIME("mc_photo::writePlotFile");
  if(m_verbosity > 5){
    pout() << m_name + "::writePlotFile" << endl;
  }

  MayDay::Abort("mc_photo::writePlotFile - not implemented");
}

void mc_photo::writeCheckpointLevel(HDF5Handle& a_handle, const int a_level) const {
  CH_TIME("mc_photo::writeCheckpointLevel");
  if(m_verbosity > 5){
    pout() << m_name + "::writeCheckpointLevel" << endl;
  }

  // Write state vector
  write(a_handle, *m_phi[a_level], m_name);

  // Write particles. Must be implemented.
  std::string str = m_name + "_particles";
  writeParticlesToHDF(a_handle, m_photons[a_level], str);
}

void mc_photo::readCheckpointLevel(HDF5Handle& a_handle, const int a_level){
  CH_TIME("mc_photo::readCheckpointLevel");
  if(m_verbosity > 5){
    pout() << m_name + "::readCheckpointLevel" << endl;
  }

  // Read state vector
  read<EBCellFAB>(a_handle, *m_phi[a_level], m_name, m_amr->getGrids(m_realm)[a_level], Interval(0,0), false);

  // Read particles. Should be implemented
  std::string str = m_name + "_particles";
  readParticlesFromHDF(a_handle, m_photons[a_level], str);
}

Vector<std::string> mc_photo::getPlotVariableNames() const {
  CH_TIME("mc_photo::getPlotVariableNames");
  if(m_verbosity > 5){
    pout() << m_name + "::getPlotVariableNames" << endl;
  }
  
  Vector<std::string> names(0);
  
  if(m_plotPhi)       names.push_back(m_name + " phi");
  if(m_plotSource)       names.push_back(m_name + " source");
  if(m_plot_phot)      names.push_back(m_name + " photons");
  if(m_plot_bulk_phot) names.push_back(m_name + " bulk_photons");
  if(m_plot_eb_phot)   names.push_back(m_name + " eb_photons");
  if(m_plot_dom_phot)  names.push_back(m_name + " domain_photons");
  if(m_plotSource_phot)  names.push_back(m_name + " source_photons");

  return names;
}

int mc_photo::getNumberOfPlotVariables() const{
  CH_TIME("mc_photo::getNumberOfPlotVariables");
  if(m_verbosity > 5){
    pout() << m_name + "::getNumberOfPlotVariables" << endl;
  }

  int num_output = 0;

  if(m_plotPhi)       num_output = num_output + 1;
  if(m_plotSource)       num_output = num_output + 1;
  if(m_plot_phot)      num_output = num_output + 1;
  if(m_plot_bulk_phot) num_output = num_output + 1;
  if(m_plot_eb_phot)   num_output = num_output + 1;
  if(m_plot_dom_phot)  num_output = num_output + 1;
  if(m_plotSource_phot)  num_output = num_output + 1;

  return num_output;
}

int mc_photo::queryGhost() const {
  return 3;
}

int mc_photo::random_fieldSolver(const Real a_mean){
  if(a_mean < m_poiss_exp_swap){
    std::poisson_distribution<int> pdist(a_mean);
    return pdist(*m_rng);
  }
  else {
    std::normal_distribution<Real> ndist(a_mean, sqrt(a_mean));
    return (int) round(ndist(*m_rng));
  }
}

int mc_photo::domainbc_map(const int a_dir, const Side::LoHiSide a_side) {
  const int iside = (a_side == Side::Lo) ? 0 : 1;

  return 2*a_dir + iside;
}

Real mc_photo::random_exponential(const Real a_mean){
  std::exponential_distribution<Real> dist(a_mean);
  return dist(*m_rng);
}

RealVect mc_photo::random_direction(){
#if CH_SPACEDIM == 2
  return random_direction2D();
#else
  return random_direction3D();
#endif
}

#if CH_SPACEDIM == 2
RealVect mc_photo::random_direction2D(){
  const Real EPS = 1.E-8;
  Real x1 = 2.0;
  Real x2 = 2.0;
  Real r  = x1*x1 + x2*x2;
  while(r >= 1.0 || r < EPS){
    x1 = (*m_udist11)(*m_rng);
    x2 = (*m_udist11)(*m_rng);
    r  = x1*x1 + x2*x2;
  }

  return RealVect(x1,x2)/sqrt(r);
}
#endif

#if CH_SPACEDIM==3
RealVect mc_photo::random_direction3D(){
  const Real EPS = 1.E-8;
  Real x1 = 2.0;
  Real x2 = 2.0;
  Real r  = x1*x1 + x2*x2;
  while(r >= 1.0 || r < EPS){
    x1 = (*m_udist11)(*m_rng);
    x2 = (*m_udist11)(*m_rng);
    r  = x1*x1 + x2*x2;
  }

  const Real x = 2*x1*sqrt(1-r);
  const Real y = 2*x2*sqrt(1-r);
  const Real z = 1 - 2*r;

  return RealVect(x,y,z);
}
#endif

int mc_photo::getPVR_buffer() const {
  CH_TIME("mc_photo::getPVR_buffer");
  if(m_verbosity > 5){
    pout() << m_name + "::getPVR_buffer" << endl;
  }

  return m_pvr_buffer;
}

int mc_photo::get_halo_buffer() const {
  CH_TIME("mc_photo::get_halo_buffer");
  if(m_verbosity > 5){
    pout() << m_name + "::get_halo_buffer" << endl;
  }

  return m_halo_buffer;
}

void mc_photo::set_pvr_buffer(const int a_buffer) {
  CH_TIME("mc_photo::set_pvr_buffer");
  if(m_verbosity > 5){
    pout() << m_name + "::set_pvr_buffer" << endl;
  }

  m_pvr_buffer = a_buffer;
}

void mc_photo::set_halo_buffer(const int a_buffer)  {
  CH_TIME("mc_photo::set_halo_buffer");
  if(m_verbosity > 5){
    pout() << m_name + "::set_halo_buffer" << endl;
  }

  m_halo_buffer = a_buffer;
}

void mc_photo::generate_photons(ParticleContainer<photon>& a_photons, const EBAMRCellData& a_source, const Real a_dt){
  CH_TIME("mc_photo::generate_photons");
  if(m_verbosity > 5){
    pout() << m_name + "::generate_photons" << endl;
  }

  const RealVect prob_lo = m_amr->getProbLo();
  const int finest_level = m_amr->getFinestLevel();

  // Put here. 
  AMRParticles<photon>& photons = a_photons.getParticles();

  for (int lvl = 0; lvl <= finest_level; lvl++){
    const DisjointBoxLayout& dbl = m_amr->getGrids(m_realm)[lvl];
    const ProblemDomain& dom     = m_amr->getDomains()[lvl];
    const Real dx                = m_amr->getDx()[lvl];
    const Real vol               = pow(dx, SpaceDim);
    const bool has_coar          = (lvl > 0);

    // If there is a coarser level, remove particles from the overlapping region and regenerate on this level.
    if(has_coar) {
      const AMRPVR& pvr = a_photons.getPVR();
      const int ref_ratio = m_amr->getRefinementRatios()[lvl-1];
      collectValidParticles(photons[lvl]->outcast(),
			    *photons[lvl-1],
			    pvr[lvl]->mask(),
			    dx*RealVect::Unit,
			    ref_ratio,
			    false,
			    prob_lo);
      photons[lvl]->outcast().clear(); 
    }

    // Create new particles on this level. 
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const Box box          = dbl.get(dit());
      const EBISBox& ebisbox = (*a_source[lvl])[dit()].getEBISBox();
      const EBGraph& ebgraph = ebisbox.getEBGraph();
      const IntVectSet ivs   = IntVectSet(box);

      const EBCellFAB& source = (*a_source[lvl])[dit()];
      const FArrayBox& srcFAB = source.getFArrayBox();

      Real sum = srcFAB.sum(0);
      
      // Generate new particles in this box
      List<photon> particles;
      if(sum > 0){
	
	// Regular cells
	for (BoxIterator bit(box); bit.ok(); ++bit){
	  const IntVect iv   = bit();

	  if(ebisbox.isRegular(iv)){
	    const RealVect pos = prob_lo + (RealVect(iv) + 0.5*RealVect::Unit)*dx;

	    // Number of physical photons
	    const int num_phys_photons = this->draw_photons(srcFAB(iv,0), vol, a_dt);

	    // Make superphotons if we have to
	    if(num_phys_photons > 0){
	      const int num_photons = (num_phys_photons <= m_max_photons) ? num_phys_photons : m_max_photons;
	      const Real weight     = (1.0*num_phys_photons)/num_photons; 
	      
	      for (int i = 0; i < num_photons; i++){
		const RealVect v = units::s_c0*this->random_direction();
		particles.append(photon(pos, v, m_rte_species->get_kappa(pos), weight));
	      }
	    }
	  }
	}

	// Irregular and multicells
	VoFIterator& vofit = (*m_amr->getVofIterator(m_realm, m_phase)[lvl])[dit()];
	for (vofit.reset(); vofit.ok(); ++vofit){
	  const VolIndex& vof = vofit();
	  const RealVect pos  = EBArith::getVofLocation(vof, dx*RealVect::Unit, prob_lo);
	  const Real kappa    = ebisbox.volFrac(vof);

	  const int num_phys_photons = draw_photons(source(vof,0), vol, a_dt);
	  
	  if(num_phys_photons > 0){
	    const int num_photons = (num_phys_photons <= m_max_photons) ? num_phys_photons : m_max_photons;
	    const Real weight      = (1.0*num_phys_photons)/num_photons;

	    // Generate computational photons 
	    for (int i = 0; i < num_photons; i++){
	      const RealVect v = units::s_c0*this->random_direction();
	      particles.append(photon(pos, v, m_rte_species->get_kappa(pos), weight));
	    }
	  }

	}
      }

      // Add new particles to data holder
      (*photons[lvl])[dit()].addItemsDestructive(particles);
    }
  }
}

int mc_photo::draw_photons(const Real a_source, const Real a_volume, const Real a_dt){
  CH_TIME("mc_photo::draw_photons");
  if(m_verbosity > 5){
    pout() << m_name + "::draw_photons" << endl;
  }

  int num_photons = 0;

  // Check if we need any type of source term normalization
  Real factor;
  if(m_src_type == source_type::number){
    factor = 1.0;
  }
  else if(m_src_type == source_type::per_vol){
    factor = a_volume;
  }
  else if(m_src_type == source_type::per_vols){
    factor = a_volume*a_dt;
  }
  else if(m_src_type == source_type::per_s){
    factor = a_dt;
  }

  // Draw a number of photons with the desired algorithm
  if(m_photogen == photon_generation::stochastic){
    const Real mean = a_source*factor;
    num_photons = random_fieldSolver(mean);
  }
  else if(m_photogen == photon_generation::deterministic){
    num_photons = round(a_source*factor);
  }
  else{
    MayDay::Abort("mc::draw_photons - unknown generation requested. Aborting...");
  }

  return num_photons;
}

void mc_photo::deposit_photons(){
  CH_TIME("mc_photo::deposit_photons");
  if(m_verbosity > 5){
    pout() << m_name + "::deposit_photons" << endl;
  }

  this->deposit_photons(m_phi, m_photons, m_deposition);
}

void mc_photo::deposit_photons(EBAMRCellData&                    a_phi,
			       const ParticleContainer<photon>& a_photons,
			       const DepositionType::Which&      a_deposition){
  CH_TIME("mc_photo::deposit_photons(ParticleContainer)");
  if(m_verbosity > 5){
    pout() << m_name + "::deposit_photons(ParticleContainer)" << endl;
  }

  this->deposit_photons(a_phi, a_photons.getParticles(), a_deposition);
}

void mc_photo::deposit_photons(EBAMRCellData&               a_phi,
			       const AMRParticles<photon>&  a_photons,
			       const DepositionType::Which& a_deposition){
  CH_TIME("mc_photo::deposit_photons(AMRParticles)");
  if(m_verbosity > 5){
    pout() << m_name + "::deposit_photons(AMRParticles)" << endl;
  }

           
  this->deposit_kappaConservative(a_phi, a_photons, a_deposition); // a_phi contains only weights, i.e. not divided by kappa
  this->deposit_nonConservative(m_depositionNC, a_phi);              // Compute m_depositionNC = sum(kappa*Wc)/sum(kappa)
  this->deposit_hybrid(a_phi, m_massDiff, m_depositionNC);           // Compute hybrid deposition, including mass differnce
  this->incrementRedist(m_massDiff);                                 // Increment level redistribution register

  // Do the redistribution magic
  const bool ebcf = m_amr->getEbCf();
  if(ebcf){ // Mucho stuff to do here...
    this->coarseFineIncrement(m_massDiff);       // Compute C2F, F2C, and C2C mass transfers
    this->level_redistribution(a_phi);           // Level redistribution. Weights is a dummy parameter
    this->coarseFineRedistribution(a_phi);     // Do the coarse-fine redistribution
  }
  else{ // Very simple, redistribute this level.
    this->level_redistribution(a_phi);
  }

  // Average down and interpolate
  m_amr->averageDown(a_phi, m_realm, m_phase);
  m_amr->interpGhost(a_phi, m_realm, m_phase);
}

void mc_photo::deposit_kappaConservative(EBAMRCellData&              a_phi,
					 const AMRParticles<photon>& a_photons,
					 const DepositionType::Which a_deposition){
  CH_TIME("mc_photo::deposit_kappaConservative");
  if(m_verbosity > 5){
    pout() << m_name + "::deposit_kappaConservative" << endl;
  }

  const int comp = 0;
  const Interval interv(comp, comp);

  const RealVect origin  = m_amr->getProbLo();
  const int finest_level = m_amr->getFinestLevel();

  data_ops::set_value(a_phi,    0.0);
  data_ops::set_value(m_scratch,  0.0);

  for (int lvl = 0; lvl <= finest_level; lvl++){
    const Real dx                = m_amr->getDx()[lvl];
    const DisjointBoxLayout& dbl = m_amr->getGrids(m_realm)[lvl];
    const ProblemDomain& dom     = m_amr->getDomains()[lvl];
    const EBISLayout& ebisl      = m_amr->getEBISLayout(m_realm, m_phase)[lvl];
    const RefCountedPtr<EBLevelGrid>& eblg = m_amr->getEBLevelGrid(m_realm, m_phase)[lvl];

    const bool has_coar = (lvl > 0);
    const bool has_fine = (lvl < finest_level);

    // 1. If we have a coarser level whose cloud extends beneath this level, interpolate that result here first. 
    if(has_coar && m_pvr_buffer > 0){
      RefCountedPtr<EBMGInterp>& interp = m_amr->getEBMGInterp(m_realm, m_phase)[lvl];
      interp->pwcInterp(*a_phi[lvl], *m_scratch[lvl-1], interv);
    }
    
    // 2. Deposit this levels photons. Note that this will deposit into ghost cells, which must later
    //    be added to neighboring patches
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const Box box          = dbl.get(dit());
      const EBISBox& ebisbox = ebisl[dit()];
      EbParticleInterp interp(box, ebisbox, dx*RealVect::Unit, origin, true);
      interp.deposit((*a_photons[lvl])[dit()].listItems(), (*a_phi[lvl])[dit()].getFArrayBox(), m_deposition);
    }

    // This code adds contributions from ghost cells into the valid region
    const RefCountedPtr<Copier>& reversecopier = m_amr->getReverseCopier(m_realm, m_phase)[lvl];
    LDaddOp<FArrayBox> addOp;
    LevelData<FArrayBox> aliasFAB;
    aliasEB(aliasFAB, *a_phi[lvl]);
    aliasFAB.exchange(Interval(0,0), *reversecopier, addOp);

    // 3. If we have a finer level, copy contributions from this level to the temporary holder that is used for
    //    interpolation of "hanging clouds"
    if(has_fine){
      a_phi[lvl]->localCopyTo(*m_scratch[lvl]);
    }
    else if(m_pvr_buffer <= 0 && has_coar){
      EbGhostCloud& ghostcloud = *(m_amr->getGhostCloud(m_realm, m_phase)[lvl]);
      ghostcloud.addFineGhostsToCoarse(*a_phi[lvl-1], *a_phi[lvl]);
    }
  }
}

void mc_photo::deposit_nonConservative(EBAMRIVData& a_depositionNC, const EBAMRCellData& a_depositionKappaC){
  CH_TIME("mc_photo::deposit_nonConservative");
  if(m_verbosity > 5){
    pout() << m_name + "::deposit_nonConservative" << endl;
  }

  if(m_blendConservation){
    const IrregAmrStencil<NonConservativeDivergenceStencil>& stencils = m_amr->getNonConservativeDivergenceStencils(m_realm, m_phase);
    stencils.apply(a_depositionNC, a_depositionKappaC);
  }
  else{
    data_ops::set_value(a_depositionNC, 0.0);
  }
}

void mc_photo::deposit_hybrid(EBAMRCellData& a_depositionH, EBAMRIVData& a_massDifference, const EBAMRIVData& a_depositionNC){
  CH_TIME("mc_photo::deposit_hybrid");
  if(m_verbosity > 5){
    pout() << m_name + "::deposit_hybrid" << endl;
  }

  const int comp  = 0;
  const int ncomp = 1;

  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++){
    const DisjointBoxLayout& dbl = m_amr->getGrids(m_realm)[lvl];
    const ProblemDomain& domain  = m_amr->getDomains()[lvl];
    const EBISLayout& ebisl      = m_amr->getEBISLayout(m_realm, m_phase)[lvl];
    
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      EBCellFAB& divH               = (*a_depositionH[lvl])[dit()];  // On input, this contains kappa*depositionWeights
      BaseIVFAB<Real>& deltaM       = (*a_massDifference[lvl])[dit()];
      const BaseIVFAB<Real>& divNC  = (*a_depositionNC[lvl])[dit()]; 

      const Box box          = dbl.get(dit());
      const EBISBox& ebisbox = ebisl[dit()];
      const EBGraph& ebgraph = ebisbox.getEBGraph();
      const IntVectSet ivs   = ebisbox.getIrregIVS(box);

      VoFIterator& vofit = (*m_amr->getVofIterator(m_realm, m_phase)[lvl])[dit()];
      for (vofit.reset(); vofit.ok(); ++vofit){
	const VolIndex& vof = vofit();
	const Real kappa    = ebisbox.volFrac(vof);
	const Real dc       = divH(vof, comp);
	const Real dnc      = divNC(vof, comp);

	// Note that if dc - kappa*dnc can be negative, i.e. we may end up STEALING mass
	// from other cells. This is why there is a flag m_blendConservation which always
	// gives positive definite results. 
	divH(vof, comp)   = dc + (1-kappa)*dnc;          // On output, contains hybrid divergence
	deltaM(vof, comp) = (1-kappa)*(dc - kappa*dnc);  // Remember, dc already scaled by kappa. 
      }
    }
  }
}

void mc_photo::incrementRedist(const EBAMRIVData& a_massDifference){
  CH_TIME("mc_photo::incrementRedist");
  if(m_verbosity > 5){
    pout() << m_name + "::incrementRedist" << endl;
  }

  const int comp  = 0;
  const int ncomp = 1;
  const int finest_level = m_amr->getFinestLevel();
  const Interval interv(comp, comp);

  for (int lvl = 0; lvl <= finest_level; lvl++){
    const DisjointBoxLayout& dbl = m_amr->getGrids(m_realm)[lvl];
    
    EBLevelRedist& level_redist = *(m_amr->getLevelRedist(m_realm, m_phase)[lvl]);
    level_redist.setToZero();

    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      level_redist.increment((*a_massDifference[lvl])[dit()], dit(), interv);
    }
  }
}

void mc_photo::level_redistribution(EBAMRCellData& a_phi){
  CH_TIME("mc_photo::level_redistribution");
  if(m_verbosity > 5){
    pout() << m_name + "::level_redistribution" << endl;
  }

  const int comp  = 0;
  const int ncomp = 1;
  const int finest_level = m_amr->getFinestLevel();
  const Interval interv(comp, comp);

  for (int lvl = 0; lvl <= finest_level; lvl++){
    EBLevelRedist& level_redist = *(m_amr->getLevelRedist(m_realm, m_phase)[lvl]);
    level_redist.redistribute(*a_phi[lvl], interv);
    level_redist.setToZero();
  }
}

void mc_photo::coarseFineIncrement(const EBAMRIVData& a_massDifference){
  CH_TIME("mc_photo::coarseFineIncrement");
  if(m_verbosity > 5){
    pout() << m_name + "::coarseFineIncrement" << endl;
  }

  const int comp  = 0;
  const int ncomp = 1;
  const int finest_level = m_amr->getFinestLevel();
  const Interval interv(0,0);

  for (int lvl = 0; lvl <= finest_level; lvl++){
    const DisjointBoxLayout& dbl = m_amr->getGrids(m_realm)[lvl];

    RefCountedPtr<EBFineToCoarRedist>& fine2coar_redist = m_amr->getFineToCoarRedist(m_realm, m_phase)[lvl];
    RefCountedPtr<EBCoarToFineRedist>& coar2fine_redist = m_amr->getCoarToFineRedist(m_realm, m_phase)[lvl];
    RefCountedPtr<EBCoarToCoarRedist>& coar2coar_redist = m_amr->getCoarToCoarRedist(m_realm, m_phase)[lvl];

    const bool has_coar = lvl > 0;
    const bool has_fine = lvl < 0;

    if(has_coar){
      fine2coar_redist->setToZero();

    }
    if(has_fine){
      coar2fine_redist->setToZero();
      coar2coar_redist->setToZero();
    }

    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      if(has_coar){
	fine2coar_redist->increment((*a_massDifference[lvl])[dit()], dit(), interv);
      }

      if(has_fine){
	coar2fine_redist->increment((*a_massDifference[lvl])[dit()], dit(), interv);
	coar2coar_redist->increment((*a_massDifference[lvl])[dit()], dit(), interv);
      }
    }
  }
}

void mc_photo::coarseFineRedistribution(EBAMRCellData& a_phi){
  CH_TIME("mc_photo::coarseFineRedistribution");
  if(m_verbosity > 5){
    pout() << m_name + "::coarseFineRedistribution" << endl;
  }

  const int comp         = 0;
  const int ncomp        = 1;
  const int finest_level = m_amr->getFinestLevel();
  const Interval interv(comp, comp);

  for (int lvl = 0; lvl <= finest_level; lvl++){
    const Real dx       = m_amr->getDx()[lvl];
    const bool has_coar = lvl > 0;
    const bool has_fine = lvl < finest_level;

    RefCountedPtr<EBCoarToFineRedist>& coar2fine_redist = m_amr->getCoarToFineRedist(m_realm, m_phase)[lvl];
    RefCountedPtr<EBCoarToCoarRedist>& coar2coar_redist = m_amr->getCoarToCoarRedist(m_realm, m_phase)[lvl];
    RefCountedPtr<EBFineToCoarRedist>& fine2coar_redist = m_amr->getFineToCoarRedist(m_realm, m_phase)[lvl];
    
    if(has_coar){
      fine2coar_redist->redistribute(*a_phi[lvl-1], interv);
      fine2coar_redist->setToZero();
    }

    if(has_fine){
      coar2fine_redist->redistribute(*a_phi[lvl+1], interv);
      coar2coar_redist->redistribute(*a_phi[lvl],   interv);

      coar2fine_redist->setToZero();
      coar2coar_redist->setToZero();
    }
  }
}

void mc_photo::advance_photons_stationary(ParticleContainer<photon>& a_bulk_photons,
					  ParticleContainer<photon>& a_eb_photons,
					  ParticleContainer<photon>& a_domain_photons,
					  ParticleContainer<photon>& a_photons){
  CH_TIME("mc_photo::advance_photons_stationary");
  if(m_verbosity > 5){
    pout() << m_name + "::advance_photons_stationary" << endl;
  }

  // TLDR: This routine iterates over the levels and boxes and does the following
  //
  //       Forall photons in a_photons: {
  //          1. Draw random absorption position
  //          2. Check if path intersects boundary, either EB or domain
  //          3. Move the photon to appropriate data holder:
  //                 Path crossed EB   => a_eb_photons
  //                 Path cross domain => a_domain_photons
  //                 Absorbe in bulk   => a_bulk_photons
  //       }
  //
  //       Remap a_bulk_photons, a_eb_photons, a_domain_photons

  // Low and high corners
  const RealVect prob_lo = m_amr->getProbLo();
  const RealVect prob_hi = m_amr->getProbHi();

  // Safety factor
  const Real SAFETY = 1.E-6;

  // This is the implicit function used for intersection tests
  RefCountedPtr<BaseIF> impfunc;
  if(m_phase == phase::gas){
    impfunc = m_computationalGeometry->get_gas_if();
  }
  else{
    impfunc = m_computationalGeometry->get_sol_if();
  }

#if MC_PHOTO_DEBUG // Debug
  int photonsBefore = this->count_photons(a_photons.getParticles());
#endif
  
  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++){
    const DisjointBoxLayout& dbl = m_amr->getGrids(m_realm)[lvl];
    const Real dx                = m_amr->getDx()[lvl];
    
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      List<photon>& bulkPhotons = a_bulk_photons[lvl][dit()].listItems();
      List<photon>& ebPhotons   = a_eb_photons[lvl][dit()].listItems();
      List<photon>& domPhotons  = a_domain_photons[lvl][dit()].listItems();
      List<photon>& allPhotons  = a_photons[lvl][dit()].listItems();

      // These must be cleared
      bulkPhotons.clear();
      ebPhotons.clear();
      domPhotons.clear();

      // Iterate over the photons that will be moved. 
      for (ListIterator<photon> lit(allPhotons); lit.ok(); ++lit){
	photon& p = lit();

	// Draw a new random absorption position
	const RealVect oldPos  = p.position();
	const RealVect unitV   = p.velocity()/(p.velocity().vectorLength());
	const RealVect newPos  = oldPos + unitV*this->random_exponential(p.kappa());
	const RealVect path    = newPos - oldPos;
	const Real     pathLen = path.vectorLength();

	// Check if we should check of different types of boundary intersections. These are checp initial tests that allow
	// us to skip intersection tests for some photons.
	bool checkEB  = false;
	bool checkDom = false;

	if(!impfunc.isNull()){
	  checkEB = true;
	}
	for (int dir = 0; dir < SpaceDim; dir++){
	  if(newPos[dir] < prob_lo[dir] || newPos[dir] > prob_hi[dir]){
	    checkDom = true; 
	  }
	}


	if(!checkEB && !checkDom){ // No intersection test necessary, add to bulk absorption
	  p.position() = newPos;
	  bulkPhotons.add(p);
	}
	else{ // Must do nasty intersect test.
	  Real dom_s = 1.E99;
	  Real eb_s  = 1.E99;

	  bool contact_domain = false;
	  bool contact_eb     = false;

	  // Do intersection tests
	  if(checkDom) contact_domain = particle_ops::domain_intersection(oldPos, newPos, path, prob_lo, prob_hi, dom_s);
	  //	  if(checkEB)  contact_eb = particle_ops::eb_intersection_bisect(impfunc, oldPos, newPos, pathLen, m_bisect_step, eb_s);
	  if(checkEB)  contact_eb = particle_ops::eb_intersection_raycast(impfunc, oldPos, newPos, 1.E-10*dx, eb_s);

	  // Move the photon to the data holder where it belongs. 
	  if(!contact_eb && !contact_domain){
	    p.position() = newPos;
	    bulkPhotons.add(p);
	  }
	  else {
	    if(eb_s < dom_s){
	      p.position() = oldPos + eb_s*path;
	      ebPhotons.add(p);
	    }
	    else{
	      p.position() = oldPos + Max(0.0,dom_s-SAFETY)*path;
	      domPhotons.add(p);
	    }
	  }
	}
      }

      // Should clear these out. 
      allPhotons.clear();
    }
  }

  // Remap and clear.
  a_bulk_photons.remap();
  a_eb_photons.remap();
  a_domain_photons.remap();

#if MC_PHOTO_DEBUG // Debug
  int bulkPhotons = this->count_photons(a_bulk_photons.getParticles());
  int ebPhotons = this->count_photons(a_eb_photons.getParticles());
  int domPhotons = this->count_photons(a_domain_photons.getParticles());

  if(procID() == 0){
    std::cout << "photons before = " << photonsBefore << "\n"
	      << "bulk photons = " << bulkPhotons << "\n"
	      << "eb photons = " << ebPhotons << "\n"
	      << "dom photons = " << domPhotons << "\n"
	      << "photons after = " << domPhotons+ebPhotons+bulkPhotons << "\n" << std::endl;
  }
#endif


}

void mc_photo::advance_photons_transient(ParticleContainer<photon>& a_bulk_photons,
					 ParticleContainer<photon>& a_eb_photons,
					 ParticleContainer<photon>& a_domain_photons,
					 ParticleContainer<photon>& a_photons,
					 const Real                  a_dt){
  CH_TIME("mc_photo::advance_photons_transient");
  if(m_verbosity > 5){
    pout() << m_name + "::advance_photons_transient" << endl;
  }

  // TLDR: This routine iterates over the levels and boxes and does the following
  //
  //       Forall photons in a_photons: {
  //          1. Check new photon position
  //          2. Check if the photon was absorbed on the interval
  //          3. Check if path intersects boundary, either EB or domain
  //          4. Move the photon to appropriate data holder:
  //                 Path crossed EB   => a_eb_photons
  //                 Path cross domain => a_domain_photons
  //                 Absorbed in bulk  => a_bulk_photons
  //       }
  //
  //       Remap a_bulk_photons, a_eb_photons, a_domain_photons, a_photons

  // Low and high corners
  const RealVect prob_lo = m_amr->getProbLo();
  const RealVect prob_hi = m_amr->getProbHi();

  // Safety factor
  const Real SAFETY = 1.E-8;

  // This is the implicit function used for intersection tests
  RefCountedPtr<BaseIF> impfunc;
  if(m_phase == phase::gas){
    impfunc = m_computationalGeometry->get_gas_if();
  }
  else{
    impfunc = m_computationalGeometry->get_sol_if();
  }

#if MC_PHOTO_DEBUG // Debug
  int photonsBefore = this->count_photons(a_photons.getParticles());
#endif

  
  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++){
    const DisjointBoxLayout& dbl = m_amr->getGrids(m_realm)[lvl];
    const Real dx = m_amr->getDx()[lvl];
    
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      List<photon>& bulkPhotons = a_bulk_photons[lvl][dit()].listItems();
      List<photon>& ebPhotons   = a_eb_photons[lvl][dit()].listItems();
      List<photon>& domPhotons  = a_domain_photons[lvl][dit()].listItems();
      List<photon>& allPhotons  = a_photons[lvl][dit()].listItems();

      
      // These must be cleared
      bulkPhotons.clear();
      ebPhotons.clear();
      domPhotons.clear();
      

      // Iterate over the photons that will be moved. 
      for (ListIterator<photon> lit(allPhotons); lit.ok(); ++lit){
	photon& p = lit();

	// Move the photon
	const RealVect oldPos  = p.position();
	const RealVect v       = p.velocity();
	const RealVect unitV   = v/v.vectorLength();
	const RealVect newPos  = oldPos + v*a_dt;
	const RealVect path    = newPos - oldPos;
	const Real     pathLen = path.vectorLength();

	// Check if we should check of different types of boundary intersections. These are checp initial tests that allow
	// us to skip more expensive intersection tests for some photons.
	bool checkEB  = false;
	bool checkDom = false;

	if(!impfunc.isNull()){
	  checkEB = true;
	}
	for (int dir = 0; dir < SpaceDim; dir++){
	  if(newPos[dir] <= prob_lo[dir] || newPos[dir] >= prob_hi[dir]){
	    checkDom = true; 
	  }
	}

	// Handles for how long the photons move
	bool absorbed_bulk   = false;
	bool absorbed_eb     = false;
	bool absorbed_domain = false;

	Real bulk_s = 1.E99;
	Real eb_s   = 1.E99;
	Real dom_s  = 1.E99;


	// Check absorption in the bulk
	const Real travelLen  = this->random_exponential(p.kappa());
	if(travelLen < pathLen){
	  absorbed_bulk = true;
	  bulk_s        = travelLen/pathLen;
	}

	// Check absorption on EBs and domain
	if(checkEB){
	  //	  absorbed_eb = particle_ops::eb_intersection_bisect(impfunc, oldPos, newPos, pathLen, m_bisect_step, eb_s);
	  absorbed_eb = particle_ops::eb_intersection_raycast(impfunc, oldPos, newPos, 1.E-10*dx, eb_s);
	}
	if(checkDom){
	  absorbed_domain = particle_ops::domain_intersection(oldPos, newPos, path, prob_lo, prob_hi, dom_s);
	  dom_s = (absorbed_domain) ? Max(0.0, dom_s - SAFETY) : dom_s;
	}

	const bool absorb = absorbed_bulk || absorbed_eb || absorbed_domain;
	if(!absorb){
	  p.position() = newPos;
	}
	else{
	  const Real s = Min(bulk_s, Min(eb_s, dom_s));
	  p.position() = oldPos + s*path;

	  // Now check where it was actually absorbed
	  if(absorbed_bulk && bulk_s < Min(eb_s, dom_s)){ 
	    bulkPhotons.transfer(lit);
	  }
	  else if(absorbed_eb && eb_s < Min(bulk_s, dom_s)){
	    ebPhotons.transfer(lit);
	  }
	  else if(absorbed_domain && dom_s < Min(bulk_s, eb_s)){
	    domPhotons.transfer(lit);
	  }
	  else{
	    MayDay::Abort("mc_photo::advance_photons_transient - logic bust");
	  }
	}
      }
    }
  }

  // Remap and clear. 
  a_bulk_photons.remap();
  a_eb_photons.remap();
  a_domain_photons.remap();
  a_photons.remap();

#if MC_PHOTO_DEBUG // Debug
  int bulkPhotons = this->count_photons(a_bulk_photons.getParticles());
  int ebPhotons = this->count_photons(a_eb_photons.getParticles());
  int domPhotons = this->count_photons(a_domain_photons.getParticles());
  int afterPhotons = this->count_photons(a_photons.getParticles());

  if(procID() == 0){
    std::cout << "photons before = " << photonsBefore << "\n"
	      << "photons after = " << afterPhotons << "\n"
	      << "bulk photons = " << bulkPhotons << "\n"
	      << "eb photons = " << ebPhotons << "\n"
	      << "dom photons = " << domPhotons << "\n"
	      << "total = " << domPhotons+ebPhotons+bulkPhotons+afterPhotons << "\n" << std::endl;
  }


#endif
  a_bulk_photons.remap();
}

void mc_photo::remap(){
  CH_TIME("mc_photo::remap()");
  if(m_verbosity > 5){
    pout() << m_name + "::remap()" << endl;
  }

  this->remap(m_photons);
}

void mc_photo::remap(ParticleContainer<photon>& a_photons){
  CH_TIME("mc_photo::remap(photons)");
  if(m_verbosity > 5){
    pout() << m_name + "::remap(photons)" << endl;
  }

  a_photons.remap();
}

int mc_photo::count_photons(const AMRParticles<photon>& a_photons) const {
  CH_TIME("mc_photo::count_photons");
  if(m_verbosity > 5){
    pout() << m_name + "::count_photons" << endl;
  }

  int num_photons = 0;

  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++){
    num_photons += a_photons[lvl]->numValid();
  }

  return num_photons;
}

int mc_photo::count_outcast(const AMRParticles<photon>& a_photons) const {
  CH_TIME("mc_photo::count_outcast");
  if(m_verbosity > 5){
    pout() << m_name + "::count_outcast" << endl;
  }

  int num_outcast = 0;

  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++){
    num_outcast += a_photons[lvl]->numOutcast();
  }

  return num_outcast;
}

void mc_photo::writePlotData(EBAMRCellData& a_output, int& a_comp){
  CH_TIME("mc_photo::writePlotData");
  if(m_verbosity > 5){
    pout() << m_name + "::writePlotData" << endl;
  }

  if(m_plotPhi) {
    this->writeData(a_output, a_comp, m_phi,  false);
  }
  if(m_plotSource) {
    this->writeData(a_output, a_comp, m_source, false);
  }
  if(m_plot_phot){
    this->deposit_photons(m_scratch, m_photons.getParticles(), m_plot_deposition);
    this->writeData(a_output, a_comp, m_scratch,  false);
  }
  if(m_plot_bulk_phot){
    this->deposit_photons(m_scratch, m_bulk_photons.getParticles(), m_plot_deposition);
    this->writeData(a_output, a_comp, m_scratch,  false);
  }
  if(m_plot_eb_phot){
    this->deposit_photons(m_scratch, m_eb_photons.getParticles(), m_plot_deposition);
    this->writeData(a_output, a_comp, m_scratch,  false);
  }
  if(m_plot_dom_phot){
    this->deposit_photons(m_scratch, m_domain_photons.getParticles(), m_plot_deposition);
    this->writeData(a_output, a_comp, m_scratch,  false);
  }
  if(m_plotSource_phot){
    this->deposit_photons(m_scratch, m_source_photons.getParticles(), m_plot_deposition);
    this->writeData(a_output, a_comp, m_scratch,  false);
  }
}

ParticleContainer<photon>& mc_photo::get_photons(){
  CH_TIME("mc_photo::get_photons");
  if(m_verbosity > 5){
    pout() << m_name + "::get_photons" << endl;
  }

  return m_photons;
}

ParticleContainer<photon>& mc_photo::get_bulk_photons(){
  CH_TIME("mc_photo::get_bulk_photons");
  if(m_verbosity > 5){
    pout() << m_name + "::get_bulk_photons" << endl;
  }

  return m_bulk_photons;
}

ParticleContainer<photon>& mc_photo::get_eb_photons(){
  CH_TIME("mc_photo::get_eb_photons");
  if(m_verbosity > 5){
    pout() << m_name + "::get_eb_photons" << endl;
  }

  return m_eb_photons;
}

ParticleContainer<photon>& mc_photo::get_domain_photons(){
  CH_TIME("mc_photo::get_domain_photons");
  if(m_verbosity > 5){
    pout() << m_name + "::get_domain_photons" << endl;
  }

  return m_domain_photons;
}

ParticleContainer<photon>& mc_photo::getSource_photons(){
  CH_TIME("mc_photo::getSource_photons");
  if(m_verbosity > 5){
    pout() << m_name + "::getSource_photons" << endl;
  }

  return m_source_photons;
}
#include "CD_NamespaceFooter.H"
