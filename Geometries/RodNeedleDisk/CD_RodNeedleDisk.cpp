/* chombo-discharge
 * Copyright © 2022 NTNU.
 * Copyright © 2022 Fanny Skirbekk. 
 * Copyright © 2023 SINTEF Energy Research
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @file   CD_RodNeedleDisk.cpp
  @brief  Implementation of CD_RodNeedleDisk.H
  @author Fanny Skirbekk
  @author Robert Marskar
*/

// Chombo includes
#include <ParmParse.H>

// Our includes
#include <CD_RodNeedleDisk.H>
#include <CD_RodIF.H>
#include <CD_Units.H>
#include <CD_NeedleIF.H>
#include <CD_EBGeometryIF.H>
#include <CD_RoundedCylinderIF.H>
#include <CD_NamespaceHeader.H>

using Vec3    = EBGeometry::Vec3T<Real>;
using ImpFunc = EBGeometry::ImplicitFunction<Real>;

RodNeedleDisk::RodNeedleDisk() noexcept
{
  CH_TIME("RodNeedleDisk::RodNeedleDisk");

  ParmParse pp("RodNeedleDisk");

  this->setGasPermittivity(1.0);
  this->defineRodNeedle();
  this->defineDisk();
}

RodNeedleDisk::~RodNeedleDisk() noexcept
{
  CH_TIME("RodNeedleDisk::~RodNeedleDisk");
}

