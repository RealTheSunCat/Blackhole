#include "io/BmdFile.h"

#include "Util.h"

#include <stack>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

BmdFile::BmdFile(BaseFile* inRarcFile) : file(inRarcFile)
{
    file->setBigEndian(true);

    file->position(0xC);
    uint32_t numSections = file->readInt();

    file->skip(0x10);

    for(uint32_t i = 0; i < numSections; i++)
    {
        QString section= file->readString(4, "ASCII"); // TODO redo all these with noclip code
        if(section == "INF1")
            readINF1();
        if(section == "VTX1")
            readVTX1();
        if(section == "EVP1")
            readEVP1();
        if(section == "DRW1")
            readDRW1();
        if(section == "JNT1")
            readJNT1();
        if(section == "SHP1")
            readSHP1();
        if(section == "MAT3") // stop rampage here please
            readMAT3();
        if(section == "MDL3")
            readMDL3();
        if(section == "TEX1")
            readTEX1();
    }
}

void BmdFile::readINF1()
{
    uint32_t sectionStart = file->position() - 0x4;
    uint32_t sectionSize = file->readInt();

    J3DLoadFlags loadFlags = J3DLoadFlags(file->readShort());
    file->skip(2);
    uint32_t mtxGroupCount = file->readInt();
    uint32_t vertexCount = file->readInt();
    uint32_t hierarchyOffset = file->readInt();
    std::span<uint8_t> hierarchyData = file->slice(sectionStart + hierarchyOffset, sectionStart + sectionSize);

    inf1 = INF1{ hierarchyData, loadFlags };

    file->position(sectionStart + sectionSize);
}

void BmdFile::readVTX1()
{
    uint32_t sectionStart = file->position() - 0x4;
    uint32_t sectionSize = file->readInt();

    uint32_t formatOffset = file->readInt();
    uint32_t dataOffsetLookupTable = 0x0C;

    // Data tables are stored in this order. Assumed to be hardcoded in a
    // struct somewhere inside JSystem.
    const std::array<GX::Attr_t, 13> dataTables = {
        GX::Attr::POS,
        GX::Attr::NRM,
        GX::Attr::NRM, // NBT
        GX::Attr::CLR0,
        GX::Attr::CLR1,
        GX::Attr::TEX0,
        GX::Attr::TEX1,
        GX::Attr::TEX2,
        GX::Attr::TEX3,
        GX::Attr::TEX4,
        GX::Attr::TEX5,
        GX::Attr::TEX6,
        GX::Attr::TEX7,
    };

    uint32_t offset = formatOffset;
    std::unordered_map<GX::Attr_t, GX::VertexArray> vertexArrays;
    while (true) {
        file->position(offset);
        GX::Attr_t vtxAttrib = GX::Attr_t(file->readInt());
        if (vtxAttrib == GX::Attr::NUL)
            break;

        GX::CompCnt_t compCnt = GX::CompCnt_t(file->readInt());
        GX::CompType_t compType = GX::CompType_t(file->readInt());
        uint8_t compShift = file->readByte();
        offset += 0x10;

        uint32_t formatIdx = indexOf(dataTables, vtxAttrib);
        if (formatIdx < 0)
            continue;

        // Each attrib in the VTX1 chunk also has a corresponding data chunk containing
        // the data for that attribute, in the format stored above.

        // BMD doesn't tell us how big each data chunk is, but we need to know to figure
        // out how much data to upload. We assume the data offset lookup table is sorted
        // in order, and can figure it out by finding the next offset above us.
        uint32_t dataOffsetLookupTableEntry = dataOffsetLookupTable + formatIdx*0x04;
        uint32_t dataOffsetLookupTableEnd = dataOffsetLookupTable + dataTables.size()*0x04;

        file->position(dataOffsetLookupTableEntry);
        uint32_t dataStart = file->readInt();

        // find dataEnd
        uint32_t dataEnd = sectionSize; // TODO is sectionSize the right default value?
        file->position(dataOffsetLookupTableEntry + 0x04);
        while (file->position() < dataOffsetLookupTableEnd) {
            uint32_t dataOffset = file->readInt();
            if (dataOffset != 0)
                dataEnd =  dataOffset;
        }

        uint32_t dataOffset = dataStart;
        uint32_t dataSize = dataEnd - dataStart;
        std::span<uint8_t> vtxDataBuffer = file->slice(dataOffset, dataOffset + dataSize);
        GX::VertexArray vertexArray = { vtxAttrib, compType, compCnt, compShift, vtxDataBuffer, dataOffset, dataSize };

        vertexArrays.insert(std::make_pair(vtxAttrib, vertexArray));
    }

    vtx1 = { vertexArrays };

    file->position(sectionStart + sectionSize);
}

