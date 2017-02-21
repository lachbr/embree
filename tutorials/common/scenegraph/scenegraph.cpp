// ======================================================================== //
// Copyright 2009-2017 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include "scenegraph.h"
#include "xml_loader.h"
#include "xml_writer.h"
#include "obj_loader.h"
#include "ply_loader.h"
#include "corona_loader.h"

namespace embree
{
  Ref<SceneGraph::Node> SceneGraph::load(const FileName& filename, const bool singleObject)
  {
    if      (toLowerCase(filename.ext()) == std::string("obj" )) return loadOBJ(filename, false, singleObject);
    else if (toLowerCase(filename.ext()) == std::string("ply" )) return loadPLY(filename);
    else if (toLowerCase(filename.ext()) == std::string("xml" )) return loadXML(filename);
    else if (toLowerCase(filename.ext()) == std::string("scn" )) return loadCorona(filename);
    else throw std::runtime_error("unknown scene format: " + filename.ext());
  }

  void SceneGraph::store(Ref<SceneGraph::Node> root, const FileName& filename, bool embedTextures)
  {
    if (toLowerCase(filename.ext()) == std::string("xml")) {
      storeXML(root,filename,embedTextures);
    }
    else
      throw std::runtime_error("unknown scene format: " + filename.ext());
  }

  void SceneGraph::Node::resetNode(std::set<Ref<Node>>& done)
  {
    indegree = 0;
    closed = true;
  }

  void SceneGraph::TransformNode::resetNode(std::set<Ref<Node>>& done)
  {
    if (done.find(this) != done.end()) return;
    else done.insert(this);
    indegree = 0;
    closed = false;
    child->resetNode(done);
  }

  void SceneGraph::GroupNode::resetNode(std::set<Ref<Node>>& done)
  {
    if (done.find(this) != done.end()) return;
    else done.insert(this);
    indegree = 0;
    closed = false;
    for (auto& c : children)
      c->resetNode(done);
  }

  void SceneGraph::Node::calculateInDegree() {
    indegree++;
  }

  void SceneGraph::TransformNode::calculateInDegree()
  {
    indegree++;
    if (indegree == 1) {
      child->calculateInDegree();
      if (spaces.size() > 1) child->calculateInDegree(); // break instance up when motion blur is used
    }
  }

  void SceneGraph::GroupNode::calculateInDegree()
  {
    indegree++;
    if (indegree == 1) {
      for (auto&  c : children)
        c->calculateInDegree();
    }
  }

  bool SceneGraph::Node::calculateClosed() 
  {
    assert(indegree);
    closed = true;
    return closed && (indegree == 1);
  }

  bool SceneGraph::TransformNode::calculateClosed() 
  {
    assert(indegree);
    closed = child->calculateClosed();
    return closed && (indegree == 1);
  }

  bool SceneGraph::GroupNode::calculateClosed()
  {
    assert(indegree);
    closed = true;
    for (auto c : children)
      closed &= c->calculateClosed();
    return closed && (indegree == 1);
  }
  
  void SceneGraph::TriangleMeshNode::verify() const
  {
    const size_t N = numVertices();
    for (const auto& p : positions) 
      if (p.size() != N) 
        THROW_RUNTIME_ERROR("incompatible vertex array sizes");
    if (normals.size() && normals.size() != N) THROW_RUNTIME_ERROR("incompatible vertex array sizes");
    if (texcoords.size() && texcoords.size() != N) THROW_RUNTIME_ERROR("incompatible vertex array sizes");
    for (auto tri : triangles) {
      if (size_t(tri.v0) >= N || size_t(tri.v1) >= N || size_t(tri.v2) >= N)
        THROW_RUNTIME_ERROR("invalid triangle");
    }
  }

  void SceneGraph::QuadMeshNode::verify() const
  {
    const size_t N = numVertices();
    for (const auto& p : positions) 
      if (p.size() != N) 
        THROW_RUNTIME_ERROR("incompatible vertex array sizes");
    if (normals.size() && normals.size() != N) THROW_RUNTIME_ERROR("incompatible vertex array sizes");
    if (texcoords.size() && texcoords.size() != N) THROW_RUNTIME_ERROR("incompatible vertex array sizes");
    for (auto quad : quads) {
      if (size_t(quad.v0) >= N || size_t(quad.v1) >= N || size_t(quad.v2) >= N || size_t(quad.v3) >= N)
        THROW_RUNTIME_ERROR("invalid quad");
    }
  }

