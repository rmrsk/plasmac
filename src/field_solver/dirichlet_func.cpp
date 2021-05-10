/*!
  @file    dirichlet_func.cpp
  @brief   Implementation of dirichlet_func.H
  @author  Robert Marskar
  @date    June 2018
*/

#include "dirichlet_func.H"

#include "CD_NamespaceHeader.H"

dirichlet_func::~dirichlet_func(){

}

void dirichlet_func::set_time(const Real a_time){
  m_time = a_time;
}

Real dirichlet_func::value(const RealVect& a_point, const int& a_comp) const {
  // m_func wants physical coordinates but a_point is the computational coordinate
  //return m_func(a_point + m_origin)*m_potential(m_time);
  return m_Potential(0.0); 
}

Real dirichlet_func::derivative(const RealVect& a_point, const int& a_comp, const int& a_dir) const {
  MayDay::Abort("field_solver_multigrid::dirichlet_func::derivative - this should not be called. How did you get here?");

  return 0.0;
}

#include "CD_NamespaceFooter.H"
