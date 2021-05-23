/* chombo-discharge
 * Copyright © 2021 SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @file   CD_HollowCylinderIF.cpp
  @brief  Implementation of CD_HollowCylinderIF.H
  @author Robert Marskar
*/

// Chombo includes
#include <SmoothUnion.H>
#include <UnionIF.H>
#include <IntersectionIF.H>

// Our includes
#include <CD_CylinderSdf.H>
#include <CD_HollowCylinderIF.H>
#include <CD_RoundedCylinderIF.H>
#include <CD_NamespaceHeader.H>

HollowCylinderIF::HollowCylinderIF(const RealVect a_center1,
				       const RealVect a_center2,
				       const Real     a_majorRadius,
				       const Real     a_minorRadius,
				       const Real     a_curv,
				       const bool     a_fluidInside){

  // Make the SmoothUnion of this stuff. 
  Vector<BaseIF*> parts;

  RealVect axis = (a_center2 - a_center1);
  axis = axis/axis.vectorLength();

  const RealVect c2 = a_center2 + a_curv*axis;
  const RealVect c1 = a_center1 - a_curv*axis;
  
  BaseIF* bigCylinder   = (BaseIF*) (new RoundedCylinderIF(a_center1, a_center2, a_majorRadius, a_curv,  a_fluidInside));
  BaseIF* smallCylinder = (BaseIF*) (new CylinderSdf        (a_center1, a_center2, a_minorRadius,         !a_fluidInside));

  parts.push_back(bigCylinder);
  parts.push_back(smallCylinder);
  
  // Make union
  m_baseif = RefCountedPtr<BaseIF> (new SmoothUnion(parts, 2*a_curv));
}

HollowCylinderIF::HollowCylinderIF(const HollowCylinderIF& a_inputIF){
  m_baseif = a_inputIF.m_baseif;
}

Real HollowCylinderIF::value(const RealVect& a_point) const{
  return m_baseif->value(a_point);
}

BaseIF* HollowCylinderIF::newImplicitFunction() const{
  return (BaseIF*) (new HollowCylinderIF(*this));
}

#include <CD_NamespaceFooter.H>