  void SceneGraph::SubdivMeshNode::verify() const
  {
    const size_t N = numPositions();
    for (const auto& p : positions) 
      if (p.size() != N) 
        THROW_RUNTIME_ERROR("incompatible position array sizes");
    for (auto i : position_indices) 
      if (size_t(i) >= N) THROW_RUNTIME_ERROR("invalid position index array");
    for (auto i : normal_indices) 
      if (size_t(i) >= normals.size()) THROW_RUNTIME_ERROR("invalid normal index array");
    for (auto i : texcoord_indices) 
      if (size_t(i) >= texcoords.size()) THROW_RUNTIME_ERROR("invalid texcoord index array");
    for (auto i : holes) 
      if (size_t(i) >= verticesPerFace.size()) THROW_RUNTIME_ERROR("invalid hole index array");
    for (auto crease : edge_creases) 
      if (max(size_t(crease.x),size_t(crease.y)) >= N) THROW_RUNTIME_ERROR("invalid edge crease array");
    if (edge_crease_weights.size() != edge_creases.size())
      THROW_RUNTIME_ERROR("invalid edge crease weight array");
    for (auto crease : vertex_creases) 
      if (size_t(crease) >= N) THROW_RUNTIME_ERROR("invalid vertex crease array");
    if (vertex_crease_weights.size() != vertex_creases.size())
      THROW_RUNTIME_ERROR("invalid vertex crease weight array");
  }

  void SceneGraph::LineSegmentsNode::verify() const
  {
    const size_t N = numVertices();
    for (const auto& p : positions) 
      if (p.size() != N) 
        THROW_RUNTIME_ERROR("incompatible vertex array sizes");
    for (auto i : indices)
      if (size_t(i) >= N)
        THROW_RUNTIME_ERROR("invalid line segment");
  }

  void SceneGraph::HairSetNode::verify() const
  {
    const size_t N = numVertices();
    for (const auto& p : positions) 
      if (p.size() != N) 
        THROW_RUNTIME_ERROR("incompatible vertex array sizes");
    for (auto hair : hairs)
      if (size_t(hair.vertex) >= N)
        THROW_RUNTIME_ERROR("invalid hair");
  }
  
  void SceneGraph::extend_animation(Ref<SceneGraph::Node> node0, Ref<SceneGraph::Node> node1)
  {
    if (node0 == node1) return;
      
    if (Ref<SceneGraph::TransformNode> xfmNode0 = node0.dynamicCast<SceneGraph::TransformNode>()) 
    {
      if (Ref<SceneGraph::TransformNode> xfmNode1 = node1.dynamicCast<SceneGraph::TransformNode>()) 
      {
        xfmNode0->spaces.add(xfmNode1->spaces);
        extend_animation(xfmNode0->child, xfmNode1->child);
      } 
      else THROW_RUNTIME_ERROR("incompatible scene graph"); 
    }
    else if (Ref<SceneGraph::GroupNode> groupNode0 = node0.dynamicCast<SceneGraph::GroupNode>()) 
    {
      if (Ref<SceneGraph::GroupNode> groupNode1 = node1.dynamicCast<SceneGraph::GroupNode>()) 
      {
        if (groupNode0->children.size() != groupNode1->children.size()) 
          THROW_RUNTIME_ERROR("incompatible scene graph");

        for (size_t i=0; i<groupNode0->children.size(); i++) 
          extend_animation(groupNode0->children[i],groupNode1->children[i]);
      } 
      else THROW_RUNTIME_ERROR("incompatible scene graph"); 
    }
    else if (Ref<SceneGraph::TriangleMeshNode> mesh0 = node0.dynamicCast<SceneGraph::TriangleMeshNode>()) 
    {
      if (Ref<SceneGraph::TriangleMeshNode> mesh1 = node1.dynamicCast<SceneGraph::TriangleMeshNode>())
      {
        if (mesh0->numVertices() != mesh1->numVertices())
          THROW_RUNTIME_ERROR("incompatible scene graph");

        for (auto& p : mesh1->positions)
          mesh0->positions.push_back(std::move(p));
      } 
      else THROW_RUNTIME_ERROR("incompatible scene graph"); 
    }
    else if (Ref<SceneGraph::QuadMeshNode> mesh0 = node0.dynamicCast<SceneGraph::QuadMeshNode>()) 
    {
      if (Ref<SceneGraph::QuadMeshNode> mesh1 = node1.dynamicCast<SceneGraph::QuadMeshNode>())
      {
        if (mesh0->numVertices() != mesh1->numVertices())
          THROW_RUNTIME_ERROR("incompatible scene graph");

        for (auto& p : mesh1->positions)
          mesh0->positions.push_back(std::move(p));
      } 
      else THROW_RUNTIME_ERROR("incompatible scene graph"); 
    }
    else if (Ref<SceneGraph::HairSetNode> mesh0 = node0.dynamicCast<SceneGraph::HairSetNode>()) 
    {
      if (Ref<SceneGraph::HairSetNode> mesh1 = node1.dynamicCast<SceneGraph::HairSetNode>()) 
      {
        if (mesh0->numVertices() != mesh1->numVertices())
          THROW_RUNTIME_ERROR("incompatible scene graph");

        for (auto& p : mesh1->positions)
          mesh0->positions.push_back(std::move(p));
      }
      else THROW_RUNTIME_ERROR("incompatible scene graph"); 
    }
    else if (Ref<SceneGraph::SubdivMeshNode> mesh0 = node0.dynamicCast<SceneGraph::SubdivMeshNode>()) 
    {
      if (Ref<SceneGraph::SubdivMeshNode> mesh1 = node1.dynamicCast<SceneGraph::SubdivMeshNode>()) 
      {
        if (mesh0->numPositions() != mesh1->numPositions())
          THROW_RUNTIME_ERROR("incompatible scene graph");
        if (mesh0->verticesPerFace != mesh1->verticesPerFace)
          THROW_RUNTIME_ERROR("incompatible scene graph");

        for (auto& p : mesh1->positions)
          mesh0->positions.push_back(std::move(p));
      }
      else THROW_RUNTIME_ERROR("incompatible scene graph"); 
    }
  }