void
RodNeedleDisk::defineRodNeedle() noexcept
{
  CH_TIME("RodNeedleDisk::defineRodNeedle");

  ParmParse pp("RodNeedleDisk");

  bool useRod     = false;
  bool useNeedle  = false;
  bool rodLive    = false;
  bool rodPlateau = false;

  Real needleLength     = 0.0;
  Real needleRadius     = 0.0;
  Real needleAngle      = 0.0;
  Real needleTipRadius  = 0.0;
  Real bigRadius        = -1.0;
  Real smallRadius      = -1.0;
  Real rodSmooth        = -1.0;
  Real smallBegin       = -1.0;
  Real bigEnd           = -1.0;
  Real midpoint         = -1.0;
  Real rodNeedleSmooth  = -1.0;
  Real rodPlateauRadius = -1.0;

  Vector<Real> v;
  std::string  orientation = "+z";
  Vec3         translate   = Vec3::zero();

  pp.get("rod_live", rodLive);
  pp.get("use_rod", useRod);
  pp.get("use_needle", useNeedle);
  pp.get("needle_length", needleLength);
  pp.get("needle_radius", needleRadius);
  pp.get("needle_angle", needleAngle);
  pp.get("needle_tip_radius", needleTipRadius);
  pp.get("rod_small_radius", smallRadius);
  pp.get("rod_big_radius", bigRadius);
  pp.get("rod_begin", smallBegin);
  pp.get("rod_midpoint", midpoint);
  pp.get("rod_end", bigEnd);
  pp.get("rod_smooth", rodSmooth);
  pp.get("rod_needle_smooth", rodNeedleSmooth);
  pp.get("orientation", orientation);
  pp.getarr("translate", v, 0, SpaceDim);
  pp.get("rod_plateau", rodPlateau);
  pp.get("rod_plateau_radius", rodPlateauRadius);

  for (int dir = 0; dir < SpaceDim; dir++) {
    translate[dir] = v[dir];
  }

  CH_assert(smallRadius > 0.0);
  CH_assert(bigRadius > 0.0);
  CH_assert(bigRadius > smallRadius);
  CH_assert(midpoint > smallBegin);
  CH_assert(midpoint < bigEnd);
  CH_assert(rodSmooth > 0.0);
  CH_assert(rodNeedleSmooth >= 0.0);
  CH_assert(needleRadius > needleTipRadius);
  CH_assert(needleTipRadius > 0.0);
  CH_assert(rodPlateauRadius > 0.0);
  CH_assert(bigRadius > rodPlateauRadius);

  std::vector<std::shared_ptr<ImpFunc>> implicitFunctions;

  // Create the needle implicit function.
  if (useNeedle) {
    // a_angle is entire opening angle, dividing by two to get half of the opening angle
    const Real bodyRadius       = needleRadius - needleTipRadius;
    const Real tipLength        = bodyRadius / std::tan((0.5 * needleAngle) * Units::pi / 180.0);
    const Real transitionRadius = 0.25 * needleTipRadius;

    // The center of the needle tip is set to origo in order for the rotation to work more easily
    const Vec3 bodyBackPosition(0.0, 0.0, 0.0);
    const Vec3 bodyFrontPosition(0.0, 0.0, -needleLength + needleTipRadius);
    const Vec3 needleTipCenter(0.0, 0.0, -needleTipRadius);

    std::shared_ptr<ImpFunc> cone;
    std::shared_ptr<ImpFunc> cylinder;
    std::shared_ptr<ImpFunc> needle;

    cylinder = std::make_shared<EBGeometry::CylinderSDF<Real>>(bodyBackPosition, bodyFrontPosition, bodyRadius);
    cone     = std::make_shared<EBGeometry::InfiniteConeSDF<Real>>(needleTipCenter, needleAngle);
    needle   = EBGeometry::SmoothIntersection<Real>(cylinder, cone, transitionRadius);
    needle   = EBGeometry::Offset<Real>(needle, needleTipRadius);

    implicitFunctions.emplace_back(needle);
  }

  // Create the rod implicit function.
  if (useRod) {
    const Real smallCapsuleLength = std::abs(midpoint - smallBegin) + smallRadius;
    const Real bigCapsuleLength   = std::abs(bigEnd - midpoint);

    const Vec3 smallCapsuleTranslate = Vec3(0.0, 0.0, -0.5 * smallCapsuleLength - smallBegin);
    const Vec3 bigCapsuleTranslate   = Vec3(0.0, 0.0, -0.5 * bigCapsuleLength - midpoint);

    // Because of the way CapsuleSDF works, we need to create the capsules around the origin and then translate them into position.
    const Vec3 z0(0.0, 0.0, -0.5 * smallCapsuleLength);
    const Vec3 z1(0.0, 0.0, +0.5 * smallCapsuleLength);
    const Vec3 z2(0.0, 0.0, -0.5 * bigCapsuleLength);
    const Vec3 z3(0.0, 0.0, +0.5 * bigCapsuleLength);

    std::shared_ptr<ImpFunc> smallCapsule;
    std::shared_ptr<ImpFunc> bigCapsule;
    std::shared_ptr<ImpFunc> rod;

    smallCapsule = std::make_shared<EBGeometry::CapsuleSDF<Real>>(z1, z0, smallRadius);
    bigCapsule   = std::make_shared<EBGeometry::CapsuleSDF<Real>>(z2, z3, bigRadius);

    smallCapsule = EBGeometry::Translate<Real>(smallCapsule, smallCapsuleTranslate);
    bigCapsule   = EBGeometry::Translate<Real>(bigCapsule, bigCapsuleTranslate);
    rod          = EBGeometry::SmoothUnion<Real>(smallCapsule, bigCapsule, rodSmooth);

    if (rodPlateau) {
      const Real plateauCurvature = bigRadius - rodPlateauRadius;
      const Vec3 zHi(0.0, 0.0, plateauCurvature);
      const Vec3 zLo(0.0, 0.0, -plateauCurvature);

      std::shared_pt<ImpFunc> cylinder;
      std::shared_pt<ImpFunc> torus;
      std::shared_pt<ImpFunc> disk;

      cylinder = std::make_shared<EBGeometry::CylinderSDF<Real>>(zLo, zHi, rodPlateauRadius);
      torus    = std::make_shared<EBGeoemtry::TorusSDF<Real>>(Vec3::zero(), rodPlateauRadius, plateauCurvature);
      disk     = EBGeometry::Union<Real>(cylinder, torus);
      disk     = EBGeometry::Elongate<Real>(
        disk,
        Vec3(0.0, 0.0, 0.5 * rodPlateauRadius)); //Making sure that the plateau reaches the wide part of the big rod
      disk = EBGeometry::Translate<Real>(disk, Vec3(0.0, 0.0, -(plateauCurvature + 0.5 * rodPlateauRadius + midpoint)));
      rod  = EBGeometry::SmoothUnion<Real>(bigCapsule, disk, 0.0);
      rod  = EBGeometry::SmoothUnion<Real>(rod, smallCapsule, rodSmooth);
    }

    implicitFunctions.emplace_back(rod);
  }

  // Combine the rod and the needle using a CSG union. Then orient and translate them into the user-specified position.
  if (implicitFunctions.size() > 0) {
    auto rodNeedleUnion = EBGeometry::SmoothUnion<Real>(implicitFunctions, rodNeedleSmooth);

    // Rotate into user-specified orientation.
    if (orientation == "+x") {
      rodNeedleUnion = EBGeometry::Rotate<Real>(rodNeedleUnion, 90.0, 1);
    }
    else if (orientation == "-x") {
      rodNeedleUnion = EBGeometry::Rotate<Real>(rodNeedleUnion, -90.0, 1);
    }
    else if (orientation == "+y") {
      rodNeedleUnion = EBGeometry::Rotate<Real>(rodNeedleUnion, -90.0, 0);
    }
    else if (orientation == "-y") {
      rodNeedleUnion = EBGeometry::Rotate<Real>(rodNeedleUnion, 90.0, 0);
    }
    else if (orientation == "+z") {
      rodNeedleUnion = EBGeometry::Rotate<Real>(rodNeedleUnion, 180.0, 0);
    }
    else if (orientation == "-z") {
    }
    else {
      MayDay::Error("RodNeedleDisk::defineRodNeedle - logic bust in orientation. Must be -x, +x, -y, ....");
    }

    // Translate to user-specified position.
    rodNeedleUnion = EBGeometry::Translate<Real>(rodNeedleUnion, translate);

    // Turn the geometry into Chombo-speak.
    const RefCountedPtr<BaseIF> implicitFunction = RefCountedPtr<BaseIF>(new EBGeometryIF<Real>(rodNeedleUnion, true));

    m_electrodes.push_back(Electrode(implicitFunction, rodLive));
  }
}