void BmdFile::readEVP1()
{
    uint32_t sectionStart = file->position() - 4;
    uint32_t sectionSize = file->readInt();

    uint16_t envelopeTableCount = file->readShort();
    file->skip(0x2);

    uint32_t weightedBoneCountTableOffset = file->readInt();
    uint32_t weightedBoneIndexTableOffset = file->readInt();
    uint32_t weightedBoneWeightTableOffset = file->readInt();
    uint32_t inverseBindPoseTableOffset = file->readInt();

    uint16_t weightedBoneId = 0;
    uint16_t maxBoneIndex = 0; // TODO this was -1 but uint?
    std::vector<Envelope> envelopes;
    for(int i = 0; i < envelopeTableCount; i++)
    {
        file->position(sectionStart + weightedBoneCountTableOffset + i);
        uint8_t numWeightedBones = file->readByte();

        std::vector<WeightedBone> weightedBones;

        for(int j = 0; j < numWeightedBones; j++)
        {
            file->position(sectionStart + weightedBoneCountTableOffset + weightedBoneId * 0x02);
            uint16_t index = file->readShort();

            file->position(sectionStart + weightedBoneWeightTableOffset + weightedBoneId * 0x04);
            float weight = file->readFloat();

            weightedBones.push_back({weight, index});
            maxBoneIndex = std::max(maxBoneIndex, index);
            weightedBoneId++;
        }

        envelopes.push_back({weightedBones});
    }

    std::vector<glm::mat4> inverseBinds;
    for(int i = 0; i < maxBoneIndex + 1; i++)
    {
        file->position(sectionStart + inverseBindPoseTableOffset + (i * 0x30));

        float m00 = file->readFloat();
        float m10 = file->readFloat();
        float m20 = file->readFloat();
        float m30 = file->readFloat();
        float m01 = file->readFloat();
        float m11 = file->readFloat();
        float m21 = file->readFloat();
        float m31 = file->readFloat();
        float m02 = file->readFloat();
        float m12 = file->readFloat();
        float m22 = file->readFloat();
        float m32 = file->readFloat();

        inverseBinds.push_back(glm::mat4( // TODO is this ctor accurate?
            m00, m01, m02, 0,
            m10, m11, m12, 0,
            m20, m21, m22, 0,
            m30, m31, m32, 1
        ));
    }

    evp1 = { envelopes, inverseBinds };

    file->position(sectionStart + sectionSize);
}

void BmdFile::readDRW1()
{
    uint32_t sectionStart = file->position() - 4;
    uint32_t sectionSize = file->readInt();

    uint16_t drawMatrixCount = file->readShort();
    file->skip(0x2);

    uint32_t drawMatrixTypeTableOffset = file->readInt();
    uint32_t dataArrayOffset = file->readInt();

    std::vector<DRW1Matrix> matrixDefinitions;
    for(int i = 0; i < drawMatrixCount; i++)
    {
        file->position(sectionStart + drawMatrixTypeTableOffset + i);
        DRW1MatrixKind kind = DRW1MatrixKind(file->readByte());

        file->position(sectionStart + dataArrayOffset + i * 0x02);
        uint16_t param = file->readShort();

        matrixDefinitions.push_back({kind, param});
    }

    drw1 = { matrixDefinitions };

    file->position(sectionStart + sectionSize);
}

void BmdFile::readJNT1()
{
    uint32_t sectionStart = file->position() - 4;
    uint32_t sectionSize = file->readInt();

    uint16_t jointDataCount = file->readShort();
    assert(file->readShort() == 0xFFFF);

    uint32_t jointDataTableOffset = file->readInt();
    uint32_t remapTableOffset = file->readInt();
    uint32_t nameTableOffset = file->readInt();

    std::vector<uint16_t> remapTable;
    for(int i = 0; i < jointDataCount; i++)
    {
        file->position(sectionStart + remapTableOffset + (i * 0x02));
        remapTable.push_back(file->readShort());
    }

    std::vector<QString> nameTable = readStringTable(sectionStart + nameTableOffset);

    std::vector<Joint> joints;
    for(int i = 0; i < jointDataCount; i++)
    {
        QString& name = nameTable[i];
        uint32_t jointDataTableIndex = jointDataTableOffset + (remapTable[i] * 0x40);

        file->position(sectionStart + jointDataTableIndex);

        uint16_t flags = file->readShort() & 0x00FF;
        // Maya / SoftImage special flags
        uint8_t calcFlags = file->readByte();

        file->skip(0x01);

        float scaleX = file->readFloat();
        float scaleY = file->readFloat();
        float scaleZ = file->readFloat();

        int16_t rotationX = file->readShortS() / 0x7FFF * M_PI;
        int16_t rotationY = file->readShortS() / 0x7FFF * M_PI;
        int16_t rotationZ = file->readShortS() / 0x7FFF * M_PI;

        file->skip(0x02);

        float translationX = file->readFloat();
        float translationY = file->readFloat();
        float translationZ = file->readFloat();

        float boundingSphereRadius = file->readFloat();

        float bboxMinX = file->readFloat();
        float bboxMinY = file->readFloat();
        float bboxMinZ = file->readFloat();
        float bboxMaxX = file->readFloat();
        float bboxMaxY = file->readFloat();
        float bboxMaxZ = file->readFloat();

        AABB bbox{{bboxMinX, bboxMinY, bboxMinZ}, {bboxMaxX, bboxMaxY, bboxMaxZ}};

        JointTransformInfo transform{{scaleX, scaleY, scaleZ}, {translationX, translationY, translationZ}};
        transform.rotation = glm::quat(glm::orientate3(glm::vec3(rotationX, rotationY, rotationZ))); // TODO is this accurate?

        joints.push_back({ name, transform, boundingSphereRadius, bbox, calcFlags });
    }

    jnt1 = JNT1{ joints };

    file->position(sectionStart + sectionSize);
}

void BmdFile::readSHP1()
{
    uint32_t sectionStart = file->position() - 4;
    uint32_t sectionSize = file->readInt();



    file->position(sectionStart + sectionSize);
}

