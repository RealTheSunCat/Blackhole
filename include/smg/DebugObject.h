#pragma once

#include "smg/BaseObject.h"

class DebugObject : public BaseObject
{
public:
    DebugObject(Zone& zone, const QString& dir, const QString& layer, const QString& fileName, BcsvFile::Entry& entry);

    DebugObject(Zone& zone, const QString& dir, const QString& layer, const QString& fileName, glm::vec3 pos);

    virtual int save() override;

    virtual ~DebugObject();
};
