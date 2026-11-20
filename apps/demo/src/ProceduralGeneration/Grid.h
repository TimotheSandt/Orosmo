#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <array>
#include <functional>

#include "engine/renderer/mesh.hpp"

struct Vertex
{
    glm::vec3 Position = glm::vec3(0.0f);
    glm::vec3 Normal = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 Color = glm::vec3(0.45f, 0.7f, 0.35f);
};

class Grid
{
  public:
    Grid();
    Grid(float size_x, float size_z, int resolution_x, int resolution_z);
    Grid(const Grid &other);
    ~Grid();

    void Destroy();

    void init(float size_x, float size_z, int resolution_x, int resolution_z);

    void GeneratePoints();
    void GenerateTriangles();
    void GenerateNormals();
    void GenerateMesh();

    void TransformPoints(const std::function<void(Vertex &, unsigned int)> &func);


    unsigned int GetResolutionX() { return this->resolution_x; }
    unsigned int GetResolutionY() { return this->resolution_z; }
    std::vector<Vertex> GetPoints() { return this->points; }
    std::vector<std::array<unsigned int, 3>> GetTriangles() { return this->triangles; }
    // Données du maillage produites par GenerateMesh (sommets position/normale/couleur, index) ; le GPU est l'affaire du monde.
    const engine::renderer::MeshData &GetMeshData() const { return this->meshData; }

    unsigned int GetPointCount() { return this->points.size(); }
    unsigned int GetTriangleCount() { return this->triangles.size(); }

  private:
    float size_x;
    float size_z;
    unsigned int resolution_x;
    unsigned int resolution_z;
    std::vector<Vertex> points;
    std::vector<std::array<unsigned int, 3>> triangles;

    engine::renderer::MeshData meshData;
};
