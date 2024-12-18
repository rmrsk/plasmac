/* chombo-discharge
 * Copyright © SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @brief CD_DiskProfiledPlane.cpp
  @brief Implementation of CD_DiskProfiledPlane.H
  @author Robert Marskar
*/

// Std includes
#include <string>
#include <iostream>

// Chombo includes
#include <CH_Timer.H>
#include <ParmParse.H>

// Our includes
#include <CD_EBGeometryIF.H>
#include <CD_RoundedCylinderIF.H>
#include <CD_DiskProfiledPlane.H>
#include <CD_NamespaceHeader.H>

using namespace EBGeometry;

using Vec3    = EBGeometry::Vec3T<Real>;
using ImpFunc = EBGeometry::ImplicitFunction<Real>;

DiskProfiledPlane::DiskProfiledPlane() noexcept
{
  CH_TIME("DiskProfiledPlane::DiskProfiledPlane");

  bool useElectrode  = false;
  bool useDielectric = false;

  ParmParse pp("DiskProfiledPlane");

  pp.get("use_electrode", useElectrode);
  pp.get("use_dielectric", useDielectric);

  m_electrodes.resize(0);
  m_dielectrics.resize(0);

  if (useElectrode) {
    this->defineElectrode();
  }
  if (useDielectric) {
    this->defineDielectric();
  }
}