// huge thanks to noclip.website for this parser!
void BmdFile::readMAT3()
{
    uint32_t sectionStart = file->position() - 4;
    uint32_t sectionSize = file->readInt();

    uint16_t materialCount = file->readShort();

    file->skip(0x2);
    uint32_t materialEntryTableOffsets = file->readInt();
    uint32_t remapTableOffset = file->readInt();

    std::vector<uint16_t> remapTable;
    for(uint32_t i = 0; i < materialCount; i++)
    {
        file->position(sectionStart + remapTableOffset + i * 0x02);
        remapTable.push_back(file->readShort());
    }

    file->position(sectionStart + 0x14);

    uint32_t nameTableOffset = file->readInt();
    std::vector<QString> nameTable = readStringTable(sectionStart + nameTableOffset);


    file->position(sectionStart + 0x18);
    uint32_t indirectTableOffset = file->readInt();         // 0x18
    uint32_t cullModeTableOffset = file->readInt();         // 0x1C
    uint32_t materialColorTableOffset = file->readInt();    // 0x20
    uint32_t colorChanCountTableOffset = file->readInt();   // 0x24
    uint32_t colorChanTableOffset = file->readInt();        // 0x28
    uint32_t ambientColorTableOffset = file->readInt();     // 0x2C

    file->skip(0x08);
    uint32_t texGenTableOffset = file->readInt();           // 0x38
    uint32_t postTexGenTableOffset = file->readInt();       // 0x3C
    uint32_t texMtxTableOffset = file->readInt();           // 0x40
    uint32_t postTexMtxTableOffset = file->readInt();       // 0x44
    uint32_t textureTableOffset = file->readInt();          // 0x48
    uint32_t tevOrderTableOffset = file->readInt();         // 0x4C
    uint32_t colorRegisterTableOffset = file->readInt();    // 0x50
    uint32_t colorConstantTableOffset = file->readInt();    // 0x54

    file->skip(0x04);
    uint32_t tevStageTableOffset = file->readInt();         // 0x58
    uint32_t tevSwapModeInfoOffset = file->readInt();
    uint32_t tevSwapModeTableInfoOffset = file->readInt();
    uint32_t fogInfoTableOffset = file->readInt();
    uint32_t alphaTestTableOffset = file->readInt();
    uint32_t blendModeTableOffset = file->readInt();
    uint32_t zModeTableOffset = file->readInt();

    m_materials.clear();

    for(uint32_t i = 0; i < materialCount; i++)
    {
        uint32_t index = i;
        QString& name = nameTable[i];
        uint32_t materialEntryIndex = materialEntryTableOffsets + (0x014C * remapTable[i]);

        file->position(sectionStart + materialEntryIndex);
        uint8_t materialMode = file->readByte();

        // Bitfield:
        //   0n001: OPA (Opaque)
        //   0b010: EDG (TexEdge / Masked)
        //   0b100: XLU (Translucent)
        // EDG has never been seen, so ignore it
        assert(materialMode == 0b001 || materialMode == 0b100);

        uint8_t cullModeIndex = file->readByte();
        uint8_t colorChanNumIndex = file->readByte();
        file->skip(0x3);
        uint8_t zModeIndex = file->readByte();

        std::vector<QColor> colorMatRegs;
        for(uint32_t j = 0; j < 2; j++)
        {

            file->position(sectionStart + materialEntryIndex + 0x08 + j * 0x02);
            uint16_t matColorIndex = file->readShort();

            if (matColorIndex != 0xFFFF)
            {
                file->position(sectionStart + materialColorTableOffset + matColorIndex * 0x04);
                colorMatRegs.push_back(readColor_RGBA8());
            }
            else
            {
                colorMatRegs.push_back(QColorConstants::White);
            }
        }

        std::vector<QColor> colorAmbRegs;
        for(uint32_t j = 0; j < 2; j++)
        {

            file->position(sectionStart + materialEntryIndex + 0x14 + j * 0x02);
            uint16_t ambColorIndex = file->readShort();

            if (ambColorIndex != 0xFFFF)
            {
                file->position(sectionStart + ambientColorTableOffset + ambColorIndex * 0x04);
                colorAmbRegs.push_back(readColor_RGBA8());
            }
            else
            {
                colorAmbRegs.push_back(QColorConstants::White);
            }
        }

        file->position(sectionStart + colorChanCountTableOffset + colorChanNumIndex);
        uint8_t lightChannelCount = file->readByte();
        std::vector<GX::LightChannelControl> lightChannels;
        for(uint32_t j = 0; j < lightChannelCount; j++)
        {
            file->position(sectionStart + materialEntryIndex + 0x0C + (j * 2) * 0x02);

            GX::ColorChannelControl colorChannel = readColorChannel(sectionStart + colorChanTableOffset, file->readShort());

            file->position(sectionStart + materialEntryIndex + 0x0C + (j * 2 + 1) * 0x02);
            GX::ColorChannelControl alphaChannel = readColorChannel(sectionStart + colorChanTableOffset, file->readShort());

            lightChannels.push_back({ colorChannel, alphaChannel });
        }

        std::vector<GX::TexGen> texGens;
        for(uint32_t j = 0; j < 8; j++)
        {
            file->position(sectionStart + materialEntryIndex + 0x28 + j * 0x02);
            int16_t texGenIndex = file->readShortS();

            if(texGenIndex < 0)
                continue; // negative index means skip

            file->position(sectionStart + texGenTableOffset + texGenIndex * 0x04);
            GX::TexGenType_t type = GX::TexGenType_t(file->readByte());
            GX::TexGenSrc_t source = GX::TexGenSrc_t(file->readByte());
            GX::TexGenMatrix_t matrixCheck = GX::TexGenMatrix_t(file->readByte());
            assert(file->readByte() == 0xFF);

            GX::PostTexGenMatrix_t postMatrix = GX::PostTexGenMatrix::PTIDENTITY;

            file->position(sectionStart + materialEntryIndex + 0x38 + j * 0x02);
            int16_t postTexGenIndex = file->readShortS(); // type punning. bad?
            if(postTexGenTableOffset > 0 && postTexGenIndex >= 0)
            {
                file->position(sectionStart + postTexGenTableOffset + texGenIndex * 0x04 + 0x02);
                postMatrix = GX::PostTexGenMatrix_t(file->readByte());
                assert(file->readByte() == 0xFF);
            }

            // BTK can apply texture animations to materials that have the matrix set to IDENTITY.
            // For this reason, we always assign a texture matrix. In theory, the file should
            // have an identity texture matrix in the texMatrices section, so it should render correctly
            GX::TexGenMatrix_t matrix = GX::TexGenMatrix_t(GX::TexGenMatrix::TEXMTX0 + (j * 3));

            // If we ever find a counter-example for this, I'll have to rethink the scheme, but I
            // *believe* that texture matrices should always be paired with TexGens in order.
            assert(matrixCheck == GX::TexGenMatrix::IDENTITY || matrixCheck == matrix);

            bool normalize = false;
            texGens.push_back({ type, source, matrix, normalize, postMatrix });
        }

        std::vector<TexMatrix> texMatrices;
        for(uint32_t j = 0; j < 10; j++)
        {
            file->position(sectionStart + materialEntryIndex + 0x48 + j * 0x02);
            int16_t texMatrixIndex = file->readShortS();

            if(texMtxTableOffset > 0 && texMatrixIndex >= 0)
            {
                uint32_t texMtxOffset = texMtxTableOffset + texMatrixIndex * 0x64;
                file->position(sectionStart + texMtxOffset);

                TexMatrixProjection projection = TexMatrixProjection(file->readByte());
                uint8_t info = file->readByte();

                GX::TexMtxMapMode_t matrixMode = GX::TexMtxMapMode_t(info & 0x3F);

                // Detect uses of unlikely map modes.
                assert(matrixMode != GX::TexMtxMapMode::ProjmapBasic && matrixMode != GX::TexMtxMapMode::ViewProjmapBasic &&
                        int(matrixMode) != 0x04 && int(matrixMode) != 0x05);

                assert(file->readShort() == 0xFFFF);

                float centerS = file->readFloat();
                float centerT = file->readFloat();
                float centerQ = file->readFloat();

                float scaleS = file->readFloat();
                float scaleT = file->readFloat();

                float rotation = file->readShort() / 0x7FFF;
                assert(file->readShort() == 0xFFFF);

                float translationS = file->readFloat();
                float translationT = file->readFloat();

                // TODO is this the right order?
                glm::mat4 effectMatrix(
                    file->readFloat(), file->readFloat(), file->readFloat(), file->readFloat(),
                    file->readFloat(), file->readFloat(), file->readFloat(), file->readFloat(),
                    file->readFloat(), file->readFloat(), file->readFloat(), file->readFloat(),
                    file->readFloat(), file->readFloat(), file->readFloat(), file->readFloat()

                );

                glm::mat4 matrix(1.0f);
                bool isMaya = info >> 7;
                if(isMaya)
                {
                    float theta = rotation * M_PI;
                    float sinR = sin(theta);
                    float cosR = cos(theta);

                    matrix[0][0]  = scaleS *  cosR;
                    matrix[1][0]  = scaleS *  sinR;
                    matrix[3][0]  = scaleS * ((-0.5 * cosR) - (0.5 * sinR - 0.5) - translationS);

                    matrix[0][1]  = scaleT * -sinR;
                    matrix[1][1]  = scaleT *  cosR;
                    matrix[3][1]  = scaleT * ((-0.5 * cosR) + (0.5 * sinR - 0.5) + translationT) + 1.0;
                }
                else
                {
                    float theta = rotation * M_PI;
                    float sinR = sin(theta);
                    float cosR = cos(theta);

                    matrix[0][0]  = scaleS *  cosR;
                    matrix[1][0]  = scaleS * -sinR;
                    matrix[3][0] = translationS + centerS - (matrix[0][0] * centerS + matrix[1][0] * centerT);

                    matrix[0][1]  = scaleT *  sinR;
                    matrix[1][1]  = scaleT *  cosR;
                    matrix[3][1]  = translationT + centerT - (matrix[1][1] * centerS + matrix[1][1] * centerT);
                }

                texMatrices.push_back(TexMatrix{ false, info, projection, effectMatrix, matrix });
            }
            else
            {
                texMatrices.push_back(TexMatrix{ true });
            }
        }

        // Since texture matrices are assigned to TEV stages in order, we
        // should never actually have more than 8 of these.
        assert(texMatrices[8].isNull && texMatrices[9].isNull);

        // These are never read in actual J3D.
        /*
        const postTexMatrices: (TexMtx | null)[] = [];
        for (let j = 0; j < 20; j++) {
            const postTexMtxIndex = view.getInt16(materialEntryIdx + 0x5C + j * 0x02);
            if (postTexMtxTableOffs > 0 && postTexMtxIndex >= 0)
                postTexMatrices[j] = readTexMatrix(postTexMtxTableOffs, postTexMtxIndex);
            else
                postTexMatrices[j] = null;
        }
        */

        std::vector<int16_t> textureIndexes; // shouldn't this be indices?
        for(uint32_t j = 0; j < 8; j++)
        {
            file->position(sectionStart + materialEntryIndex + 0x84 + j * 0x02);
            uint16_t textureTableIndex = file->readShort();
            if(textureTableIndex != 0xFFFF)
            {
                file->position(sectionStart + textureTableOffset + textureTableIndex * 0x02);
                textureIndexes.push_back(file->readShort());
            }
            else
            {
                textureIndexes.push_back(-1);
            }
        }

        std::vector<QColor> colorConstants; // shouldn't this be indices?
        for(uint32_t j = 0; j < 4; j++)
        {
            file->position(sectionStart + materialEntryIndex + 0x94 + j * 0x02);
            uint16_t colorIndex = file->readShort();
            if(colorIndex != 0xFFFF)
            {
                file->position(sectionStart + colorConstantTableOffset + colorIndex * 0x04);
                colorConstants.push_back(readColor_RGBA8());
            }
            else
            {
                colorConstants.push_back(QColorConstants::White);
            }
        }

        std::vector<QColor> colorRegisters; // shouldn't this be indices?
        for(uint32_t j = 0; j < 4; j++)
        {
            file->position(sectionStart + materialEntryIndex + 0xDC + j * 0x02);
            uint16_t colorIndex = file->readShort();
            if(colorIndex != 0xFFFF)
            {
                file->position(sectionStart + colorRegisterTableOffset + colorIndex * 0x08);
                colorConstants.push_back(readColor_RGBA16());
            }
            else
            {
                colorConstants.push_back(QColorConstants::Transparent);
            }
        }

        std::vector<GX::IndTexStage> indTexStages;
        std::vector<float> indTexMatrices;
        uint32_t indirectEntryOffset = indirectTableOffset + i * 0x138;

        bool hasIndirectTable = indirectTableOffset != nameTableOffset;

        if(hasIndirectTable)
        {
            file->position(sectionStart + indirectEntryOffset);

            uint8_t hasIndirect = file->readByte();
            assert((hasIndirect & 0b11111110) == 0); // make sure it's a bool

            uint8_t indTexStageNum = file->readByte();
            assert(indTexStageNum <= 4);

            for(uint32_t j = 0; j < indTexStageNum; j++)
            {
                // SetIndTexOrder
                uint32_t indTexOrderOffset = indirectEntryOffset + 0x04 + j * 0x04;
                file->position(sectionStart + indTexOrderOffset);

                GX::TexCoordID_t texCoordId = GX::TexCoordID_t(file->readByte());
                GX::TexMapID_t texture = GX::TexMapID_t(file->readByte());

                // SetIndTexCoordScale
                uint32_t indTexScaleOffset = indirectEntryOffset + 0x04 + (0x04 * 4) + (0x1C * 3) + j * 0x04;
                file->position(sectionStart + indTexScaleOffset);

                GX::IndTexScale_t scaleS = GX::IndTexScale_t(file->readByte());
                GX::IndTexScale_t scaleT = GX::IndTexScale_t(file->readByte());
                indTexStages.push_back({ texCoordId, texture, scaleS, scaleT });

                // SetIndTexMatrix
                uint32_t indTexMatrixOffset = indirectEntryOffset + 0x04 + (0x04 * 4) + j * 0x1C;
                file->position(sectionStart + indTexMatrixOffset);

                float p00 = file->readFloat();
                float p01 = file->readFloat();
                float p02 = file->readFloat();
                float p10 = file->readFloat();
                float p11 = file->readFloat();
                float p12 = file->readFloat();
                float scale = pow(2, file->readInt());

                // TODO should this be a mat2x4?
                indTexMatrices.push_back(p00*scale);
                indTexMatrices.push_back(p01*scale);
                indTexMatrices.push_back(p02*scale);
                indTexMatrices.push_back(scale);
                indTexMatrices.push_back(p10*scale);
                indTexMatrices.push_back(p11*scale);
                indTexMatrices.push_back(p12*scale);
                indTexMatrices.push_back(0.0f);

            }
        }

        std::vector<GX::TevStage> tevStages;
        for(uint32_t j = 0; j < 16; j++)
        {
            // TevStage
            file->position(sectionStart + materialEntryIndex + 0xE4 + j * 0x02);

            int16_t tevStageIndex = file->readShortS();
            if(tevStageIndex < 0)
                continue;

            uint32_t tevStageOffset = tevStageTableOffset + tevStageIndex * 0x14;
            file->position(sectionStart + tevStageOffset + 1); // skip unk byte

            GX::CC_t colorInA = GX::CC_t(file->readByte());
            GX::CC_t colorInB = GX::CC_t(file->readByte());
            GX::CC_t colorInC = GX::CC_t(file->readByte());
            GX::CC_t colorInD = GX::CC_t(file->readByte());
            GX::TevOp_t colorOp = GX::TevOp_t(file->readByte());
            GX::TevBias_t colorBias = GX::TevBias_t(file->readByte());
            GX::TevScale_t colorScale = GX::TevScale_t(file->readByte());
            bool colorClamp = file->readByte();
            GX::Register_t colorRegID = GX::Register_t(file->readByte());

            GX::CA_t alphaInA = GX::CA_t(file->readByte());
            GX::CA_t alphaInB = GX::CA_t(file->readByte());
            GX::CA_t alphaInC = GX::CA_t(file->readByte());
            GX::CA_t alphaInD = GX::CA_t(file->readByte());
            GX::TevOp_t alphaOp = GX::TevOp_t(file->readByte());
            GX::TevBias_t alphaBias = GX::TevBias_t(file->readByte());
            GX::TevScale_t alphaScale = GX::TevScale_t(file->readByte());
            bool alphaClamp = file->readByte();
            GX::Register_t alphaRegID = GX::Register_t(file->readByte());

            // TevOrder
            file->position(sectionStart + materialEntryIndex + 0xBC + j * 0x02);

            uint16_t tevOrderIndex = file->readShort();

            uint32_t tevOrderOffset = tevOrderTableOffset + tevOrderIndex * 0x04;
            file->position(sectionStart + tevOrderOffset);

            GX::TexCoordID_t texCoordID = GX::TexCoordID_t(file->readByte());
            GX::TexMapID_t texMap = GX::TexMapID_t(file->readByte());

            GX::RasColorChannelID_t channelID;
            switch (GX::ColorChannelID_t(file->readByte())) {
                case GX::ColorChannelID::COLOR0:
                case GX::ColorChannelID::ALPHA0:
                case GX::ColorChannelID::COLOR0A0:
                    channelID = GX::RasColorChannelID::COLOR0A0;
                    break;
                case GX::ColorChannelID::COLOR1:
                case GX::ColorChannelID::ALPHA1:
                case GX::ColorChannelID::COLOR1A1:
                    channelID = GX::RasColorChannelID::COLOR1A1;
                    break;
                case GX::ColorChannelID::ALPHA_BUMP:
                    channelID = GX::RasColorChannelID::ALPHA_BUMP;
                    break;
                case GX::ColorChannelID::ALPHA_BUMP_N:
                    channelID = GX::RasColorChannelID::ALPHA_BUMP_N;
                    break;
                case GX::ColorChannelID::COLOR_ZERO:
                case GX::ColorChannelID::COLOR_NULL:
                    channelID = GX::RasColorChannelID::COLOR_ZERO;
                    break;
                default:
                    assert(false);
            }
            assert(file->readByte() == 0xFF);

            // KonstSel
            file->position(sectionStart + materialEntryIndex + 0x9C + j);
            GX::KonstColorSel_t konstColorSel = GX::KonstColorSel_t(file->readByte());
            file->skip(0x10);
            GX::KonstAlphaSel_t konstAlphaSel = GX::KonstAlphaSel_t(file->readByte());

            // SetTevSwapMode
            file->position(sectionStart + materialEntryIndex + 0x104 + j * 0x02);
            uint16_t tevSwapModeIndex = file->readShort();

            file->position(sectionStart + tevSwapModeInfoOffset + tevSwapModeIndex * 0x04);
            uint8_t tevSwapModeRasSel = file->readByte();
            uint8_t tevSwapModeTexSel = file->readByte();

            file->position(sectionStart + materialEntryIndex + 0x124 + tevSwapModeRasSel * 0x02);
            uint16_t tevSwapModeTableRasIndex = file->readShort();

            file->position(sectionStart + materialEntryIndex + 0x124 + tevSwapModeTexSel * 0x02);
            uint16_t tevSwapModeTableTexIndex = file->readShort();

            file->position(sectionStart + tevSwapModeTableInfoOffset + tevSwapModeTableRasIndex * 0x04);
            uint8_t rasSwapA = file->readByte();
            uint8_t rasSwapB = file->readByte();
            uint8_t rasSwapC = file->readByte();
            uint8_t rasSwapD = file->readByte();

            file->position(sectionStart + tevSwapModeTableInfoOffset + tevSwapModeTableTexIndex * 0x04);
            uint8_t texSwapA = file->readByte();
            uint8_t texSwapB = file->readByte();
            uint8_t texSwapC = file->readByte();
            uint8_t texSwapD = file->readByte();

            GX::SwapTable rasSwapTable = {
                GX::TevColorChan_t(rasSwapA),
                GX::TevColorChan_t(rasSwapB),
                GX::TevColorChan_t(rasSwapC),
                GX::TevColorChan_t(rasSwapD)
            };

            GX::SwapTable texSwapTable = {
                GX::TevColorChan_t(texSwapA),
                GX::TevColorChan_t(texSwapB),
                GX::TevColorChan_t(texSwapC),
                GX::TevColorChan_t(texSwapD)
            };

            // SetTevIndirect
            uint32_t indTexStageOffset = indirectEntryOffset + 0x04 + (0x04 * 4) + (0x1C * 3) + (0x04 * 4) + j * 0x0C;
            GX::IndTexStageID_t indTexStage = GX::IndTexStageID::STAGE0;
            GX::IndTexFormat_t indTexFormat = GX::IndTexFormat::_8;
            GX::IndTexBiasSel_t indTexBiasSel = GX::IndTexBiasSel::NONE;
            GX::IndTexAlphaSel_t indTexAlphaSel = GX::IndTexAlphaSel::OFF;
            GX::IndTexMtxID_t indTexMatrix = GX::IndTexMtxID::OFF;
            GX::IndTexWrap_t indTexWrapS = GX::IndTexWrap::OFF;
            GX::IndTexWrap_t indTexWrapT = GX::IndTexWrap::OFF;
            bool indTexAddPrev = false;
            bool indTexUseOrigLOD = false;

            if(hasIndirectTable)
            {
                file->position(sectionStart + indTexStageOffset);

                indTexStage = GX::IndTexStageID_t(file->readByte());
                indTexFormat = GX::IndTexFormat_t(file->readByte());
                indTexBiasSel = GX::IndTexBiasSel_t(file->readByte());
                indTexMatrix = GX::IndTexMtxID_t(file->readByte());
                assert(indTexMatrix <= GX::IndTexMtxID::T2);

                indTexWrapS = GX::IndTexWrap_t(file->readByte());
                indTexWrapT = GX::IndTexWrap_t(file->readByte());
                indTexAddPrev = file->readByte();
                indTexUseOrigLOD = file->readByte();
                indTexAlphaSel = GX::IndTexAlphaSel_t(file->readByte());
            }

            tevStages.push_back(GX::TevStage{
                colorInA, colorInB, colorInC, colorInC, colorOp,
                colorBias, colorScale, colorClamp, colorRegID,

                alphaInA, alphaInB, alphaInC, alphaInD, alphaOp,
                alphaBias, alphaScale, alphaClamp, alphaRegID,

                // SetTevOrder
                texCoordID, texMap, channelID,
                konstColorSel, konstAlphaSel,

                // SetTevSwapMode / SetTevSwapModeTable
                rasSwapTable, texSwapTable,

                // SetTevIndirect
                indTexStage, indTexFormat, indTexBiasSel, indTexAlphaSel,
                indTexMatrix, indTexWrapS, indTexWrapT, indTexAddPrev, indTexUseOrigLOD

            });
        }

        // SetAlphaCompare
        file->position(sectionStart + materialEntryIndex + 0x146);
        uint16_t alphaTestIndex = file->readShort();
        uint16_t blendModeIndex = file->readShort();

        uint32_t alphaTestOffset = alphaTestTableOffset + alphaTestIndex * 0x08;
        file->position(sectionStart + alphaTestOffset);

        GX::CompareType_t compareA = GX::CompareType_t(file->readByte());
        uint8_t referenceA = file->readByte() / 0xFF; // TODO should this be a float?
        GX::AlphaOp_t op = GX::AlphaOp_t(file->readByte());
        GX::CompareType_t compareB = GX::CompareType_t(file->readByte());
        uint8_t referenceB = file->readByte() / 0xFF;
        GX::AlphaTest alphaTest = { op, compareA, referenceA, compareB, referenceB };

        // SetBlendMode
        uint32_t blendModeOffset = blendModeTableOffset + blendModeIndex * 0x04;
        file->position(sectionStart + blendModeOffset);

        GX::BlendMode_t blendMode = GX::BlendMode_t(file->readByte());
        GX::BlendFactor_t blendSrcFactor = GX::BlendFactor_t(file->readByte());
        GX::BlendFactor_t blendDstFactor = GX::BlendFactor_t(file->readByte());
        GX::LogicOp_t blendLogicOp = GX::LogicOp_t(file->readByte());

        file->position(sectionStart + cullModeTableOffset + cullModeIndex * 0x04);
        GX::CullMode_t cullMode = GX::CullMode_t(file->readInt());

        uint32_t zModeOffset = zModeTableOffset + zModeIndex * 4;
        file->position(zModeOffset);

        bool depthTest = file->readByte();
        GX::CompareType_t depthFunc = GX::CompareType_t(file->readByte());
        bool depthWrite = file->readByte();

        file->position(sectionStart + materialEntryIndex + 0x144);
        uint16_t fogInfoIndex = file->readShort();

        uint32_t fogInfoOffset = fogInfoTableOffset + fogInfoIndex * 0x2C;
        file->position(sectionStart + fogInfoOffset);

        GX::FogType_t fogType = GX::FogType_t(file->readByte());
        bool fogAdjEnabled = file->readByte();
        uint16_t fogAdjCenter = file->readShort();
        float fogStartZ = file->readFloat();
        float fogEndZ = file->readFloat();
        float fogNearZ = file->readFloat();
        float fogFarZ = file->readFloat();
        QColor fogColor = readColor_RGBA8();

        std::array<uint16_t, 10> fogAdjTable;
        for(uint32_t j = 0; j < 10; j++)
            fogAdjTable[j] = file->readShort();

        GX::FogBlock fogBlock;
        bool fogProj = uint8_t(fogType) >> 3;
        if(fogProj)
        {
            // orthographic
            fogBlock.A = (fogFarZ - fogNearZ) / (fogEndZ - fogStartZ);
            fogBlock.B = 0.0f;
            fogBlock.C = (fogStartZ - fogNearZ) / (fogEndZ - fogStartZ);
        }
        else
        {
            fogBlock.A = (fogFarZ * fogNearZ) / ((fogFarZ - fogNearZ) * (fogEndZ - fogStartZ));
            fogBlock.B = (fogFarZ) / (fogFarZ - fogNearZ);
            fogBlock.C = (fogStartZ) / (fogEndZ - fogStartZ);
        }
        fogBlock.color = fogColor;
        fogBlock.adjTable = fogAdjTable;
        fogBlock.adjCenter = fogAdjCenter;

        bool translucent = materialMode == 0x04;
        bool colorUpdate = true, alphaUpdate = false;

        GX::RopInfo ropInfo{
            fogType, fogAdjEnabled,
            depthTest, depthFunc, depthWrite,
            blendMode, blendSrcFactor, blendDstFactor, blendLogicOp,

            colorUpdate, alphaUpdate
        };

        GX::Material gxMaterial{
            name, cullMode, lightChannels, texGens, tevStages, indTexStages, alphaTest
        };

        GX::autoOptimizeMaterial(gxMaterial);

        m_materials.push_back(Material{
            index, name,
            materialMode, translucent,
            textureIndexes, texMatrices,
            indTexMatrices,
            gxMaterial,
            colorMatRegs, colorAmbRegs,
            colorConstants, colorRegisters,
            fogBlock
        });
    }

    file->position(sectionStart + sectionSize);
}

