/*
 *  File:       luadgn.cc
 *  Summary:    Dungeon-builder Lua interface.
 *
 *  Modified for Crawl Reference by $Author: dshaligram $ on $Date: 2007-06-21T19:20:47.183838Z $
 */

#include "AppHdr.h"
#include "clua.h"
#include "files.h"
#include "luadgn.h"
#include "mapdef.h"
#include "stuff.h"
#include "dungeon.h"
#include <sstream>

// Lua interpreter for the dungeon builder.
CLua dlua(false);

template <typename list, typename lpush>
static int dlua_gentable(lua_State *ls, const list &strings, lpush push)
{
    lua_newtable(ls);
    for (int i = 0, size = strings.size(); i < size; ++i)
    {
        push(ls, strings[i]);
        lua_rawseti(ls, -2, i + 1);
    }
    return (1);
}

inline static void dlua_pushcxxstring(lua_State *ls, const std::string &s)
{
    lua_pushstring(ls, s.c_str());
}

int dlua_stringtable(lua_State *ls, const std::vector<std::string> &s)
{
    return dlua_gentable(ls, s, dlua_pushcxxstring);
}

///////////////////////////////////////////////////////////////////////////
// dlua_chunk

dlua_chunk::dlua_chunk(const std::string &_context)
    : file(), chunk(), compiled(), context(_context), first(-1),
      last(-1), error()
{
    clear();
}

void dlua_chunk::write(FILE *outf) const
{
    if (empty())
    {
        writeByte(outf, CT_EMPTY);
        return;
    }
    
    if (!compiled.empty())
    {
        writeByte(outf, CT_COMPILED);
        writeString(outf, compiled, LUA_CHUNK_MAX_SIZE);
    }
    else
    {
        writeByte(outf, CT_SOURCE);
        writeString(outf, chunk, LUA_CHUNK_MAX_SIZE);
    }
        
    writeString(outf, file);
    writeLong(outf, first);
}

void dlua_chunk::read(FILE *inf)
{
    clear();
    chunk_t type = static_cast<chunk_t>(readByte(inf));
    switch (type)
    {
    case CT_EMPTY:
        return;
    case CT_SOURCE:
        chunk = readString(inf, LUA_CHUNK_MAX_SIZE);
        break;
    case CT_COMPILED:
        compiled = readString(inf, LUA_CHUNK_MAX_SIZE);
        break;
    }
    file  = readString(inf);
    first = readLong(inf);
}

void dlua_chunk::clear()
{
    file.clear();
    chunk.clear();
    first = last = -1;
    error.clear();
    compiled.clear();
}

void dlua_chunk::set_file(const std::string &s)
{
    file = s;
}

void dlua_chunk::add(int line, const std::string &s)
{
    if (first == -1)
        first = line;

    if (line != last && last != -1)
        while (last++ < line)
            chunk += '\n';

    chunk += " ";
    chunk += s;
    last = line;
}

void dlua_chunk::set_chunk(const std::string &s)
{
    chunk = s;
}

int dlua_chunk::check_op(CLua &interp, int err)
{
    error = interp.error;
    return (err);
}

static int dlua_compiled_chunk_writer(lua_State *ls, const void *p,
                                      size_t sz, void *ud)
{
    std::ostringstream &out = *static_cast<std::ostringstream*>(ud);
    out.write((const char *) p, sz);
    return (0);
}

int dlua_chunk::load(CLua &interp)
{
    if (!compiled.empty())
        return check_op( interp,
                         interp.loadbuffer(compiled.c_str(), compiled.length(),
                                           context.c_str()) );

    if (empty())
    {
        chunk.clear();
        return (-1000);
    }
    
    int err = check_op( interp,
                        interp.loadstring(chunk.c_str(), context.c_str()) );
    if (err)
        return (err);
    std::ostringstream out;
    err = lua_dump(interp, dlua_compiled_chunk_writer, &out);
    if (err)
    {
        const char *e = lua_tostring(interp, -1);
        error = e? e : "Unknown error compiling chunk";
        lua_pop(interp, 2);
    }
    compiled = out.str();
    chunk.clear();
    return (err);
}