  void SceneGraph::optimize_animation(Ref<SceneGraph::Node> node)
  {
    if (Ref<SceneGraph::TransformNode> xfmNode = node.dynamicCast<SceneGraph::TransformNode>()) 
      optimize_animation(xfmNode->child);

    else if (Ref<SceneGraph::GroupNode> groupNode = node.dynamicCast<SceneGraph::GroupNode>()) 
    {
      for (const auto& child : groupNode->children) 
        optimize_animation(child);
    }
    else if (Ref<SceneGraph::TriangleMeshNode> mesh = node.dynamicCast<SceneGraph::TriangleMeshNode>()) 
    {
      bool equal = true;
      for (size_t i=1; i<mesh->numTimeSteps(); i++)
        equal &= mesh->positions[0] == mesh->positions[i];

      if (equal)
        mesh->positions.resize(1);
    }
    else if (Ref<SceneGraph::QuadMeshNode> mesh = node.dynamicCast<SceneGraph::QuadMeshNode>()) 
    {
      bool equal = true;
      for (size_t i=1; i<mesh->numTimeSteps(); i++)
        equal &= mesh->positions[0] == mesh->positions[i];

      if (equal)
        mesh->positions.resize(1);
    }
    else if (Ref<SceneGraph::HairSetNode> mesh = node.dynamicCast<SceneGraph::HairSetNode>()) 
    {
      bool equal = true;
      for (size_t i=1; i<mesh->numTimeSteps(); i++)
        equal &= mesh->positions[0] == mesh->positions[i];

      if (equal)
        mesh->positions.resize(1);
    }
    else if (Ref<SceneGraph::SubdivMeshNode> mesh = node.dynamicCast<SceneGraph::SubdivMeshNode>()) 
    {
      bool equal = true;
      for (size_t i=1; i<mesh->numTimeSteps(); i++)
        equal &= mesh->positions[0] == mesh->positions[i];

      if (equal)
        mesh->positions.resize(1);
    }
  }

  void SceneGraph::set_motion_vector(Ref<SceneGraph::Node> node, const Vec3fa& dP)
  {
    if (Ref<SceneGraph::TransformNode> xfmNode = node.dynamicCast<SceneGraph::TransformNode>()) {
      set_motion_vector(xfmNode->child, dP);
    }
    else if (Ref<SceneGraph::GroupNode> groupNode = node.dynamicCast<SceneGraph::GroupNode>()) 
    {
      for (size_t i=0; i<groupNode->children.size(); i++) 
        set_motion_vector(groupNode->children[i],dP);
    }
    else if (Ref<SceneGraph::TriangleMeshNode> mesh = node.dynamicCast<SceneGraph::TriangleMeshNode>()) 
    {
      avector<Vec3fa> positions1;
      for (auto P : mesh->positions.back()) 
        positions1.push_back(P+dP);
      mesh->positions.push_back(std::move(positions1));
    }
    else if (Ref<SceneGraph::QuadMeshNode> mesh = node.dynamicCast<SceneGraph::QuadMeshNode>()) 
    {
      avector<Vec3fa> positions1;
      for (auto P : mesh->positions.back()) 
        positions1.push_back(P+dP);
      mesh->positions.push_back(std::move(positions1));
    }
    else if (Ref<SceneGraph::HairSetNode> mesh = node.dynamicCast<SceneGraph::HairSetNode>()) 
    {
      avector<Vec3fa> positions1;
      for (auto P : mesh->positions.back()) 
        positions1.push_back(P+dP);
      mesh->positions.push_back(std::move(positions1));
    }
    else if (Ref<SceneGraph::LineSegmentsNode> mesh = node.dynamicCast<SceneGraph::LineSegmentsNode>()) 
    {
      avector<Vec3fa> positions1;
      for (auto P : mesh->positions.back()) 
        positions1.push_back(P+dP);
      mesh->positions.push_back(std::move(positions1));
    }
    else if (Ref<SceneGraph::SubdivMeshNode> mesh = node.dynamicCast<SceneGraph::SubdivMeshNode>()) 
    {
      avector<Vec3fa> positions1;
      for (auto P : mesh->positions.back()) 
        positions1.push_back(P+dP);
      mesh->positions.push_back(std::move(positions1));
    }
  }

