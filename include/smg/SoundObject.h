#pragma once

#include "smg/BaseObject.h"

class SoundObject : public BaseObject
{
public:
    SoundObject(Zone& zone, const QString& dir, const QString& layer, const QString& fileName, BcsvFile::Entry& entry);

    SoundObject(Zone& zone, const QString& dir, const QString& layer, const QString& fileName, glm::vec3 pos);

    virtual int save() override;

    virtual ~SoundObject();
};

