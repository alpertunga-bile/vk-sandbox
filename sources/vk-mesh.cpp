#include "vk-mesh.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include <iostream>

VertexInputDescription Vertex::getVertexDescription()
{
	VertexInputDescription description = {};

	VkVertexInputBindingDescription mainBinding = {};
	mainBinding.binding = 0;
	mainBinding.stride = sizeof(Vertex);
	mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	description.bindings.push_back(mainBinding);

	VkVertexInputAttributeDescription positionAttribute = {};
	positionAttribute.binding = 0;
	positionAttribute.location = 0;
	positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	positionAttribute.offset = offsetof(Vertex, position);

	VkVertexInputAttributeDescription normalAttribute = {};
	normalAttribute.binding = 0;
	normalAttribute.location = 1;
	normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	normalAttribute.offset = offsetof(Vertex, normal);

	VkVertexInputAttributeDescription colorAttribute = {};
	colorAttribute.binding = 0;
	colorAttribute.location = 2;
	colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	colorAttribute.offset = offsetof(Vertex, color);

	VkVertexInputAttributeDescription uvAttribute = {};
	uvAttribute.binding = 0;
	uvAttribute.location = 3;
	uvAttribute.format = VK_FORMAT_R32G32_SFLOAT;
	uvAttribute.offset = offsetof(Vertex, uv);

	description.attributes.push_back(positionAttribute);
	description.attributes.push_back(normalAttribute);
	description.attributes.push_back(colorAttribute);
	description.attributes.push_back(uvAttribute);

	return description;
}

bool Mesh::loadFromObj(const char* filename)
{
	tinyobj::attrib_t attrib;

	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;

	std::string warning;
	std::string error;

	tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error, filename, nullptr);

	if (!warning.empty())
	{
		std::cout << "::WARNING:: " << warning << "\n";
	}

	if (!error.empty())
	{
		std::cerr << "::ERROR:: " << error << "\n";
		return false;
	}

	size_t shapeSize = shapes.size();
	glm::vec3 defaultColor = { 0.5f, 0.5f, 0.5f };

	for (size_t s = 0; s < shapeSize; s++)
	{
		size_t index_offset = 0;
		size_t vertexSize = shapes[s].mesh.num_face_vertices.size();
		for (size_t f = 0; f < vertexSize; f++)
		{
			int fv = 3;

			for (size_t v = 0; v < fv; v++)
			{
				tinyobj::index_t index = shapes[s].mesh.indices[index_offset + v];

				bool hasNormal = (attrib.normals.size() > 0);

				tinyobj::real_t vx = attrib.vertices[3 * index.vertex_index + 0];
				tinyobj::real_t vy = attrib.vertices[3 * index.vertex_index + 1];
				tinyobj::real_t vz = attrib.vertices[3 * index.vertex_index + 2];

				tinyobj::real_t nx = 0;
				tinyobj::real_t ny = 0;
				tinyobj::real_t nz = 0;

				if (hasNormal)
				{
					nx = attrib.normals[3 * index.normal_index + 0];
					ny = attrib.normals[3 * index.normal_index + 1];
					nz = attrib.normals[3 * index.normal_index + 2];
				}
			
				Vertex newVertex;
				newVertex.position.x = vx;
				newVertex.position.y = vy;
				newVertex.position.z = vz;
				
				newVertex.normal.x = nx;
				newVertex.normal.y = ny;
				newVertex.normal.z = nz;

				newVertex.color = hasNormal ? newVertex.normal : defaultColor;

				tinyobj::real_t ux = attrib.texcoords[2 * index.texcoord_index + 0];
				tinyobj::real_t uy = attrib.texcoords[2 * index.texcoord_index + 1];

				newVertex.uv.x = ux;
				newVertex.uv.y = 1 - uy;

				_vertices.push_back(newVertex);
			}

			index_offset += fv;
		}
	}

	return true;
}