  void SceneGraph::set_motion_vector(Ref<SceneGraph::Node> node, const avector<Vec3fa>& motion_vector)
  {
    if (Ref<SceneGraph::TransformNode> xfmNode = node.dynamicCast<SceneGraph::TransformNode>()) {
      set_motion_vector(xfmNode->child,motion_vector);
    }
    else if (Ref<SceneGraph::GroupNode> groupNode = node.dynamicCast<SceneGraph::GroupNode>()) 
    {
      for (size_t i=0; i<groupNode->children.size(); i++) 
        set_motion_vector(groupNode->children[i],motion_vector);
    }
    else if (Ref<SceneGraph::TriangleMeshNode> mesh = node.dynamicCast<SceneGraph::TriangleMeshNode>()) 
    {
      avector<Vec3fa> positions = std::move(mesh->positions[0]);
      mesh->positions.clear();
      for (size_t t=0; t<motion_vector.size(); t++) {
        avector<Vec3fa> tpositions(positions.size());
        for (size_t i=0; i<positions.size(); i++) tpositions[i] = positions[i] + motion_vector[t];
        mesh->positions.push_back(std::move(tpositions));
      }
    }
    else if (Ref<SceneGraph::QuadMeshNode> mesh = node.dynamicCast<SceneGraph::QuadMeshNode>()) 
    {
      avector<Vec3fa> positions = std::move(mesh->positions[0]);
      mesh->positions.clear();
      for (size_t t=0; t<motion_vector.size(); t++) {
        avector<Vec3fa> tpositions(positions.size());
        for (size_t i=0; i<positions.size(); i++) tpositions[i] = positions[i] + motion_vector[t];
        mesh->positions.push_back(std::move(tpositions));
      }
    }
    else if (Ref<SceneGraph::HairSetNode> mesh = node.dynamicCast<SceneGraph::HairSetNode>()) 
    {
      avector<Vec3fa> positions = std::move(mesh->positions[0]);
      mesh->positions.clear();
      for (size_t t=0; t<motion_vector.size(); t++) {
        avector<Vec3fa> tpositions(positions.size());
        for (size_t i=0; i<positions.size(); i++) tpositions[i] = positions[i] + motion_vector[t];
        mesh->positions.push_back(std::move(tpositions));
      }
    }
    else if (Ref<SceneGraph::LineSegmentsNode> mesh = node.dynamicCast<SceneGraph::LineSegmentsNode>()) 
    {
      avector<Vec3fa> positions = std::move(mesh->positions[0]);
      mesh->positions.clear();
      for (size_t t=0; t<motion_vector.size(); t++) {
        avector<Vec3fa> tpositions(positions.size());
        for (size_t i=0; i<positions.size(); i++) tpositions[i] = positions[i] + motion_vector[t];
        mesh->positions.push_back(std::move(tpositions));
      }
    }
    else if (Ref<SceneGraph::SubdivMeshNode> mesh = node.dynamicCast<SceneGraph::SubdivMeshNode>()) 
    {
      avector<Vec3fa> positions = std::move(mesh->positions[0]);
      mesh->positions.clear();
      for (size_t t=0; t<motion_vector.size(); t++) {
        avector<Vec3fa> tpositions(positions.size());
        for (size_t i=0; i<positions.size(); i++) tpositions[i] = positions[i] + motion_vector[t];
        mesh->positions.push_back(std::move(tpositions));
      }
    }
  }