int dlua_chunk::load_call(CLua &interp, const char *fn)
{
    int err = load(interp);
    if (err == -1000)
        return (0);
    if (err)
        return (err);

    return check_op(interp, !interp.callfn(fn, fn? 1 : 0, 0));
}

std::string dlua_chunk::orig_error() const
{
    rewrite_chunk_errors(error);
    return (error);
}

bool dlua_chunk::empty() const
{
    return compiled.empty() && trimmed_string(chunk).empty();
}

bool dlua_chunk::rewrite_chunk_errors(std::string &s) const
{
    const std::string contextm = "[string \"" + context + "\"]:";
    std::string::size_type dlwhere = s.find(contextm);

    if (dlwhere == std::string::npos)
        return (false);

    if (!dlwhere)
    {
        s = rewrite_chunk_prefix(s);
        return (true);
    }

    // Our chunk is mentioned, go back through and rewrite lines.
    std::vector<std::string> lines = split_string("\n", s);
    std::string newmsg = lines[0];
    bool wrote_prefix = false;
    for (int i = 2, size = lines.size() - 1; i < size; ++i)
    {
        const std::string &st = lines[i];
        if (st.find(context) != std::string::npos)
        {
            if (!wrote_prefix)
            {
                newmsg = get_chunk_prefix(st) + ": " + newmsg;
                wrote_prefix = true;
            }
            else
                newmsg += "\n" + rewrite_chunk_prefix(st);
        }
    }
    s = newmsg;
    return (true);
}

std::string dlua_chunk::rewrite_chunk_prefix(const std::string &line) const
{
    std::string s = line;
    const std::string contextm = "[string \"" + context + "\"]:";
    const std::string::size_type ps = s.find(contextm);
    if (ps == std::string::npos)
        return (s);

    std::string::size_type pe = s.find(':', ps + contextm.length());
    if (pe != std::string::npos)
    {
        const std::string::size_type lns = ps + contextm.length();
        const std::string line_num = s.substr(lns, pe - lns);
        const int lnum = atoi(line_num.c_str());
        s = s.substr(0, lns) + make_stringf("%d", lnum + first - 1)
            + s.substr(pe);
    }

    return s.substr(0, ps) + (file.empty()? context : file) + ":"
        + s.substr(ps + contextm.length());
}

std::string dlua_chunk::get_chunk_prefix(const std::string &sorig) const
{
    std::string s = rewrite_chunk_prefix(sorig);
    const std::string::size_type cpos = s.find(':');
    if (cpos == std::string::npos)
        return (s);
    const std::string::size_type cnpos = s.find(':', cpos + 1);
    if (cnpos == std::string::npos)
        return (s);
    return s.substr(0, cnpos);
}

///////////////////////////////////////////////////////////////////////////
// Lua dungeon bindings (in the dgn table).

static depth_ranges dgn_default_depths;

#define MAP(ls, n, var)                             \
    map_def *var = *(map_def **) luaL_checkudata(ls, n, MAP_METATABLE)

void dgn_reset_default_depth()
{
    dgn_default_depths.clear();
}

std::string dgn_set_default_depth(const std::string &s)
{
    std::vector<std::string> frags = split_string(",", s);
    for (int i = 0, size = frags.size(); i < size; ++i)
    {
        try
        {
            dgn_default_depths.push_back( level_range::parse(frags[i]) );
        }
        catch (const std::string &error)
        {
            return (error);
        }
    }
    return ("");
}

static void dgn_add_depths(depth_ranges &drs, lua_State *ls, int s, int e)
{
    for (int i = s; i <= e; ++i)
    {
        const char *depth = luaL_checkstring(ls, i);
        std::vector<std::string> frags = split_string(",", depth);
        for (int j = 0, size = frags.size(); i < size; ++i)
        {
            try
            {
                dgn_default_depths.push_back( level_range::parse(frags[j]) );
            }
            catch (const std::string &error)
            {
                luaL_error(ls, error.c_str());
            }
        }
    }
}

static std::string dgn_depth_list_string(const depth_ranges &drs)
{
    return (comma_separated_line(drs.begin(), drs.end(), ", ", ", "));
}

