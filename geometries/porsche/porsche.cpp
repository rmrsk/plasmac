/*!
  @file   porsche.cpp
  @brief  Implementation of porsche.H
  @author Robert Marskar
  @date   Jan. 2019
*/

#include "porsche.H"

#include <ParmParse.H>

#include "dcel_BoundingVolumes.H"
#include "dcel_mesh.H"
#include "dcel_parser.H"
#include "dcel_if.H"
#include "bvh_if.H"

using namespace dcel;

using precision = float;

using face   = faceT<precision>;
using mesh   = meshT<precision>;
using AABB   = AABBT<precision>;
using Sphere = BoundingSphereT<precision>;

using BV = Sphere;

porsche::porsche(){

  std::string filename;

  ParmParse pp("porsche");

  pp.get("mesh_file", filename);

  // Build the mesh and set default parameters. 
  auto m = std::make_shared<mesh>();
  parser::PLY<precision>::readASCII(*m, filename);
  m->sanityCheck();
  m->reconcile(VertexNormalWeight::Angle);
  // m->setSearchAlgorithm(SearchAlgorithm::Direct2); // Only for direct searches!
  // m->setInsideOutsideAlgorithm(InsideOutsideAlgorithm::CrossingNumber);


  // Creat the object and build the BVH.
  auto root = std::make_shared<NodeT<precision, face, BV> >(m->getFaces());
  root->topDownSortAndPartitionObjects(bvh_if<precision, BV>::defaultStopFunction,
				       bvh_if<precision, BV>::defaultPartitionFunction,
				       bvh_if<precision, BV>::defaultBVConstructor);
  
  RefCountedPtr<bvh_if<precision, BV> > bif = RefCountedPtr<bvh_if<precision, BV> > (new bvh_if<precision, BV>(root,true));

  //  bif->buildBVH();

  m_electrodes.push_back(electrode(bif, true));
  
}

porsche::~porsche(){
  
}
