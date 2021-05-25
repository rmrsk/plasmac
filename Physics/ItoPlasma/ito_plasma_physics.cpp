/*!
  @file   ito_plasma_physics.cpp
  @brief  Implementation of ito_plasma_physics.H
  @author Robert Marskar
  @date   June 2020
*/

#include "ito_plasma_physics.H"
#include <CD_Units.H>

#include <PolyGeom.H>

#include <fstream>
#include <sstream>

#include "CD_NamespaceHeader.H"
  
using namespace Physics::ItoPlasma;

ito_plasma_physics::ito_plasma_physics(){

  // Default coupling
  m_coupling = coupling::LFA;

  // Initialize RNGs
  m_udist11 = std::uniform_real_distribution<Real>(-1., 1.);
  m_udist01 = std::uniform_real_distribution<Real>(0., 1.);

  m_reactions.clear();
  m_ItoPlasmaPhotoReactions.clear();

  // Default
  m_ppc = 32;

  // Default parameters for hybrid algorithm. 
  m_Ncrit     = 10;
  m_eps       = 1.0;
  m_NSSA      = 100;
  m_SSAlim    = 0.1;
  m_algorithm = algorithm::hybrid;

  // Default parameter for lookup tables
  m_table_entries = 1000;
}

ito_plasma_physics::~ito_plasma_physics(){
}

const Vector<RefCountedPtr<ItoSpecies> >& ito_plasma_physics::get_ItoSpecies() const { 
  return m_ItoSpecies; 
}

const Vector<RefCountedPtr<RtSpecies> >& ito_plasma_physics::getRtSpecies() const {
  return m_RtSpecies;
}

int ito_plasma_physics::get_num_ItoSpecies() const{
  return m_ItoSpecies.size();
}

int ito_plasma_physics::getNumRtSpecies() const {
  return m_RtSpecies.size();
}

ito_plasma_physics::coupling ito_plasma_physics::get_coupling() const {
  return m_coupling;
}

Real ito_plasma_physics::initialSigma(const Real a_time, const RealVect a_pos) const {
  return 0.0;
}

void ito_plasma_physics::addTable(const std::string a_table_name, const std::string a_file){

  LookupTable table;

  this->read_file(table, a_file);        // Read file
  table.makeUniform(m_table_entries);   // Make table into a unifom table
  m_tables.emplace(a_table_name, table); // Add table
}

void ito_plasma_physics::read_file(LookupTable& a_table, const std::string a_file){

  Real x, y;
  
  std::ifstream infile(a_file);
  std::string line;

  while (std::getline(infile, line)){

    // Trim string
    trim(line);

    std::istringstream iss(line);

    const bool skipline = (line.at(0) == '#') || (line.length() == 0);
    if(!skipline){
      if (!(iss >> x >> y)) {
	continue;
      }
      a_table.addEntry(x, y);
    }
  }
  infile.close();
}
#include "CD_NamespaceFooter.H"