static int dgn_depth_proc(lua_State *ls, depth_ranges &dr, int s)
{
    if (lua_gettop(ls) < s)
    {
        PLUARET(string, dgn_depth_list_string(dr).c_str());
    }
    
    if (lua_isnil(ls, s))
    {
        dr.clear();
        return (0);
    }

    dr.clear();
    dgn_add_depths(dr, ls, s, lua_gettop(ls));
    return (0);
}

static int dgn_default_depth(lua_State *ls)
{
    return dgn_depth_proc(ls, dgn_default_depths, 1);
}

static int dgn_depth(lua_State *ls)
{
    MAP(ls, 1, map);
    return dgn_depth_proc(ls, map->depths, 2);
}

static int dgn_place(lua_State *ls)
{
    MAP(ls, 1, map);
    if (lua_gettop(ls) > 1)
    {
        if (lua_isnil(ls, 2))
            map->place.clear();
        else
            map->place = luaL_checkstring(ls, 2);
    }
    PLUARET(string, map->place.c_str());
}

static int dgn_tags(lua_State *ls)
{
    MAP(ls, 1, map);
    if (lua_gettop(ls) > 1)
    {
        if (lua_isnil(ls, 2))
            map->tags.clear();
        else
        {
            const char *s = luaL_checkstring(ls, 2);
            map->tags += " " + trimmed_string(s) + " ";
        }
    }
    PLUARET(string, map->tags.c_str());
}

static int dgn_tags_remove(lua_State *ls)
{
    MAP(ls, 1, map);

    const int top = lua_gettop(ls);
    for (int i = 2; i <= top; ++i)
    {
        const std::string axee = luaL_checkstring(ls, i);
        const std::string::size_type pos = map->tags.find(axee);
        if (pos != std::string::npos)
            map->tags =
                map->tags.substr(0, pos)
                + map->tags.substr(pos + axee.length());
    }
    PLUARET(string, map->tags.c_str());
}

static int dgn_weight(lua_State *ls)
{
    MAP(ls, 1, map);
    if (!lua_isnil(ls, 2))
        map->chance = luaL_checkint(ls, 2);
    PLUARET(number, map->chance);
}

static int dgn_orient(lua_State *ls)
{
    MAP(ls, 1, map);
    if (lua_gettop(ls) > 1)
    {
        if (lua_isnil(ls, 2))
            map->orient = MAP_NONE;
        else
        {
            const std::string orient = luaL_checkstring(ls, 2);
            bool found = false;
            // Note: Empty string is intentionally mapped to MAP_NONE!
            for (int i = MAP_NONE; i < MAP_NUM_SECTION_TYPES; ++i)
            {
                if (orient == map_section_name(i))
                {
                    map->orient = static_cast<map_section_type>(i);
                    found = true;
                    break;
                }
            }
            if (!found)
                luaL_error(ls, ("Bad orient: " + orient).c_str());
        }
    }
    PLUARET(string, map_section_name(map->orient));
}

static int dgn_shuffle(lua_State *ls)
{
    MAP(ls, 1, map);
    if (lua_gettop(ls) == 1)
        return dlua_stringtable(ls, map->get_shuffle_strings());

    for (int i = 2, size = lua_gettop(ls); i <= size; ++i)
    {
        if (lua_isnil(ls, i))
            map->map.clear_shuffles();
        else
        {
            std::string err = map->map.add_shuffle(luaL_checkstring(ls, i));
            if (!err.empty())
                luaL_error(ls, err.c_str());
        }
    }

    return (0);
}

static int dgn_shuffle_remove(lua_State *ls)
{
    MAP(ls, 1, map);
    for (int i = 2, size = lua_gettop(ls); i <= size; ++i)
        map->map.remove_shuffle(luaL_checkstring(ls, i));
    return (0);
}

static int dgn_subst(lua_State *ls)
{
    MAP(ls, 1, map);
    if (lua_gettop(ls) == 1)
        return dlua_stringtable(ls, map->get_subst_strings());

    for (int i = 2, size = lua_gettop(ls); i <= size; ++i)
    {
        if (lua_isnil(ls, i))
            map->map.clear_substs();
        else
        {
            std::string err = map->map.add_subst(luaL_checkstring(ls, i));
            if (!err.empty())
                luaL_error(ls, err.c_str());
        }
    }

    return (0);    
}