  void SceneGraph::resize_randomly(RandomSampler& sampler, Ref<Node> node, const size_t N)
  {
     if (Ref<SceneGraph::TransformNode> xfmNode = node.dynamicCast<SceneGraph::TransformNode>()) {
       resize_randomly(sampler,xfmNode->child, N);
    }
    else if (Ref<SceneGraph::GroupNode> groupNode = node.dynamicCast<SceneGraph::GroupNode>()) 
    {
      for (size_t i=0; i<groupNode->children.size(); i++) 
        resize_randomly(sampler,groupNode->children[i],N);
    }
    else if (Ref<SceneGraph::TriangleMeshNode> mesh = node.dynamicCast<SceneGraph::TriangleMeshNode>()) 
    {
      if (!mesh->triangles.size()) return;
      for (size_t i=0; i<N; i++) {
        size_t j = RandomSampler_getInt(sampler)%(min(mesh->triangles.size(),N));
        if (i < mesh->triangles.size()) std::swap(mesh->triangles[i],mesh->triangles[j]);
        else                            mesh->triangles.push_back(mesh->triangles[j]);
      }
    }
    else if (Ref<SceneGraph::QuadMeshNode> mesh = node.dynamicCast<SceneGraph::QuadMeshNode>()) 
    {
      if (!mesh->quads.size()) return;
      for (size_t i=0; i<N; i++) {
        size_t j = RandomSampler_getInt(sampler)%(min(mesh->quads.size(),N));
        if (i < mesh->quads.size()) std::swap(mesh->quads[i],mesh->quads[j]);
        else                        mesh->quads.push_back(mesh->quads[j]);
      }
    }
    else if (Ref<SceneGraph::HairSetNode> mesh = node.dynamicCast<SceneGraph::HairSetNode>()) 
    {
      if (!mesh->hairs.size()) return;
      for (size_t i=0; i<N; i++) {
        size_t j = RandomSampler_getInt(sampler)%(min(mesh->hairs.size(),N));
        if (i < mesh->hairs.size()) std::swap(mesh->hairs[i],mesh->hairs[j]);
        else                        mesh->hairs.push_back(mesh->hairs[j]);
      }
    }
    else if (Ref<SceneGraph::SubdivMeshNode> mesh = node.dynamicCast<SceneGraph::SubdivMeshNode>()) 
    {
      if (mesh->verticesPerFace.size() <= N) return;
      mesh->verticesPerFace.resize(N);
    }
  }

  std::pair<int,int> quad_index2(int p, int a0, int a1, int b0, int b1)
  {
    if      (b0 == a0) return std::make_pair(p-1,b1);
    else if (b0 == a1) return std::make_pair(p+0,b1);
    else if (b1 == a0) return std::make_pair(p-1,b0);
    else if (b1 == a1) return std::make_pair(p+0,b0);
    else return std::make_pair(0,-1);
  }
  
  std::pair<int,int> quad_index3(int a0, int a1, int a2, int b0, int b1, int b2)
  {
    if      (b0 == a0) return quad_index2(0,a2,a1,b1,b2);
    else if (b0 == a1) return quad_index2(1,a0,a2,b1,b2);
    else if (b0 == a2) return quad_index2(2,a1,a0,b1,b2);
    else if (b1 == a0) return quad_index2(0,a2,a1,b0,b2);
    else if (b1 == a1) return quad_index2(1,a0,a2,b0,b2);
    else if (b1 == a2) return quad_index2(2,a1,a0,b0,b2);
    else if (b2 == a0) return quad_index2(0,a2,a1,b0,b1);
    else if (b2 == a1) return quad_index2(1,a0,a2,b0,b1);
    else if (b2 == a2) return quad_index2(2,a1,a0,b0,b1);
    else return std::make_pair(0,-1);
  }
  
