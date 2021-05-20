/*!
  @file   brownian_walker_tagger.cpp
  @brief  Implementation of brownian_walker_tagger.H
  @author Robert Marskar
  @date   Nov. 2017
*/

#include "brownian_walker_tagger.H"
#include <CD_DataOps.H>

#include <ParmParse.H> 

#include "CD_NamespaceHeader.H"
using namespace physics::brownian_walker;

brownian_walker_tagger::brownian_walker_tagger(RefCountedPtr<ito_solver>& a_solver,
					       RefCountedPtr<AmrMesh>&   a_amr){
  m_solver    = a_solver;
  m_amr       = a_amr;
  m_name      = "brownian_walker";
  m_verbosity = -1;
}

brownian_walker_tagger::~brownian_walker_tagger(){

}

void brownian_walker_tagger::regrid(){

}

void brownian_walker_tagger::parseOptions(){
  ParmParse pp(m_name.c_str());
  pp.get("refine_magn", m_refi_magn);
  
  parseBuffer(); // Derived from CellTagger
}

bool brownian_walker_tagger::tagCells(EBAMRTags& a_tags){
  return true;
}
#include "CD_NamespaceFooter.H"