static int dgn_subst_remove(lua_State *ls)
{
    MAP(ls, 1, map);
    for (int i = 2, size = lua_gettop(ls); i <= size; ++i)
        map->map.remove_subst(luaL_checkstring(ls, i));
    return (0);
}

static int dgn_map(lua_State *ls)
{
    MAP(ls, 1, map);
    if (lua_gettop(ls) == 1)
        return dlua_stringtable(ls, map->map.get_lines());

    if (lua_isnil(ls, 2))
    {
        map->map.clear();
        return (0);
    }

    if (lua_isstring(ls, 2))
    {
        map->map.add_line(luaL_checkstring(ls, 2));
        return (0);
    }

    std::vector<std::string> &lines = map->map.get_lines();
    int which_line = luaL_checkint(ls, 2);
    if (which_line < 0)
        which_line += (int) lines.size();
    if (lua_gettop(ls) == 2)
    {
        if (which_line < 0 || which_line >= (int) lines.size())
        {
            luaL_error(ls,
                       lines.empty()? "Map is empty"
                       : make_stringf("Line %d out of range (0-%u)",
                                      which_line,
                                      lines.size() - 1).c_str());
        }
        PLUARET(string, lines[which_line].c_str());
    }

    if (lua_isnil(ls, 3))
    {
        if (which_line >= 0 && which_line < (int) lines.size())
        {
            lines.erase(lines.begin() + which_line);
            PLUARET(boolean, true);
        }
        return (0);
    }
    
    const std::string newline = luaL_checkstring(ls, 3);
    if (which_line < 0)
        luaL_error(ls,
                   make_stringf("Index %d out of range", which_line).c_str());
    
    if (which_line < (int) lines.size())
    {
        lines[which_line] = newline;
        return (0);
    }

    lines.reserve(which_line + 1);
    lines.resize(which_line + 1, "");
    lines[which_line] = newline;
    return (0);
}

static int dgn_mons(lua_State *ls)
{
    MAP(ls, 1, map);
    if (lua_gettop(ls) == 1)
        return (0);

    if (lua_isnil(ls, 2))
    {
        map->mons.clear();
        return (0);
    }

    if (lua_isstring(ls, 2))
    {
        std::string err = map->mons.add_mons(luaL_checkstring(ls, 2));
        if (!err.empty())
            luaL_error(ls, err.c_str());
        return (0);
    }

    const int index = luaL_checkint(ls, 2);
    std::string err = map->mons.set_mons(index, luaL_checkstring(ls, 3));
    if (!err.empty())
        luaL_error(ls, err.c_str());
    return (0);
}

static int dgn_item(lua_State *ls)
{
    MAP(ls, 1, map);
    if (lua_gettop(ls) == 1)
        return (0);

    if (lua_isnil(ls, 2))
    {
        map->items.clear();
        return (0);
    }

    if (lua_isstring(ls, 2))
    {
        std::string err = map->items.add_item(luaL_checkstring(ls, 2));
        if (!err.empty())
            luaL_error(ls, err.c_str());
        return (0);
    }

    const int index = luaL_checkint(ls, 2);
    std::string err = map->items.set_item(index, luaL_checkstring(ls, 3));
    if (!err.empty())
        luaL_error(ls, err.c_str());
    return (0);
}

static int dgn_kfeat(lua_State *ls)
{
    MAP(ls, 1, map);
    std::string err = map->add_key_feat(luaL_checkstring(ls, 2));
    if (!err.empty())
        luaL_error(ls, err.c_str());
    return (0);
}

static int dgn_kmons(lua_State *ls)
{
    MAP(ls, 1, map);
    std::string err = map->add_key_mons(luaL_checkstring(ls, 2));
    if (!err.empty())
        luaL_error(ls, err.c_str());
    return (0);
}

static int dgn_kitem(lua_State *ls)
{
    MAP(ls, 1, map);
    std::string err = map->add_key_item(luaL_checkstring(ls, 2));
    if (!err.empty())
        luaL_error(ls, err.c_str());
    return (0);
}

static int dgn_name(lua_State *ls)
{
    MAP(ls, 1, map);
    PLUARET(string, map->name.c_str());
}

