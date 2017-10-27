#pragma once 
#include <rapidjson/document.h>

static inline const char *json_str(rapidjson::Value const &obj, const char *member, const char *defaultValue = "")
{
    if (obj.IsObject()) {
        rapidjson::Value::ConstMemberIterator m = obj.FindMember(member);
        if (m != obj.MemberEnd() && m->value.IsString()) {
            return m->value.GetString();
        }
    }
    return defaultValue;
}

static inline double json_double(rapidjson::Value const &obj, const char *member, double defaultValue = 0.0)
{
    if (obj.IsObject()) {
        rapidjson::Value::ConstMemberIterator m = obj.FindMember(member);
        if (m != obj.MemberEnd() && m->value.IsNumber()) {
            return m->value.GetDouble();
        }
    }
    return defaultValue;
}

static inline void json_vec4(rapidjson::Value const &obj, const char *member, double out[4], double defaultValue = 0.0)
{
    if (obj.IsObject()) {
        rapidjson::Value::ConstMemberIterator m = obj.FindMember(member);
        if (m != obj.MemberEnd() && m->value.IsArray() && m->value.Size() == 4) {
            for (unsigned i = 0; i < 4; i++) {
                out[i] = m->value[i].IsNumber() ? m->value[i].GetDouble() : defaultValue;
            }
            return;
        }
    }
    for (unsigned i = 0; i < 4; i++) {
        out[i] = defaultValue;
    }
}

static inline rapidjson::Value const* json_obj(rapidjson::Value const &obj, const char *member)
{
    if (obj.IsObject()) {
        rapidjson::Value::ConstMemberIterator m = obj.FindMember(member);
        if (m != obj.MemberEnd()) {
            return &m->value;
        }
    }
    return NULL;    
}