void BmdFile::readMDL3()
{
    uint32_t sectionStart = file->position() - 4;
    uint32_t sectionSize = file->readInt();

    // this is the most important section
    // here we're going to parse it with lots of code
    // it's really good
    // and uh

    // skip section
    file->position(sectionStart + sectionSize);
}

void BmdFile::readTEX1()
{
    uint32_t sectionStart = file->position() - 4;
    uint32_t sectionSize = file->readInt();

    uint16_t textureCount = file->readShort();
    file->skip(0x02);

    uint32_t textureHeaderOffset = file->readInt();

    uint32_t nameTableOffset = file->readInt();
    std::vector<QString> nameTable = readStringTable(sectionStart + nameTableOffset);

    std::vector<Sampler> samplers;
    std::vector<GX::BTI_Texture> textureDatas;
    for(uint32_t i = 0; i < textureCount; i++)
    {
        uint32_t textureIndex = textureHeaderOffset + i * 0x20;
        QString& name = nameTable[i];

        GX::BTI_Texture btiTexture = readBTI(sectionStart + textureIndex, name);

        int32_t textureDataIndex = -1;

        // Try to find existing texture data
        const auto& textureData = btiTexture.data;
        if(!textureData.empty())
        {
            for(uint32_t j = 0; j < m_textures.size(); j++)
            {
                const GX::BTI_Texture& curTex = m_textures[j];

                if(curTex.data.empty())
                    continue;

                if(curTex.data == textureData)
                    textureDataIndex = j;
            }
        }

        if(textureDataIndex < 0)
        {
            m_textures.push_back(btiTexture);

            textureDataIndex = m_textures.size() - 1;
        }

        m_samplers.push_back({
            i,
            btiTexture.name,
            btiTexture.wrapS,
            btiTexture.wrapT,
            btiTexture.minFilter,
            btiTexture.magFilter,
            btiTexture.minLOD,
            btiTexture.maxLOD,
            btiTexture.lodBias,
            textureDataIndex
        });
    }

    file->position(sectionStart + sectionSize);
}

