/* chombo-discharge
 * Copyright © 2021 SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*
  @file   CD_MFHelmholtzNeumannDomainBCFactory.cpp
  @brief  Implementation of CD_MFHelmholtzNeumannDomainBCFactory.H
  @author Robert Marskar
*/

// Our includes
#include <CD_EBHelmholtzNeumannDomainBC.H>
#include <CD_MFHelmholtzNeumannDomainBCFactory.H>
#include <CD_NamespaceHeader.H>

MFHelmholtzNeumannDomainBCFactory::MFHelmholtzNeumannDomainBCFactory(){
  m_multByBco   = true;
  m_useConstant = false;
  m_useFunction = false;
}

MFHelmholtzNeumannDomainBCFactory::MFHelmholtzNeumannDomainBCFactory(const Real a_DphiDn){
  this->setDphiDn(a_DphiDn);
}

MFHelmholtzNeumannDomainBCFactory::MFHelmholtzNeumannDomainBCFactory(const std::function<Real(const RealVect& a_pos)>& a_DphiDn){
  this->setDphiDn(a_DphiDn);
}

MFHelmholtzNeumannDomainBCFactory::~MFHelmholtzNeumannDomainBCFactory(){

}

void MFHelmholtzNeumannDomainBCFactory::setDphiDn(const int a_DphiDn){
  m_multByBco   = false;
  m_useConstant = true;
  m_useFunction = false;
  
  m_constantDphiDn = a_DphiDn;
}

void MFHelmholtzNeumannDomainBCFactory::setDphiDn(const std::function<Real(const RealVect& a_pos)>& a_DphiDn){
  m_multByBco   = false;
  m_useConstant = false;
  m_useFunction = true;
  
  m_functionDphiDn = a_DphiDn;
}

void MFHelmholtzNeumannDomainBCFactory::setBxDphiDn(const int a_BxDphiDn){
  this->setDphiDn(a_BxDphiDn);
  m_multByBco = false;
}

void MFHelmholtzNeumannDomainBCFactory::setBxDphiDn(const std::function<Real(const RealVect& a_pos)>& a_BxDphiDn){
  this->setDphiDn(a_BxDphiDn);
  m_multByBco = false;
}

RefCountedPtr<EBHelmholtzDomainBC> MFHelmholtzNeumannDomainBCFactory::create(const int a_iphase) const {

  auto bc = new EBHelmholtzNeumannDomainBC();

  if(m_multByBco){
    if(m_useConstant) {
      bc->setDphiDn(m_constantDphiDn);
    }
    else if(m_useFunction){
      bc->setDphiDn(m_functionDphiDn);
    }
    else{
      MayDay::Error("MFHelmholtzEBBC::create - logic bust");
    }
  }
  else{
    if(m_useConstant) {
      bc->setBxDphiDn(m_constantDphiDn);
    }
    else if(m_useFunction){
      bc->setBxDphiDn(m_functionDphiDn);
    }
    else{
      MayDay::Error("MFHelmholtzEBBC::create - logic bust");
    }
  }

  return RefCountedPtr<EBHelmholtzNeumannDomainBC>(bc);
}

#include <CD_NamespaceFooter.H>
