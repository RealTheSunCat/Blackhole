#pragma once

#include "smg/BaseObject.h"
#include "io/BmdFile.h"
#include "rendering/Texture.h"
#include "rendering/GX.h"

#include <vector>
#include <future>

struct DrawCall
{
    uint32_t primType;
    GX::VtxFmt_t vertexFormat;
    uint32_t srcOffset;
    uint32_t vertexCount;
};

class ObjectRenderer
{
    BmdFile m_model;
    RarcFile m_rarc;

    BaseObject* m_object;
    std::vector<Texture> m_textures;

    glm::vec3& m_translation;
    glm::vec3& m_rotation;
    glm::vec3& m_scale;

    QString m_modelName;

    void decodeTexture(GX::BTI_Texture& tex);

    unsigned int VAO;
    uint32_t nTris;
public:
    ObjectRenderer(BaseObject* obj);

    void initGL();
    void draw();
};