void
DiskProfiledPlane::defineElectrode() noexcept
{
  CH_TIME("DiskProfiledPlane::defineElectrode");

  bool isLive         = false;
  Real wheelThickness = 0.0;
  Real wheelRadius    = 0.0;
  Real wheelCurvature = 0.0;
  Real stemRadius     = 0.0;
  Real wheelSmooth    = 0.0;
  bool isLive2         = false;
  Real wheel2Thickness = 0.0;
  Real wheel2Radius    = 0.0;
  Real wheel2Curvature = 0.0;
  Real stem2Radius     = 0.0;
  Real wheel2Smooth    = 0.0;
  bool isLive3         = false;
  Real wheel3Thickness = 0.0;
  Real wheel3Radius    = 0.0;
  Real wheel3Curvature = 0.0;
  Real stem3Radius     = 0.0;
  Real wheel3Smooth    = 0.0;

  Vector<Real> v(3, 0.0);

  ParmParse pp("DiskProfiledPlane");

  // Get input parameters
  pp.get("wheel_live", isLive);
  pp.get("wheel_extra_thickness", wheelThickness);
  pp.get("wheel_radius", wheelRadius);
  pp.get("wheel_curvature", wheelCurvature);
  pp.get("wheel_stem_radius", stemRadius);
  pp.get("wheel_smooth", wheelSmooth);
  pp.getarr("wheel_center", v, 0, SpaceDim);

  // Create disk electrode using EBGeometry. This is an elongation of the union of a torus and a cylinder. Created
  // in the xy-plane and then put into place later.

  auto torus    = std::make_shared<TorusSDF<Real>>(Vec3::zero(), wheelRadius, wheelCurvature);
  auto cylinder = std::make_shared<CylinderSDF<Real>>(Vec3::unit(2) * wheelCurvature,
                                                      -Vec3::unit(2) * wheelCurvature,
                                                      wheelRadius);

  // Make smooth union and put into place.
  auto wheel = Union<Real>(torus, cylinder);
  if (stemRadius > 0.0) {
    auto holder = std::make_shared<CapsuleSDF<Real>>(Vec3::zero(), 1.E10 * Vec3::unit(1), stemRadius);
    wheel       = SmoothUnion<Real>(wheel, holder, wheelSmooth);
  }
  if (wheelThickness > 0.0) {
    wheel = Elongate<Real>(wheel, 0.5 * wheelThickness * Vec3::unit(2));
  }
  wheel = Rotate<Real>(wheel, 90.0, 1);
  wheel = Translate<Real>(wheel, Vec3(v[0], v[1], v[2]));

  if (SpaceDim == 2) {
    wheel = Elongate<Real>(wheel, std::numeric_limits<Real>::max() * Vec3::unit(2));
  }
	
	// SECOND ELECTRODE :::
	
	// Get input parameters
  pp.get("wheel2_live", isLive2);
  pp.get("wheel2_extra_thickness", wheel2Thickness);
  pp.get("wheel2_radius", wheel2Radius);
  pp.get("wheel2_curvature", wheel2Curvature);
  pp.get("wheel2_stem_radius", stem3Radius);
  pp.get("wheel2_smooth", wheel2Smooth);
  pp.getarr("wheel2_center", v, 0, SpaceDim);

  // Create disk electrode using EBGeometry. This is an elongation of the union of a torus and a cylinder. Created
  // in the xy-plane and then put into place later.

  auto torus2    = std::make_shared<TorusSDF<Real>>(Vec3::zero(), wheel2Radius, wheel2Curvature);
  auto cylinder2 = std::make_shared<CylinderSDF<Real>>(Vec3::unit(2) * wheel2Curvature,
                                                      -Vec3::unit(2) * wheel2Curvature,
                                                      wheel2Radius);

  // Make smooth union and put into place.
  auto wheel2 = Union<Real>(torus2, cylinder2);
  if (stemRadius > 0.0) {
    auto holder = std::make_shared<CapsuleSDF<Real>>(Vec3::zero(), 1.E10 * Vec3::unit(1), stem2Radius);
    wheel2       = SmoothUnion<Real>(wheel2, holder, wheel2Smooth);
  }
  if (wheel2Thickness > 0.0) {
    wheel2 = Elongate<Real>(wheel2, 0.5 * wheel2Thickness * Vec3::unit(2));
  }
  wheel2 = Rotate<Real>(wheel2, 90.0, 0);
  wheel2 = Translate<Real>(wheel2, Vec3(v[0], v[1], v[2]));

  if (SpaceDim == 2) {
    wheel2 = Elongate<Real>(wheel2, std::numeric_limits<Real>::max() * Vec3::unit(2));
  }
  
  // THIRD ELECTRODE :::
	
	// Get input parameters
  pp.get("wheel3_live", isLive3);
  pp.get("wheel3_extra_thickness", wheel3Thickness);
  pp.get("wheel3_radius", wheel3Radius);
  pp.get("wheel3_curvature", wheel3Curvature);
  pp.get("wheel3_stem_radius", stem3Radius);
  pp.get("wheel3_smooth", wheel3Smooth);
  pp.getarr("wheel3_center", v, 0, SpaceDim);

  // Create disk electrode using EBGeometry. This is an elongation of the union of a torus and a cylinder. Created
  // in the xy-plane and then put into place later.

  auto torus3    = std::make_shared<TorusSDF<Real>>(Vec3::zero(), wheel3Radius, wheel3Curvature);
  auto cylinder3 = std::make_shared<CylinderSDF<Real>>(Vec3::unit(2) * wheel3Curvature,
                                                      -Vec3::unit(2) * wheel3Curvature,
                                                      wheel3Radius);

  // Make smooth union and put into place.
  auto wheel3 = Union<Real>(torus3, cylinder3);
  if (stemRadius > 0.0) {
    auto holder = std::make_shared<CapsuleSDF<Real>>(Vec3::zero(), 1.E10 * Vec3::unit(1), stem3Radius);
    wheel3       = SmoothUnion<Real>(wheel3, holder, wheel3Smooth);
  }
  if (wheel3Thickness > 0.0) {
    wheel3 = Elongate<Real>(wheel3, 0.5 * wheel3Thickness * Vec3::unit(2));
  }
  wheel3 = Rotate<Real>(wheel3, 90.0, 0);
  wheel3 = Translate<Real>(wheel3, Vec3(v[0], v[1], v[2]));

  if (SpaceDim == 2) {
    wheel3 = Elongate<Real>(wheel3, std::numeric_limits<Real>::max() * Vec3::unit(2));
  }
	
  // Turn EBGeometry into Chombo
  m_electrodes.push_back(Electrode(RefCountedPtr<BaseIF>(new EBGeometryIF<Real>(wheel, true)), isLive));
  m_electrodes.push_back(Electrode(RefCountedPtr<BaseIF>(new EBGeometryIF<Real>(wheel2, true)), isLive2));
  m_electrodes.push_back(Electrode(RefCountedPtr<BaseIF>(new EBGeometryIF<Real>(wheel3, true)), isLive3));
}