  Ref<SceneGraph::Node> SceneGraph::convert_triangles_to_quads(Ref<SceneGraph::Node> node)
  {
    if (Ref<SceneGraph::TransformNode> xfmNode = node.dynamicCast<SceneGraph::TransformNode>()) {
      xfmNode->child = convert_triangles_to_quads(xfmNode->child);
    } 
    else if (Ref<SceneGraph::GroupNode> groupNode = node.dynamicCast<SceneGraph::GroupNode>()) 
    {
      for (size_t i=0; i<groupNode->children.size(); i++) 
        groupNode->children[i] = convert_triangles_to_quads(groupNode->children[i]);
    }
    else if (Ref<SceneGraph::TriangleMeshNode> tmesh = node.dynamicCast<SceneGraph::TriangleMeshNode>()) 
    {
      SceneGraph::QuadMeshNode* qmesh = new SceneGraph::QuadMeshNode(tmesh->material);

      for (auto& p : tmesh->positions)
        qmesh->positions.push_back(p);

      qmesh->normals = tmesh->normals;
      qmesh->texcoords = tmesh->texcoords;

      for (size_t i=0; i<tmesh->triangles.size(); i++)
      {
        const int a0 = tmesh->triangles[i+0].v0;
        const int a1 = tmesh->triangles[i+0].v1;
        const int a2 = tmesh->triangles[i+0].v2;
        if (i+1 == tmesh->triangles.size()) {
          qmesh->quads.push_back(SceneGraph::QuadMeshNode::Quad(a0,a1,a2,a2));
          continue;
        }

        const int b0 = tmesh->triangles[i+1].v0;
        const int b1 = tmesh->triangles[i+1].v1;
        const int b2 = tmesh->triangles[i+1].v2;
        const std::pair<int,int> q = quad_index3(a0,a1,a2,b0,b1,b2);
        const int a3 = q.second;
        if (a3 == -1) {
          qmesh->quads.push_back(SceneGraph::QuadMeshNode::Quad(a0,a1,a2,a2));
          continue;
        }

        if      (q.first == -1) qmesh->quads.push_back(SceneGraph::QuadMeshNode::Quad(a1,a2,a3,a0));
        else if (q.first ==  0) qmesh->quads.push_back(SceneGraph::QuadMeshNode::Quad(a3,a1,a2,a0));
        else if (q.first ==  1) qmesh->quads.push_back(SceneGraph::QuadMeshNode::Quad(a0,a1,a3,a2));
        else if (q.first ==  2) qmesh->quads.push_back(SceneGraph::QuadMeshNode::Quad(a1,a2,a3,a0)); 
        i++;
      }
      return qmesh;
    }
    return node;
  }

  Ref<SceneGraph::Node> SceneGraph::convert_quads_to_subdivs(Ref<SceneGraph::Node> node)
  {
    if (Ref<SceneGraph::TransformNode> xfmNode = node.dynamicCast<SceneGraph::TransformNode>()) {
      xfmNode->child = convert_quads_to_subdivs(xfmNode->child);
    } 
    else if (Ref<SceneGraph::GroupNode> groupNode = node.dynamicCast<SceneGraph::GroupNode>()) 
    {
      for (size_t i=0; i<groupNode->children.size(); i++) 
        groupNode->children[i] = convert_quads_to_subdivs(groupNode->children[i]);
    }
    else if (Ref<SceneGraph::QuadMeshNode> tmesh = node.dynamicCast<SceneGraph::QuadMeshNode>()) 
    {
      SceneGraph::SubdivMeshNode* smesh = new SceneGraph::SubdivMeshNode(tmesh->material);

      for (auto& p : tmesh->positions)
        smesh->positions.push_back(p);

      for (size_t i=0; i<tmesh->quads.size(); i++) {
        smesh->position_indices.push_back(tmesh->quads[i].v0);
        smesh->position_indices.push_back(tmesh->quads[i].v1);
        smesh->position_indices.push_back(tmesh->quads[i].v2);
        if (tmesh->quads[i].v2 != tmesh->quads[i].v3)
          smesh->position_indices.push_back(tmesh->quads[i].v3);
      }
      
      smesh->normals = tmesh->normals;
      if (smesh->normals.size())
        smesh->normal_indices = smesh->position_indices;
      
      smesh->texcoords = tmesh->texcoords;
      if (smesh->texcoords.size())
        smesh->texcoord_indices = smesh->position_indices;
      
      for (size_t i=0; i<tmesh->quads.size(); i++) 
        smesh->verticesPerFace.push_back(3 + (int)(tmesh->quads[i].v2 != tmesh->quads[i].v3));

      return smesh;
    }
    return node;
  }

  Ref<SceneGraph::Node> SceneGraph::convert_bezier_to_lines(Ref<SceneGraph::Node> node)
  {
    if (Ref<SceneGraph::TransformNode> xfmNode = node.dynamicCast<SceneGraph::TransformNode>()) {
      xfmNode->child = convert_bezier_to_lines(xfmNode->child);
    }
    else if (Ref<SceneGraph::GroupNode> groupNode = node.dynamicCast<SceneGraph::GroupNode>()) 
    {
      for (size_t i=0; i<groupNode->children.size(); i++) 
        groupNode->children[i] = convert_bezier_to_lines(groupNode->children[i]);
    }
    else if (Ref<SceneGraph::HairSetNode> hmesh = node.dynamicCast<SceneGraph::HairSetNode>()) 
    {
      SceneGraph::LineSegmentsNode* lmesh = new SceneGraph::LineSegmentsNode(hmesh->material);

      for (auto& p : hmesh->positions)
        lmesh->positions.push_back(p);

      for (auto hair : hmesh->hairs) {
        lmesh->indices.push_back(hair.vertex+0);
        lmesh->indices.push_back(hair.vertex+1);
        lmesh->indices.push_back(hair.vertex+2);
      }
      return lmesh;
    }
    return node;
  }