void
RodNeedleDisk::defineDisk()
{
  CH_TIME("RodNeedleDisk::defineDisk");

  ParmParse pp("RodNeedleDisk");

  bool useDisk  = false;
  bool diskLive = false;

  Real diskPoint     = -1.0;
  Real diskThickness = -1.0;
  Real diskCurvature = -1.0;
  Real diskRadius    = -1.0;

  Vector<Real> v;
  std::string  orientation = "+z";
  Vec3         translate   = Vec3::zero();

  pp.get("use_disk", useDisk);
  pp.get("disk_live", diskLive);
  pp.get("disk_point", diskPoint);
  pp.get("disk_radius", diskRadius);
  pp.get("disk_curvature", diskCurvature);
  pp.get("disk_extra_thickness", diskThickness);
  pp.get("orientation", orientation);
  pp.getarr("translate", v, 0, SpaceDim);

  for (int dir = 0; dir < SpaceDim; dir++) {
    translate[dir] = v[dir];
  }

  CH_assert(diskCurvature > 0.0);
  CH_assert(diskRadius > 0.0);
  CH_assert(diskThickness >= 0.0);

  if (useDisk) {
    const Vec3 zHi(0.0, 0.0, 0.5 * diskCurvature);
    const Vec3 zLo(0.0, 0.0, -0.5 * diskCurvature);

    std::shared_ptr<ImpFunc> cylinder;
    std::shared_ptr<ImpFunc> torus;
    std::shared_ptr<ImpFunc> disk;

    cylinder = std::make_shared<EBGeometry::CylinderSDF<Real>>(zLo, zHi, diskRadius);
    torus    = std::make_shared<EBGeometry::TorusSDF<Real>>(Vec3::zero(), diskRadius, 0.5 * diskCurvature);
    disk     = EBGeometry::Union<Real>(cylinder, torus);
    disk     = EBGeometry::Elongate<Real>(disk, Vec3(0.0, 0.0, 0.5 * diskThickness));
    disk     = EBGeometry::Translate<Real>(disk, Vec3(0.0, 0.0, -diskPoint));

    // Rotate into user-specified orientation.
    if (orientation == "+x") {
      disk = EBGeometry::Rotate<Real>(disk, 90.0, 1);
    }
    else if (orientation == "-x") {
      disk = EBGeometry::Rotate<Real>(disk, -90.0, 1);
    }
    else if (orientation == "+y") {
      disk = EBGeometry::Rotate<Real>(disk, -90.0, 0);
    }
    else if (orientation == "-y") {
      disk = EBGeometry::Rotate<Real>(disk, 90.0, 0);
    }
    else if (orientation == "+z") {
      disk = EBGeometry::Rotate<Real>(disk, 180.0, 0);
    }
    else if (orientation == "-z") {
    }
    else {
      MayDay::Error("RodNeedleDisk::defineRodNeedle - logic bust in orientation. Must be -x, +x, -y, ....");
    }

    // Translate to user-specified position.
    disk = EBGeometry::Translate<Real>(disk, translate);

    // Turn the geometry into Chombo-speak.
    const RefCountedPtr<BaseIF> implicitFunction = RefCountedPtr<BaseIF>(new EBGeometryIF<Real>(disk, true));

    m_electrodes.push_back(Electrode(implicitFunction, diskLive));
  }
}

#include <CD_NamespaceFooter.H>
