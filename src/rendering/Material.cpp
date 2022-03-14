#include "rendering/Material.h"

QString GXShaderLibrary::generateBindingsDefinition(Material& material)
{
    return QString(R"(
// Expected to be constant across the entire scene.
layout(std140) uniform ub_SceneParams {
    Mat4x4 u_Projection;
    vec4 u_Misc0;
};
#define u_SceneTextureLODBias u_Misc0[0]
struct Light {
    vec4 Color;
    vec4 Position;
    vec4 Direction;
    vec4 DistAtten;
    vec4 CosAtten;
};
struct FogBlock {
    // A, B, C, Center
    vec4 Param;
    // 10 items
    vec4 AdjTable[3];
    // Fog color is RGB
    vec4 Color;
};
// Expected to change with each material.
layout(std140) uniform ub_MaterialParams {
    vec4 u_ColorMatReg[2];
    vec4 u_ColorAmbReg[2];
    vec4 u_KonstColor[4];
    vec4 u_Color[4];
    Mat4x3 u_TexMtx[10];
    vec4 u_TextureSizes[4];
    vec4 u_TextureBiases[2];
    Mat4x2 u_IndTexMtx[3];
    // Optional parameters.)") +

    QString(material.hasPostTexMtxBlock ? "\
    Mat4x3 u_PostTexMtx[20];" : "") +

    QString(material.hasLightsBlock ? "\
    Light u_LightParams[8];" : "") +

    QString(material.hasFogBlock ? "\
    FogBlock u_FogBlock;" : "") +

    QString(material.hasDynamicAlphaTest ? "\
    vec4 u_DynamicAlphaParams;" : "") +

    QString(R"(
};
// Expected to change with each shape draw.
layout(std140) uniform ub_DrawParams {)") +

    QString(material.usePnMtxIdx ?
    "\nMat4x3 u_PosMtx[10];" : "\nMat4x3 u_PosMtx[1];") +

    QString(R"(
};
uniform sampler2D u_Texture0;
uniform sampler2D u_Texture1;
uniform sampler2D u_Texture2;
uniform sampler2D u_Texture3;
uniform sampler2D u_Texture4;
uniform sampler2D u_Texture5;
uniform sampler2D u_Texture6;
uniform sampler2D u_Texture7;)");
}

uint32_t GXShaderLibrary::getMaterialParamsBlockSize(Material& material) {
    uint32_t size = 4*2 + 4*2 + 4*4 + 4*4 + 4*3*10 + 4*4 + 4*2 + 4*2*3;
    if (material.hasPostTexMtxBlock)
        size += 4*3*20;
    if (material.hasLightsBlock)
        size += 4*5*8;
    if (material.hasFogBlock)
        size += 4*5;
    if (material.hasDynamicAlphaTest)
        size += 4*1;

    return size;
}

uint32_t GXShaderLibrary::getDrawParamsBlockSize(Material& material) {
    uint32_t size = 0;

    if (material.usePnMtxIdx)
        size += 4*3 * 10;
    else
        size += 4*3 * 1;

    return size;
}

GXShaderLibrary::GX_Program::GX_Program(GX::Material& material) : name(material.name)
{
    // TODO generateShaders();
}

QString GXShaderLibrary::GX_Program::generateFloat(float f)
{
    std::string s = std::to_string(f);
    if (s.find_first_of('.') == std::string::npos)
        s += ".0";
    return QString::fromStdString(s);
}

QString GXShaderLibrary::GX_Program::generateMaterialSource(ColorChannelControl chan, uint32_t i) {
    //if (this.hacks !== null && this.hacks.disableVertexColors && chan.matColorSource === GX.ColorSrc.VTX)
    //    return `vec4(1.0, 1.0, 1.0, 1.0)`;

    switch (chan.matColorSource) {
        case GX::ColorSrc::VTX: return QString("a_Color") + i;
        case GX::ColorSrc::REG: return QString("u_ColorMatReg[") + i + "]";
    }

    return "";
}

QString GXShaderLibrary::GX_Program::GX_ProgramgenerateAmbientSource(ColorChannelControl chan, uint32_t i) {
    //if (this.hacks !== null && this.hacks.disableVertexColors && chan.ambColorSource === GX.ColorSrc.VTX)
    //    return `vec4(1.0, 1.0, 1.0, 1.0)`;

    switch (chan.ambColorSource) {
        case GX::ColorSrc::VTX: return QString("a_Color") + i;
        case GX::ColorSrc::REG: return QString("u_ColorMatReg[") + i + "]";
    }

    return "";
}

QString GXShaderLibrary::GX_Program::generateLightDiffFn(ColorChannelControl chan, QString& lightName)
{
    const QString NdotL = "dot(t_Normal, t_LightDeltaDir)";

    switch (chan.diffuseFunction) {
        case GX::DiffuseFunction::NONE: return "1.0";
        case GX::DiffuseFunction::SIGN: return NdotL;
        case GX::DiffuseFunction::CLAMP: return "max(" + NdotL + ", 0.0)";
    }
}

QString GXShaderLibrary::GX_Program::generateLightAttnFn(ColorChannelControl chan, QString& lightName) {
        if (chan.attenuationFunction == GX::AttenuationFunction::NONE) {
            return R"(
    t_Attenuation = 1.0;)";
        } else if (chan.attenuationFunction == GX::AttenuationFunction::SPOT) {
            const QString attn = "max(0.0, dot(t_LightDeltaDir, " + lightName + ".Direction.xyz))";
            const QString cosAttn = "max(0.0, ApplyAttenuation(" + lightName + ".CosAtten.xyz, " + attn + "))";
            const QString distAttn = "dot(" + lightName + ".DistAtten.xyz, vec3(1.0, t_LightDeltaDist, t_LightDeltaDist2))";
            return R"(
    t_Attenuation = ${cosAttn} / ${distAttn};)";
        } else if (chan.attenuationFunction == GX::AttenuationFunction::SPEC) {
            const QString attn = "(dot(t_Normal, t_LightDeltaDir) >= 0.0) ? max(0.0, dot(t_Normal, " + lightName + ".Direction.xyz)) : 0.0";
            const QString cosAttn = "ApplyAttenuation(" + lightName + ".CosAtten.xyz, t_Attenuation)";
            const QString distAttn = "ApplyAttenuation(" + lightName + ".DistAtten.xyz, t_Attenuation)";
            return QString(R"(
    t_Attenuation = )") + attn + R"(;
    t_Attenuation = )" + cosAttn + " / " + distAttn + ";";
        } else {
            throw "whoops"; // lol nice
        }
    }

// TODO continue at https://github.com/magcius/noclip.website/blob/master/src/gx/gx_material.ts#L474