  Ref<SceneGraph::Node> SceneGraph::convert_hair_to_curves(Ref<SceneGraph::Node> node)
  {
    if (Ref<SceneGraph::TransformNode> xfmNode = node.dynamicCast<SceneGraph::TransformNode>()) {
      xfmNode->child = convert_hair_to_curves(xfmNode->child);
    }
    else if (Ref<SceneGraph::GroupNode> groupNode = node.dynamicCast<SceneGraph::GroupNode>()) 
    {
      for (size_t i=0; i<groupNode->children.size(); i++) 
        groupNode->children[i] = convert_hair_to_curves(groupNode->children[i]);
    }
    else if (Ref<SceneGraph::HairSetNode> hmesh = node.dynamicCast<SceneGraph::HairSetNode>()) 
    {
      hmesh->hair = false;
      return hmesh.dynamicCast<SceneGraph::Node>();
    }
    return node;
  }

  Ref<SceneGraph::Node> SceneGraph::flatten(Ref<SceneGraph::Node> node, const Transformations& spaces)
  {
    if (Ref<SceneGraph::TransformNode> xfmNode = node.dynamicCast<SceneGraph::TransformNode>()) {
      return flatten(xfmNode->child, spaces*xfmNode->spaces);
    } 
    else if (Ref<SceneGraph::GroupNode> groupNode = node.dynamicCast<SceneGraph::GroupNode>()) {
      for (auto& child : groupNode->children) child = flatten(child,spaces);
    }
    else if (Ref<SceneGraph::TriangleMeshNode> mesh = node.dynamicCast<SceneGraph::TriangleMeshNode>()) {
      return new SceneGraph::TriangleMeshNode(mesh,spaces);
    }
    else if (Ref<SceneGraph::QuadMeshNode> mesh = node.dynamicCast<SceneGraph::QuadMeshNode>()) {
      return new SceneGraph::QuadMeshNode(mesh,spaces);
    }
    else if (Ref<SceneGraph::SubdivMeshNode> mesh = node.dynamicCast<SceneGraph::SubdivMeshNode>()) {
      return new SceneGraph::SubdivMeshNode(mesh,spaces);
    }
    else if (Ref<SceneGraph::LineSegmentsNode> mesh = node.dynamicCast<SceneGraph::LineSegmentsNode>()) {
      return new SceneGraph::LineSegmentsNode(mesh,spaces);
    }
    else if (Ref<SceneGraph::HairSetNode> mesh = node.dynamicCast<SceneGraph::HairSetNode>()) {
      return new SceneGraph::HairSetNode(mesh,spaces);
    }
    else {
      throw std::runtime_error("unsupported node type in SceneGraph::flatten");
    }
    return node;
  }

  struct SceneGraphFlattener
  {
    Ref<SceneGraph::Node> node;
    std::map<Ref<SceneGraph::Node>,Ref<SceneGraph::Node>> object_mapping;
    std::map<std::string,size_t> unique_id;
    
    SceneGraphFlattener (Ref<SceneGraph::Node> in, SceneGraph::InstancingMode instancing, const SceneGraph::Transformations& spaces)
    { 
      std::vector<Ref<SceneGraph::Node>> geometries;      
      if (instancing != SceneGraph::INSTANCING_NONE) 
      {
        std::cout << "extracting instances ";
        if (instancing == SceneGraph::INSTANCING_SCENE_GROUP) 
        {
          in->reset();
          in->calculateInDegree();
          in->calculateClosed();
        }
        convertInstances(geometries,in,spaces);
        std::cout << "[DONE] (" << geometries.size() << " instances, " << object_mapping.size() << " objects)" << std::endl;
      }
      else
        convertGeometries(geometries,in,spaces);

      convertLightsAndCameras(geometries,in,spaces);

      node = new SceneGraph::GroupNode(geometries);
    }

    std::string makeUniqueID(std::string id) 
    {
      if (id == "") id = "camera";
      std::map<std::string,size_t>::iterator i = unique_id.find(id);
      if (i == unique_id.end()) {
        unique_id[id] = 0;
        return id;
      }
      else {
        size_t n = ++unique_id[id];
        return id + "_" + std::to_string(n);
      }
    }