GX::BTI_Texture BmdFile::readBTI(uint32_t absoluteStartIndex, const QString& name)
{
    file->position(absoluteStartIndex);

    GX::TexFormat_t format = GX::TexFormat_t(file->readByte());
    file->skip(0x01);

    uint16_t width = file->readShort();
    uint16_t height = file->readShort();

    GX::WrapMode_t wrapS = GX::WrapMode_t(file->readByte());
    GX::WrapMode_t wrapT = GX::WrapMode_t(file->readByte());
    file->skip(0x01);

    GX::TexPalette_t paletteFormat = GX::TexPalette_t(file->readByte());
    uint16_t paletteCount = file->readShort();
    uint32_t paletteOffset = file->readInt();
    file->skip(0x04);

    GX::TexFilter_t minFilter = GX::TexFilter_t(file->readByte());
    GX::TexFilter_t magFilter = GX::TexFilter_t(file->readByte());

    float minLOD = file->readByte() / 8.f;
    float maxLOD = file->readByte() / 8.f;
    uint8_t mipCount = file->readByte();
    file->skip(0x01);

    float lodBias = file->readShort() / 100.f;
    uint32_t dataOffset = file->readInt();

    assert(minLOD == 0);

    std::span<const uint8_t> data;
    if(dataOffset != 0)
        data = std::span<const uint8_t>(file->getContents().data() + absoluteStartIndex + dataOffset, file->getLength() - (absoluteStartIndex + dataOffset));

    std::span<const uint8_t> paletteData;
    if(paletteOffset != 0)
        paletteData = std::span<const uint8_t>(file->getContents().data() + absoluteStartIndex + paletteOffset, absoluteStartIndex + paletteOffset + paletteCount * 2);

    return {
        name, format, width, height,
        wrapS, wrapT, minFilter, magFilter,
        minLOD, maxLOD, lodBias, mipCount,
        data, paletteFormat, paletteData
    };
}