void
DiskProfiledPlane::defineDielectric() noexcept
{
  CH_TIME("DiskProfiledPlane::defineDielectric");

  std::string str;

  Real boxCurvature      = 0.0;
  Real permittivity      = 1.0;
  Real sphereRadius      = 0.0;
  Real cylinderRadius    = 0.0;
  Real golfpegRadius     = 0.0;
  Real golfpegLength     = 0.0;
  Real golfpegWidth      = 0.0;
  Real golfpegCurvature1 = 0.0;
  Real golfpegCurvature2 = 0.0;
  bool reflectLo         = false;
  bool reflectHi         = false;

  Vector<Real> boxDimensions(3, std::numeric_limits<Real>::max());
  Vector<Real> boxTranslation(3, 0.0);
  Vector<Real> profileTranslateLo(3, 0.0);
  Vector<Real> profileTranslateHi(3, 0.0);
  Vector<Real> profileRepetitionLo(3, 0.0);
  Vector<Real> profileRepetitionHi(3, 0.0);
  Vector<Real> profilePeriod(3, 0.0);
  Vector<Real> squareDimensions(3, std::numeric_limits<Real>::max());
  Vector<Real> lshapeDimensions(3, std::numeric_limits<Real>::max());

  Real lshapeCutAngle = 0.0;

  // Get input parameters.
  ParmParse pp("DiskProfiledPlane");

  pp.get("profile_type", str);
  pp.get("box_permittivity", permittivity);
  pp.get("box_curvature", boxCurvature);
  pp.get("sphere_radius", sphereRadius);
  pp.get("cylinder_radius", cylinderRadius);
  pp.get("golfpeg_radius", golfpegRadius);
  pp.get("golfpeg_length", golfpegLength);
  pp.get("golfpeg_width", golfpegWidth);
  pp.get("golfpeg_curvature1", golfpegCurvature1);
  pp.get("golfpeg_curvature2", golfpegCurvature2);
  pp.get("lshape_cut_angle", lshapeCutAngle);
  pp.get("reflect_lo", reflectLo);
  pp.get("reflect_hi", reflectHi);

  pp.getarr("box_dimensions", boxDimensions, 0, 3);
  pp.getarr("box_translate", boxTranslation, 0, 3);
  pp.getarr("profile_translate_lo", profileTranslateLo, 0, 3);
  pp.getarr("profile_translate_hi", profileTranslateHi, 0, 3);
  pp.getarr("profile_repetition_lo", profileRepetitionLo, 0, 3);
  pp.getarr("profile_repetition_hi", profileRepetitionHi, 0, 3);
  pp.getarr("profile_period", profilePeriod, 0, 3);
  pp.getarr("square_dimensions", squareDimensions, 0, 3);
  pp.getarr("lshape_dimensions", lshapeDimensions, 0, 3);

  if (boxCurvature <= 0.0) {
    MayDay::Error("DiskProfiledPlane::defineDielectric - must have 'box_curvature' > 0.0");
  }

  const Vec3 boxDims(boxDimensions[0] - 2 * boxCurvature,
                     boxDimensions[1] - 2 * boxCurvature,
                     boxDimensions[2] - 2 * boxCurvature);
  const Vec3 profileTraLo(profileTranslateLo[0], profileTranslateLo[1], profileTranslateLo[2]);
  const Vec3 profileTraHi(profileTranslateHi[0], profileTranslateHi[1], profileTranslateHi[2]);
  //const Vec3 lshapeTra(lshapeTranslate[0], lshapeTranslate[1], lshapeTranslate[2]);
  const Vec3 profileRepLo(profileRepetitionLo[0], profileRepetitionLo[1], profileRepetitionLo[2]);
  const Vec3 profileRepHi(profileRepetitionHi[0], profileRepetitionHi[1], profileRepetitionHi[2]);
  const Vec3 profilePer(profilePeriod[0], profilePeriod[1], profilePeriod[2]);
  const Vec3 squareDim(squareDimensions[0] - 2 * boxCurvature,
                       squareDimensions[1] - 2 * boxCurvature,
                       squareDimensions[2] - 2 * boxCurvature);
  const Vec3 lshapeDim(lshapeDimensions[0] - 2 * boxCurvature,
                       lshapeDimensions[1] - 2 * boxCurvature,
                       lshapeDimensions[2] - 2 * boxCurvature);

  // Basic rounded box.
  std::shared_ptr<ImpFunc> roundedBox = std::make_shared<RoundedBoxSDF<Real>>(boxDims, boxCurvature);
  roundedBox                          = Translate<Real>(roundedBox, -0.5 * boxDimensions[1] * Vec3::unit(1));

  // Determine the requested profile type.
  std::shared_ptr<ImpFunc> profile;
  std::shared_ptr<ImpFunc> profile2;
  std::shared_ptr<ImpFunc> profileLo;
  std::shared_ptr<ImpFunc> profileHi;
  // std::shared_ptr<ImpFunc> profile3;
  std::shared_ptr<ImpFunc> plane;
  if (str == "square") {
    profile = std::make_shared<RoundedBoxSDF<Real>>(squareDim, boxCurvature);
  }
  else if (str == "sphere") {
    profile = std::make_shared<SphereSDF<Real>>(Vec3::zero(), sphereRadius);
  }
  else if (str == "cylinder_x") {
    profile = std::make_shared<SphereSDF<Real>>(Vec3::zero(), cylinderRadius);
    profile = Elongate<Real>(profile, std::numeric_limits<Real>::max() * Vec3::unit(0));
  }
  else if (str == "cylinder_y") {
    profile = std::make_shared<SphereSDF<Real>>(Vec3::zero(), cylinderRadius);
    profile = Elongate<Real>(profile, std::numeric_limits<Real>::max() * Vec3::unit(1));
  }
  else if (str == "cylinder_z") {
    profile = std::make_shared<SphereSDF<Real>>(Vec3::zero(), cylinderRadius);
    profile = Elongate<Real>(profile, std::numeric_limits<Real>::max() * Vec3::unit(2));
  }
  else if (str == "lshape") {
    profile = std::make_shared<RoundedBoxSDF<Real>>(Vec3(lshapeDim[0], 2 * (lshapeDim[1] + boxCurvature), 0.0),
                                                    boxCurvature);
    profile = Translate<Real>(profile, Vec3(0.0, -0.5 * (lshapeDim[1] + 2 * boxCurvature), 0.0));

    profile2 = std::make_shared<RoundedBoxSDF<Real>>(lshapeDim, boxCurvature);
    profile2 = Translate<Real>(profile2,
                               -0.5 * Vec3(lshapeDim[0] + 2 * boxCurvature, lshapeDim[1] + 2 * boxCurvature, 0.0));

    profile = SmoothDifference<Real>(profile, profile2, boxCurvature);

    // make cut
    plane   = std::make_shared<PlaneSDF<Real>>(Vec3(-0.5 * lshapeDim[0], 0.5 * lshapeDim[1] + boxCurvature, 0.0),
                                             Vec3(-std::sin(lshapeCutAngle), -std::cos(lshapeCutAngle), 0.0));
    profile = SmoothDifference<Real>(profile, plane, boxCurvature);
  }

  else if (str == "golfpeg") {
    profile  = std::make_shared<SphereSDF<Real>>(Vec3::zero(), golfpegRadius);
    profile2 = std::make_shared<CylinderSDF<Real>>(Vec3::zero(), Vec3(0.0, -golfpegLength, 0.0), golfpegWidth);
    profile  = SmoothUnion<Real>(profile, profile2, golfpegCurvature1);
  }
  else if (str == "none") {
  }
  else {
    MayDay::Abort("DiskProfiledPlane::defineDielectric - unsupported profile type requested");
  }

  profileLo = profile;
  profileHi = profile;
  if (reflectLo) {
    // reflect profile about x = 0
    profileLo = Reflect<Real>(profile, 0);
  }
  if (reflectHi) {
    // reflect profile about x = 0
    profileHi = Reflect<Real>(profile, 0);
  }

  // Translate and repeat the profiles. Then do a smooth difference or union
  profileLo = FiniteRepetition<Real>(profileLo, profilePer, profileRepLo, Vec3::zero());
  profileLo = Translate<Real>(profileLo, profileTraLo);
  profileHi = FiniteRepetition<Real>(profileHi, profilePer, Vec3::zero(), profileRepHi);
  profileHi = Translate<Real>(profileHi, profileTraHi);

  if (str != "none" && str != "lshape" && str != "golfpeg") {

    roundedBox = SmoothDifference<Real>(roundedBox, profileLo, boxCurvature);
    roundedBox = SmoothDifference<Real>(roundedBox, profileHi, boxCurvature);
  }

  else if (str == "lshape") {

    roundedBox = SmoothUnion<Real>(roundedBox, profileLo, boxCurvature);
    roundedBox = SmoothUnion<Real>(roundedBox, profileHi, boxCurvature);
  }

  else if (str == "golfpeg") {

    roundedBox = SmoothUnion<Real>(roundedBox, profileLo, golfpegCurvature2);
    roundedBox = SmoothUnion<Real>(roundedBox, profileHi, golfpegCurvature2);
  }

  // Translate box into place.
  roundedBox = Translate<Real>(roundedBox, Vec3(boxTranslation[0], boxTranslation[1], boxTranslation[2]));

  if (SpaceDim == 2) {
    roundedBox = Elongate<Real>(roundedBox, std::numeric_limits<Real>::max() * Vec3::unit(2));
  }

  // Finally, create our dielectric.
  m_dielectrics.push_back(Dielectric(RefCountedPtr<BaseIF>(new EBGeometryIF<Real>(roundedBox, true)), permittivity));
}

#include <CD_NamespaceFooter.H>
