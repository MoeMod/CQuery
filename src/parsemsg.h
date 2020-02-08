/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//
//  parsemsg.h
//

#define ASSERT( x )

class BufferReader
{
public:
    BufferReader(const char *name, const void *buf, int size) :
            m_szMsgName(name), m_pBuf((uint8_t*)buf), m_iSize(size), m_iRead(0), m_bBad(false) {}
    BufferReader(const void *buf, int size) : BufferReader("not set", buf, size) {}


    template<typename T> T Read(void);

    int8_t ReadChar(void);
    uint8_t ReadByte(void);
    int16_t ReadShort(void);
    int16_t ReadWord(void);
    int32_t ReadLong(void); // no mistake here, we assume that long is 32 bit.
    char *ReadString(void);
    float ReadFloat(void);
    float ReadCoord(void);
    float ReadAngle(void);
    float ReadHiResAngle(void);
    bool Eof() const { return m_iRead >= m_iSize; }

private:
    const char *m_szMsgName;
    uint8_t *m_pBuf;
    size_t   m_iSize;
    size_t   m_iRead;
    bool     m_bBad;
};

template<typename T>
inline T BufferReader::Read(void)
{
    if (m_bBad)
        return -1;

    // don't go out of bounds
    if (m_iRead + sizeof(T) > m_iSize)
    {
        m_bBad = true;

        // may occur, but safe
        return -1;
    }

    if (sizeof(T) == 1)
        return m_pBuf[m_iRead++];

    T t = *(T*)(m_pBuf + m_iRead);
    m_iRead += sizeof(T);

    return t;
}


template<>
inline char* BufferReader::Read(void)
{
    static char string[2048];

    if (m_bBad)
        return (char*)""; // do not return NULL, may break strcpy's

    size_t l;
    for (l = 0; l < sizeof(string) - 1; l++)
    {
        if (m_iRead > m_iSize)
            break;

        int8_t c = ReadChar();
        if (c == -1 || c == 0)
            break;

        string[l] = c;
    }

    string[l] = 0;

    return string;

}

template<>
inline const char * BufferReader::Read(void)
{
    return Read<char *>();
}

template<>
inline float BufferReader::Read(void)
{
    union
    {
        unsigned char b[4];
        float f;
    } tr;

    if (m_bBad)
        return -1.0f;

    if (m_iRead + 4 > m_iSize)
    {
        m_bBad = true;
        return -1.0f;
    }

    for (int i = 0; i < 4; i++)
        tr.b[i] = m_pBuf[m_iRead + i];

    m_iRead += 4;

    return tr.f;
}

inline int8_t BufferReader::ReadChar(void)
{
    return Read<int8_t>();
}

inline uint8_t BufferReader::ReadByte(void)
{
    return Read<uint8_t>();
}

inline int16_t BufferReader::ReadShort(void)
{
    return Read<int16_t>();
}

inline int16_t BufferReader::ReadWord(void)
{
    return ReadShort();
}

inline int32_t BufferReader::ReadLong(void)
{
    return Read<int32_t>();
}

inline char *BufferReader::ReadString(void)
{
    return Read<char*>();
}

inline float BufferReader::ReadFloat(void)
{
    return Read<float>();
}

inline float BufferReader::ReadCoord(void)
{
    return ReadShort() * 0.125f;
}

inline float BufferReader::ReadAngle(void)
{
    return ReadChar() * 360.0f / 256.0f;
}

inline float BufferReader::ReadHiResAngle(void)
{
    return ReadShort() * 360.0f / 65536.0f;
}

/*
void BEGIN_READ( void *buf, int size );
int READ_CHAR( void );
int READ_BYTE( void );
int READ_SHORT( void );
int READ_WORD( void );
int READ_LONG( void );
float READ_FLOAT( void );
char* READ_STRING( void );
float READ_COORD( void );
float READ_ANGLE( void );
float READ_HIRESANGLE( void );
*/