std::vector<QString> BmdFile::readStringTable(uint32_t absoluteOffset)
{
    std::vector<QString> ret;

    file->position(absoluteOffset);
    uint16_t stringCount = file->readShort();
    uint32_t index = 0x04;
    for(uint32_t i = 0; i < stringCount; i++)
    {
        // const hash = view.getUint16(tableIdx + 0x00);
        file->position(absoluteOffset + index + 0x02);
        uint16_t stringOffset = file->readShort();

        file->position(absoluteOffset + stringOffset);
        QString string = file->readString(0, "UTF-8");
        ret.push_back(string);
        index += 0x04;
    }

    return ret;
}


float BmdFile::readArrayShort(uint8_t fixedPoint)
{
    short val = file->readShort();
    return (float)(val / (float)(1 << fixedPoint));
}

float BmdFile::readArrayFloat()
{
    return file->readFloat();
}

float BmdFile::readArrayValue(uint32_t type, uint8_t fixedPoint)
{
    if(type == 3)
        return readArrayShort(fixedPoint);
    if(type == 4)
        return readArrayFloat();

    return 0;
}

QColor BmdFile::readColor_RGBA8()
{
    int r = file->readByte() & 0xFF;
    int g = file->readByte() & 0xFF;
    int b = file->readByte() & 0xFF;
    int a = file->readByte() & 0xFF;
    return QColor(r, g, b, a);
}

