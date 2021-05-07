/*!
  @file polygon_rod_if.cpp
  @brief Implementation of polygon_rod_if.hpp
  @author Robert Marskar
  @date Aug. 2016
*/

#include <IntersectionIF.H>
#include <SphereIF.H>
#include <PlaneIF.H>
#include <TransformIF.H>
#include <UnionIF.H>
#include <SmoothIntersection.H>
#include <SmoothUnion.H>

#include "polygon_rod_if.H"
#include "cylinder_if.H"

namespace ChomboDischarge {

  polygon_rod_if::polygon_rod_if(const RealVect a_endPoint1,
				 const RealVect a_endPoint2,
				 const Real     a_radius,
				 const Real     a_cornerCurv,
				 const int      a_numSides,
				 const bool     a_fluidInside){
    if(SpaceDim != 3){
      MayDay::Abort("polygon_rod_if::polygon_rod_if - this is a 3D object!");
    }

    const Real length = (a_endPoint2 - a_endPoint1).vectorLength();
    const Real dTheta = 2.*M_PI/a_numSides;  // Internal angle
    const Real alpha  = M_PI/(a_numSides);   // Internal half angle 
    const Real beta   = 0.5*M_PI - alpha;    // External half angle
  
    const Real a = sin(alpha);             // Triangle factor, opposite internal angle
    const Real b = sin(beta);              // Triangle factor, opposite external angle
    const Real r = a_cornerCurv;           // Radius of curvature. Shortcut. 
    const Real R = a_radius - r + r/b;     // Radius accounts for curvature
    const Real c = R - r/b;                // Sphere center for rounding
  
    const RealVect zhat = RealVect(D_DECL(0., 0., 1.0));
  
    // Add the "sides" of the cylinder. 
    Vector<BaseIF*> planes;
    for (int iside = 0; iside < a_numSides; iside++){
      const Real theta = iside*dTheta;
      const RealVect n =   RealVect(D_DECL(cos(theta + 0.5*dTheta),sin(theta+0.5*dTheta), 0));
      const RealVect p = R*RealVect(D_DECL(cos(theta),             sin(theta),            0.));

      planes.push_back((BaseIF*) new PlaneIF(-n, p, a_fluidInside));
    }

    // Add cuts above/below.
    planes.push_back((BaseIF*) (new PlaneIF(-zhat, length*zhat,    a_fluidInside)));
    planes.push_back((BaseIF*) (new PlaneIF( zhat, RealVect::Zero, a_fluidInside)));

    // Make a smooth union of those planes.
    BaseIF* isect = (BaseIF*) new SmoothUnion(planes, a_cornerCurv);

    // Do a transform, translating the rod into its specified place. 
    TransformIF* transif = new TransformIF(*isect);
  
    if(a_endPoint2[2] >= a_endPoint1[2]){
      transif->rotate(zhat, a_endPoint2-a_endPoint1);
      transif->translate(a_endPoint1);
    }
    else{
      transif->rotate(zhat, a_endPoint1-a_endPoint2);
      transif->translate(a_endPoint2);
    }

    // Ok, we're done. Set m_baseif and clear up memory. 
    m_baseif = RefCountedPtr<BaseIF> (transif);

    for (int i = 0; i < planes.size(); i++) {
      delete planes[i];
    }
  }
    
  polygon_rod_if::polygon_rod_if(const polygon_rod_if& a_inputIF){
    m_baseif = a_inputIF.m_baseif;
  }

  polygon_rod_if::~polygon_rod_if(){
  }

  Real polygon_rod_if::value(const RealVect& a_pos) const {
    return m_baseif->value(a_pos);
  }

  BaseIF* polygon_rod_if::newImplicitFunction() const {
    return (BaseIF*) new polygon_rod_if(*this);
  }
}
