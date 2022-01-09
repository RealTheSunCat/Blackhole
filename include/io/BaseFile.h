#pragma once

#include "Constants.h"

#include <QByteArray>

#include <QString>

class BaseFile
{
protected:
    bool m_bigEndian = true;

    mutable uint32_t m_curPos = 0;

    QByteArray m_contents;
    bool m_modifiedFlag = false;
public:

    virtual void save() = 0;
    virtual void close() = 0; // without saving!

    // virtual void releaseStorage(); // TODO do we need this?

    virtual void setBigEndian(bool big);

    virtual uint32_t getLength() const;
    virtual void setLength(uint32_t length);

    virtual uint32_t position() const;
    virtual void position(uint32_t newPos);
    virtual void skip(uint32_t count);

    uint8_t readByte() const;
    uint16_t readShort() const;
    uint32_t readInt() const;
    float readFloat() const;
    QString readString(uint32_t length, const char* enc = "ASCII") const;
    QByteArray readBytes(uint32_t count) const;

    void writeByte(uint8_t val);
    void writeShort(uint16_t val);
    void writeInt(uint32_t val);
    void writeFloat(float val);
    int writeString(const QString& str, const char* enc = "ASCII");
    void writeBytes(QByteArray bytes);

    virtual QByteArray getContents() const;
    virtual void setContents(QByteArray bytes);
};