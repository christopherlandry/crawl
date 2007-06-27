#ifndef __CLUA_H__
#define __CLUA_H__

#include "AppHdr.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <cstdio>
#include <cstdarg>
#include <string>
#include <map>
#include <set>

#include "libutil.h"
#include "externs.h"

#ifndef CLUA_MAX_MEMORY_USE
#define CLUA_MAX_MEMORY_USE (6 * 1024)
#endif

class CLua;
class lua_call_throttle
{
public:
    lua_call_throttle(CLua *handle);
    ~lua_call_throttle();

    static CLua *find_clua(lua_State *ls);
    
private:
    CLua *lua;

    typedef std::map<lua_State *, CLua *> lua_clua_map;
    static lua_clua_map lua_map;
};

class CLua
{
public:
    CLua(bool managed = true);
    ~CLua();

    lua_State *state();

    operator lua_State * ()
    {
        return state();
    }

    void save(const char *filename);

    void setglobal(const char *name);
    void getglobal(const char *name);
    
    // Assigns the value on top of the stack to a unique name in the registry
    // and returns the name.
    std::string setuniqregistry();

    void setregistry(const char *name);
    void getregistry(const char *name);

    int loadbuffer(const char *buf, size_t size, const char *context);
    int loadstring(const char *str, const char *context);
    int execstring(const char *str, const char *context = "init.txt");
    int execfile(const char *filename, bool trusted = false,
                 bool die_on_fail = false);

    bool callbooleanfn(bool defval, const char *fn, const char *params, ...);
    bool callfn(const char *fn, int nargs, int nret = 1);
    bool callfn(const char *fn, const char *params, ...);
    void fnreturns(const char *params, ...);
    bool runhook(const char *hook, const char *params, ...);

    static int file_write(lua_State *ls);
    static int loadfile(lua_State *ls, const char *file,
                        bool trusted = false, bool die_on_fail = false);
    static bool is_path_safe(std::string file, bool trusted = false);

public:
    std::string error;

    // If managed_vm is set, we have to take steps to control badly-behaved
    // scripts.
    bool managed_vm;
    bool shutting_down;
    int throttle_unit_lines;
    int throttle_sleep_ms;
    int throttle_sleep_start, throttle_sleep_end;
    int n_throttle_sleeps;
    int mixed_call_depth;
    int lua_call_depth;
    int max_mixed_call_depth;
    int max_lua_call_depth;

    long memory_used;

    static const int MAX_THROTTLE_SLEEPS = 100;

private:
    lua_State *_state;
    typedef std::set<std::string> sfset;
    sfset sourced_files;
    unsigned long uniqindex;

private:
    void init_lua();
    void set_error(int err, lua_State *ls = NULL);
    void load_cmacro();
    void load_chooks();
    void init_throttle();

    void vfnreturns(const char *par, va_list va);

    bool proc_returns(const char *par) const;

    bool calltopfn(lua_State *ls, const char *format, va_list args, 
                   int retc = -1, va_list *fnr = NULL);

    int push_args(lua_State *ls, const char *format, va_list args,
                    va_list *cpto = NULL);
    int return_count(lua_State *ls, const char *format);

    struct CLuaSave
    {
        const char *filename;
        FILE *handle;

        FILE *get_file();
    };

    friend class lua_call_throttle;
};

class lua_text_pattern : public base_pattern
{
public:
    lua_text_pattern(const std::string &pattern);
    ~lua_text_pattern();

    bool valid() const;
    bool matches(const std::string &s) const;

    static bool is_lua_pattern(const std::string &s);

private:
    bool        translated;
    bool        isvalid;
    std::string pattern;
    std::string lua_fn_name;

    static unsigned long lfndx;

    bool translate() const;
    void pre_pattern(std::string &pat, std::string &fn) const;
    void post_pattern(std::string &pat, std::string &fn) const;

    static std::string new_fn_name();
};

extern CLua clua;

void lua_set_exclusive_item(const item_def *item = NULL);

#define LUAWRAP(name, wrapexpr) \
    static int name(lua_State *ls) \
    {   \
        wrapexpr; \
        return (0); \
    }

#define PLUARET(type, val) \
        lua_push##type(ls, val); \
        return (1);

#define LUARET1(name, type, val) \
    static int name(lua_State *ls) \
    { \
        lua_push##type(ls, val); \
        return (1); \
    }

#define LUARET2(name, type, val1, val2)  \
    static int name(lua_State *ls) \
    { \
        lua_push##type(ls, val1); \
        lua_push##type(ls, val2); \
        return (2); \
    }

template <class T>
inline static T *util_get_userdata(lua_State *ls, int ndx)
{
    return (lua_islightuserdata(ls, ndx))?
            static_cast<T *>( lua_touserdata(ls, ndx) )
          : NULL;
}

template <class T>
inline static T *clua_get_userdata(lua_State *ls, const char *mt)
{
    return static_cast<T*>( luaL_checkudata( ls, 1, mt ) );
}

std::string quote_lua_string(const std::string &s);

class map_def;
void clua_push_map(lua_State *ls, map_def *map);

#define MAP_METATABLE "dgn.mtmap"

#endif