static int dgn_grid(lua_State *ls)
{
    const int x = luaL_checkint(ls, 1), y = luaL_checkint(ls, 2);
    if (!map_bounds(x, y))
        luaL_error(ls,
                   make_stringf("(%d,%d) is out of bounds (%d-%d,%d-%d)",
                                x, y,
                                X_BOUND_1, X_BOUND_2,
                                Y_BOUND_1, Y_BOUND_2).c_str());
    if (lua_isnumber(ls, 3))
        grd[x][y] = static_cast<dungeon_feature_type>(luaL_checkint(ls, 3));
    PLUARET(number, grd[x][y]);
}

static int dgn_points_connected(lua_State *ls)
{
    MAP(ls, 1, map);
    const int nargs = lua_gettop(ls);
    if (nargs < 5)
        return luaL_error(ls,
                          "Not enough points to test connectedness "
                          "(need at least two)");
    
    map_def::map_feature_finder feat_finder(*map);
    map_def::map_bounds_check bounds_checker(*map);
    flood_find<map_def::map_feature_finder, map_def::map_bounds_check>
        finder(feat_finder, bounds_checker);

    for (int i = 4; i < nargs; i += 2)
    {
        const coord_def c(luaL_checkint(ls, i),
                          luaL_checkint(ls, i + 1));
        finder.add_point(c);
    }

    const coord_def pos(luaL_checkint(ls, 2), luaL_checkint(ls, 3));
    const bool connected = finder.points_connected_from(pos);
    PLUARET(boolean, connected);
}

static void dlua_push_coord(lua_State *ls, const coord_def &c)
{
    lua_pushnumber(ls, c.x);
    lua_pushnumber(ls, c.y);
}

static int dgn_gly_point(lua_State *ls)
{
    MAP(ls, 1, map);
    coord_def c = map->find_first_glyph(*luaL_checkstring(ls, 2));
    if (c.x != -1 && c.y != -1)
    {
        dlua_push_coord(ls, c);
        return (2);
    }
    return (0);
}

static int dgn_gly_points(lua_State *ls)
{
    MAP(ls, 1, map);
    std::vector<coord_def> cs = map->find_glyph(*luaL_checkstring(ls, 2));

    for (int i = 0, size = cs.size(); i < size; ++i)
        dlua_push_coord(ls, cs[i]);
    return (cs.size() * 2);
}

static int dgn_original_map(lua_State *ls)
{
    MAP(ls, 1, map);
    if (map->original)
        clua_push_map(ls, map->original);
    else
        lua_pushnil(ls);
    return (1);
}

static const struct luaL_reg dgn_lib[] =
{
    { "default_depth", dgn_default_depth },
    { "name", dgn_name },
    { "depth", dgn_depth },
    { "place", dgn_place },
    { "tags",  dgn_tags },
    { "tags_remove", dgn_tags_remove },
    { "chance", dgn_weight },
    { "weight", dgn_weight },
    { "orient", dgn_orient },
    { "shuffle", dgn_shuffle },
    { "shuffle_remove", dgn_shuffle_remove },
    { "subst", dgn_subst },
    { "subst_remove", dgn_subst_remove },
    { "map", dgn_map },
    { "mons", dgn_mons },
    { "item", dgn_item },
    { "kfeat", dgn_kfeat },
    { "kitem", dgn_kitem },
    { "kmons", dgn_kmons },
    { "grid", dgn_grid },
    { "points_connected", dgn_points_connected },
    { "gly_point", dgn_gly_point },
    { "gly_points", dgn_gly_points },
    { "original_map", dgn_original_map },
    { NULL, NULL }
};

static int crawl_args(lua_State *ls)
{
    return dlua_stringtable(ls, SysEnv.cmd_args);
}

static const struct luaL_reg crawl_lib[] =
{
    { "args", crawl_args },
    { NULL, NULL }
};

void init_dungeon_lua()
{
    luaL_openlib(dlua, "dgn", dgn_lib, 0);
    // Add additional function to the Crawl module.
    luaL_openlib(dlua, "crawl", crawl_lib, 0);
    dlua.execfile("clua/dungeon.lua", true, true);
    luaopen_debug(dlua);
    luaL_newmetatable(dlua, MAP_METATABLE);
    lua_settop(dlua, 1);
}
