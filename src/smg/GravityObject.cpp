#include "smg/GravityObject.h"

#include "ui/Blackhole.h"

GravityObject::GravityObject(Zone& zone, const QString& dir, const QString& layer, const QString& fileName, BcsvFile::Entry& entry)
        : BaseObject(zone, dir, layer, fileName, entry)
{
    m_scl = glm::vec3(
        m_data.getf("scale_x"),
        m_data.getf("scale_y"),
        m_data.getf("scale_z")
    );
}

GravityObject::GravityObject(Zone& zone, const QString& dir, const QString& layer, const QString& fileName, glm::vec3 pos)
        : BaseObject(zone, dir, layer, fileName, pos)
{

    m_data.insert("scale_x", m_scl.x);
    m_data.insert("scale_y", m_scl.y);
    m_data.insert("scale_z", m_scl.z);

    m_data.insert("Range", -1.0f);
    m_data.insert("Distant", 0.0f);
    m_data.insert("Priority", uint32_t(0));
    m_data.insert("Inverse", uint32_t(0));
    m_data.insert("Power", "Normal");
    m_data.insert("Gravity_type", "Normal");

    m_data.insert("Obj_arg0", uint32_t(-1));
    m_data.insert("Obj_arg1", uint32_t(-1));
    m_data.insert("Obj_arg2", uint32_t(-1));
    m_data.insert("Obj_arg3", uint32_t(-1));

    m_data.insert("SW_APPEAR", uint32_t(-1));
    m_data.insert("SW_DEAD", uint32_t(-1));
    m_data.insert("SW_A", uint32_t(-1));
    m_data.insert("SW_B", uint32_t(-1));


    m_data.insert("FollowId", uint32_t(-1));
    m_data.insert("ShapeModelNo", uint16_t(-1));
    m_data.insert("CommonPath_ID", uint16_t(-1));
    m_data.insert("ClippingGroupId", uint16_t(-1));
    m_data.insert("GroupId", uint16_t(-1));
    m_data.insert("DemoGroupId", uint16_t(-1));
    m_data.insert("MapParts_ID", uint16_t(-1));
    m_data.insert("Obj_ID", uint16_t(-1));


    if (g_gameType == 1)
    {
        m_data.insert("SW_SLEEP", uint32_t(-1));
        m_data.insert("ChildObjId", uint16_t(-1));
    }
    if (g_gameType == 2) {
        m_data.insert("SW_AWAKE", uint32_t(-1));
    }
}

int GravityObject::save()
{
    m_data.insert("name", m_name);

    m_data.insert("pos_x", m_pos.x);
    m_data.insert("pos_y", m_pos.y);
    m_data.insert("pos_z", m_pos.z);

    m_data.insert("dir_x", m_rot.x);
    m_data.insert("dir_y", m_rot.y);
    m_data.insert("dir_z", m_rot.z);

    m_data.insert("scale_x", m_scl.x);
    m_data.insert("scale_y", m_scl.y);
    m_data.insert("scale_z", m_scl.z);

    // TODO shouldn't we save the other data, too?

    return 0;
}

GravityObject::~GravityObject()
{ }