    void convertLightsAndCameras(std::vector<Ref<SceneGraph::Node>>& group, Ref<SceneGraph::Node> node, const SceneGraph::Transformations& spaces)
    {
      if (Ref<SceneGraph::TransformNode> xfmNode = node.dynamicCast<SceneGraph::TransformNode>()) {
        convertLightsAndCameras(group,xfmNode->child, spaces*xfmNode->spaces);
      } 
      else if (Ref<SceneGraph::GroupNode> groupNode = node.dynamicCast<SceneGraph::GroupNode>()) {
        for (const auto& child : groupNode->children) convertLightsAndCameras(group,child,spaces);
      }
      else if (Ref<SceneGraph::LightNode> lightNode = node.dynamicCast<SceneGraph::LightNode>()) {
        group.push_back(new SceneGraph::LightNode(lightNode->light->transform(spaces[0])));
      }
      else if (Ref<SceneGraph::PerspectiveCameraNode> cameraNode = node.dynamicCast<SceneGraph::PerspectiveCameraNode>()) {
        group.push_back(new SceneGraph::PerspectiveCameraNode(cameraNode,spaces[0],makeUniqueID(cameraNode->name)));
      }
    }

    void convertGeometries(std::vector<Ref<SceneGraph::Node>>& group, Ref<SceneGraph::Node> node, const SceneGraph::Transformations& spaces)
    {
      if (Ref<SceneGraph::TransformNode> xfmNode = node.dynamicCast<SceneGraph::TransformNode>()) {
        convertGeometries(group,xfmNode->child, spaces*xfmNode->spaces);
      } 
      else if (Ref<SceneGraph::GroupNode> groupNode = node.dynamicCast<SceneGraph::GroupNode>()) {
        for (const auto& child : groupNode->children) convertGeometries(group,child,spaces);
      }
      else if (Ref<SceneGraph::TriangleMeshNode> mesh = node.dynamicCast<SceneGraph::TriangleMeshNode>()) {
        group.push_back(new SceneGraph::TriangleMeshNode(mesh,spaces));
      }
      else if (Ref<SceneGraph::QuadMeshNode> mesh = node.dynamicCast<SceneGraph::QuadMeshNode>()) {
        group.push_back(new SceneGraph::QuadMeshNode(mesh,spaces));
      }
      else if (Ref<SceneGraph::SubdivMeshNode> mesh = node.dynamicCast<SceneGraph::SubdivMeshNode>()) {
        group.push_back(new SceneGraph::SubdivMeshNode(mesh,spaces));
      }
      else if (Ref<SceneGraph::LineSegmentsNode> mesh = node.dynamicCast<SceneGraph::LineSegmentsNode>()) {
        group.push_back(new SceneGraph::LineSegmentsNode(mesh,spaces));
      }
      else if (Ref<SceneGraph::HairSetNode> mesh = node.dynamicCast<SceneGraph::HairSetNode>()) {
        group.push_back(new SceneGraph::HairSetNode(mesh,spaces));
      }
    }

    Ref<SceneGraph::Node> lookupGeometries(Ref<SceneGraph::Node> node)
    {
      if (object_mapping.find(node) == object_mapping.end())
      {
        std::vector<Ref<SceneGraph::Node>> geometries;
        convertGeometries(geometries,node,one);
        
        if (geometries.size() == 1) object_mapping[node] = geometries[0];
        else object_mapping[node] = new SceneGraph::GroupNode(geometries);
      }
      
      return object_mapping[node];
    }

    void convertInstances(std::vector<Ref<SceneGraph::Node>>& group, Ref<SceneGraph::Node> node, const SceneGraph::Transformations& spaces)
    {
      if (node->isClosed()) {
        if (group.size() % 10000 == 0) std::cout << "." << std::flush;
        group.push_back(new SceneGraph::TransformNode(spaces,lookupGeometries(node)));
      }
      else if (Ref<SceneGraph::TransformNode> xfmNode = node.dynamicCast<SceneGraph::TransformNode>()) {
        convertInstances(group,xfmNode->child, spaces*xfmNode->spaces);
      } 
      else if (Ref<SceneGraph::GroupNode> groupNode = node.dynamicCast<SceneGraph::GroupNode>()) {
        for (const auto& child : groupNode->children) convertInstances(group,child,spaces);
      }
    }
  };

  Ref<SceneGraph::Node> SceneGraph::flatten(Ref<Node> node, InstancingMode mode, const Transformations& spaces) {
    return SceneGraphFlattener(node,mode,spaces).node;
  }

  Ref<SceneGraph::GroupNode> SceneGraph::flatten(Ref<SceneGraph::GroupNode> node, SceneGraph::InstancingMode mode, const SceneGraph::Transformations& spaces) {
    return flatten(node.dynamicCast<SceneGraph::Node>(),mode,spaces).dynamicCast<SceneGraph::GroupNode>();
  }
}