QColor BmdFile::readColor_RGBX8()
{
    int r = file->readByte() & 0xFF;
    int g = file->readByte() & 0xFF;
    int b = file->readByte() & 0xFF;
    file->readByte();
    return QColor(qRgb(r, g, b));
}

QColor BmdFile::readColor_RGBA16()
{
    uint16_t r = file->readInt();
    uint16_t g = file->readInt();
    uint16_t b = file->readInt();
    uint16_t a = file->readInt();
    return QColor(qRgba(r, g, b, a));
}

QColor BmdFile::readColorValue(uint32_t type)
{
    switch (type)
    {
        case 1:
        case 2:
            return readColor_RGBX8();
        case 5:
            return readColor_RGBA8();
    }

    return QColor();
}

GX::ColorChannelControl BmdFile::readColorChannel(uint32_t absoluteColorChanTableOffset, uint16_t colorChanIndex) {
    if (colorChanIndex != 0xFFFF) {
        file->position(absoluteColorChanTableOffset + colorChanIndex * 0x08);
        bool lightingEnabled = file->readByte();
        //assert(lightingEnabled < 2);

        GX::ColorSrc_t matColorSource = GX::ColorSrc_t(file->readByte());
        uint8_t litMask = file->readByte();
        GX::DiffuseFunction_t diffuseFunction = GX::DiffuseFunction_t(file->readByte());

        uint8_t attnFn = file->readByte();

        GX::AttenuationFunction_t attenuationFunction;
        switch(attnFn)
        {
            case 0:
            case 2:
                attenuationFunction = GX::AttenuationFunction::NONE;
                break;
            case 1:
                attenuationFunction = GX::AttenuationFunction::SPEC;
                break;
            case 3:
                attenuationFunction = GX::AttenuationFunction::SPOT;
                break;
            default:
                assert(false); // invalid attnFn
        }

        GX::ColorSrc_t ambColorSource = GX::ColorSrc_t(file->readByte());

        return { lightingEnabled, matColorSource, ambColorSource, litMask, diffuseFunction, attenuationFunction };
    } else {
        bool lightingEnabled = false;
        GX::ColorSrc_t matColorSource = GX::ColorSrc::REG;
        uint8_t litMask = 0;
        GX::DiffuseFunction_t diffuseFunction = GX::DiffuseFunction::CLAMP;
        GX::AttenuationFunction_t attenuationFunction = GX::AttenuationFunction::NONE;
        GX::ColorSrc_t ambColorSource = GX::ColorSrc::REG;
        return { lightingEnabled, matColorSource, ambColorSource, litMask, diffuseFunction, attenuationFunction };
    }
}

glm::vec3 BmdFile::readVec3()
{
    return glm::vec3(
        file->readFloat(),
        file->readFloat(),
        file->readFloat()
    );
}
