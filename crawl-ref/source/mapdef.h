/*
 * mapdef.h:
 * Header for map structures used by the level compiler.
 *
 * NOTE: When we refer to map, this could be a full map, filling an entire
 * level or a minivault that occupies just a portion of the level.
 */

#ifndef __MAPDEF_H__
#define __MAPDEF_H__

#include <string>
#include <vector>
#include <cstdio>

#include "luadgn.h"
#include "enum.h"
#include "externs.h"

struct raw_range
{
    branch_type branch;
    int shallowest, deepest;
    bool deny;
};

struct level_range
{
public:
    branch_type branch;
    int shallowest, deepest;
    bool deny;

public:
    level_range(const raw_range &range);
    level_range(branch_type br = BRANCH_MAIN_DUNGEON, int s = -1, int d = -1);

    void set(int s, int d = -1);
    void set(const std::string &branch, int s, int d) throw (std::string);
    
    void reset();
    bool matches(const level_id &) const;
    bool matches(int depth) const;

    void write(FILE *) const;
    void read(FILE *);

    bool valid() const;
    int span() const;

    static level_range parse(std::string lr) throw (std::string);
    
    std::string describe() const;
    std::string str_depth_range() const;

    bool operator == (const level_range &lr) const;

    operator raw_range () const;
    operator std::string () const
    {
        return describe();
    }

private:
    static void parse_partial(level_range &lr, const std::string &s)
        throw (std::string);
    static void parse_depth_range(const std::string &s, int *low, int *high)
        throw (std::string);
};

typedef std::pair<int,int> glyph_weighted_replacement_t;
typedef std::vector<glyph_weighted_replacement_t> glyph_replacements_t;

class map_lines;
class map_transformer
{
public:
    enum transform_type
    {
        TT_SHUFFLE,
        TT_SUBST
    };
    
public:
    virtual ~map_transformer() = 0;
    virtual void apply_transform(map_lines &map) = 0;
    virtual map_transformer *clone() const = 0;
    virtual transform_type type() const = 0;
    virtual std::string describe() const = 0;
};

class subst_spec : public map_transformer
{
public:
    subst_spec(int torepl, bool fix, const glyph_replacements_t &repls);

    int key() const
    {
        return (foo);
    }
    
    int value();
    
    void apply_transform(map_lines &map);
    map_transformer *clone() const;
    transform_type type() const;
    std::string describe() const;

    bool operator == (const subst_spec &other) const;

private:
    int foo;        // The thing to replace.
    bool fix;       // If true, the first replacement fixes the value.
    int frozen_value;
    
    glyph_replacements_t repl;
};

struct shuffle_spec : public map_transformer
{
    std::string shuffle;

    shuffle_spec(const std::string &spec)
        : shuffle(spec)
    {
    }
    
    void apply_transform(map_lines &map);
    map_transformer *clone() const;
    transform_type type() const;
    std::string describe() const;
    bool operator == (const shuffle_spec &other) const
    {
        return (shuffle == other.shuffle);
    }
};

class map_lines
{
public:
    map_lines();
    map_lines(const map_lines &);
    ~map_lines();

    map_lines &operator = (const map_lines &);

    void add_line(const std::string &s);
    std::string add_subst(const std::string &st);
    std::string add_shuffle(const std::string &s);
    void remove_shuffle(const std::string &s);
    void remove_subst(const std::string &s);
    void clear_shuffles();
    void clear_substs();

    void set_orientation(const std::string &s);

    int width() const;
    int height() const;

    int glyph(int x, int y) const;
    bool is_solid(int gly) const;
    
    bool solid_borders(map_section_type border);

    void apply_transforms();

    // Make all lines the same length.
    void normalise(char fillc = 'x');

    // Rotate 90 degrees either clockwise or anticlockwise
    void rotate(bool clockwise);
    void hmirror();
    void vmirror();

    void clear();

    const std::vector<std::string> &get_lines() const;
    std::vector<std::string> &get_lines();
    std::vector<std::string> get_shuffle_strings() const;
    std::vector<std::string> get_subst_strings() const;

    int operator () (const coord_def &c) const;
    
private:
    void init_from(const map_lines &map);
    void release_transforms();
    
    void resolve_shuffle(const std::string &shuffle);
    void subst(std::string &s, subst_spec &spec);
    void subst(subst_spec &);
    void check_borders();
    void clear_transforms(map_transformer::transform_type);
    std::string shuffle(std::string s);
    std::string block_shuffle(const std::string &s);
    std::string check_shuffle(std::string &s);
    std::string check_block_shuffle(const std::string &s);
    std::string clean_shuffle(std::string s);
    std::string parse_glyph_replacements(std::string s,
                                         glyph_replacements_t &gly);

    friend class subst_spec;
    friend class shuffle_spec;
    
private:
    std::vector<map_transformer *> transforms;
    std::vector<std::string> lines;
    int map_width;
    bool solid_north, solid_east, solid_south, solid_west;
    bool solid_checked;
};

struct mons_spec
{
    int  mid;
    int  monnum;              // The zombified monster for zombies, or head
                              // count for hydras.
    int  genweight, mlevel;
    bool fix_mons;
    bool generate_awake;

    mons_spec(int id = RANDOM_MONSTER, int num = 250,
              int gw = 10, int ml = 0,
              bool _fixmons = false, bool awaken = false)
        : mid(id), monnum(num), genweight(gw), mlevel(ml), fix_mons(_fixmons),
          generate_awake(awaken)
    {
    }
};

class mons_list
{
public:
    mons_list();

    void clear();

    mons_spec get_monster(int index);

    // Returns an error string if the monster is unrecognised.
    std::string add_mons(const std::string &s, bool fix_slot = false);
    std::string set_mons(int slot, const std::string &s);

    size_t size() const { return mons.size(); }

private:
    typedef std::vector<mons_spec> mons_spec_list;

    struct mons_spec_slot
    {
        mons_spec_list mlist;
        bool fix_slot;

        mons_spec_slot(const mons_spec_list &list, bool fix = false)
            : mlist(list), fix_slot(fix)
        {
        }

        mons_spec_slot()
            : mlist(), fix_slot(false)
        {
        }
    };

private:
    mons_spec mons_by_name(std::string name) const;
    void get_zombie_type(std::string s, mons_spec &spec) const;
    mons_spec get_hydra_spec(const std::string &name) const;
    mons_spec get_zombified_monster(const std::string &name,
                                    monster_type zomb) const;
    mons_spec_slot parse_mons_spec(std::string spec);
    mons_spec pick_monster(mons_spec_slot &slot);
    int fix_demon(int id) const;
    bool check_mimic(const std::string &s, int *mid, bool *fix) const;

private:
    std::vector< mons_spec_slot > mons;
    std::string error;
};

enum item_spec_type
{
    ISPEC_GOOD   = -2,
    ISPEC_SUPERB = -3
};

struct item_spec
{
    int genweight;
    
    object_class_type base_type;
    int sub_type;
    int allow_uniques;
    int level;
    int race;

    item_spec() : genweight(10), base_type(OBJ_RANDOM), sub_type(OBJ_RANDOM),
        allow_uniques(1), level(-1), race(MAKE_ITEM_RANDOM_RACE)
    {
    }
};
typedef std::vector<item_spec> item_spec_list;

class item_list
{
public:
    item_list() : items() { }

    void clear();

    item_spec get_item(int index);
    size_t size() const { return items.size(); }

    std::string add_item(const std::string &spec, bool fix = false);
    std::string set_item(int index, const std::string &spec);

private:
    struct item_spec_slot
    {
        item_spec_list ilist;
        bool fix_slot;

        item_spec_slot() : ilist(), fix_slot(false)
        {
        }
    };
    
private:
    item_spec item_by_specifier(const std::string &spec);
    item_spec_slot parse_item_spec(std::string spec);
    item_spec parse_single_spec(std::string s);
    void parse_raw_name(std::string name, item_spec &spec);
    void parse_random_by_class(std::string c, item_spec &spec);
    item_spec pick_item(item_spec_slot &slot);

private:
    std::vector<item_spec_slot> items;
    std::string error;
};

struct feature_spec
{
    int genweight;
    int feat;
    int shop;
    int trap;
    int glyph;

    feature_spec(int f, int wt = 10)
        : genweight(wt), feat(f), shop(-1),
          trap(-1), glyph(-1)
    { }
    feature_spec() : genweight(0), feat(0), shop(-1), trap(-1), glyph(-1) { }
};

typedef std::vector<feature_spec> feature_spec_list;
struct feature_slot
{
    feature_spec_list feats;
    bool fix_slot;

    feature_slot();
    feature_spec get_feat(int default_glyph);
};

struct keyed_mapspec
{
public:
    int          key_glyph;
    
    feature_slot feat;
    item_list    item;
    mons_list    mons;

public:
    keyed_mapspec();

    std::string set_feat(const std::string &s, bool fix);
    std::string set_mons(const std::string &s, bool fix);
    std::string set_item(const std::string &s, bool fix);

    feature_spec get_feat();
    mons_list &get_monsters();
    item_list &get_items();

private:
    std::string err;

private:
    void parse_features(const std::string &);
    feature_spec_list parse_feature(const std::string &s);
    feature_spec parse_shop(std::string s, int weight);
    feature_spec parse_trap(std::string s, int weight);
};

typedef std::map<int, keyed_mapspec> keyed_specs;

typedef std::vector<level_range> depth_ranges;

class map_def;
struct dlua_set_map
{
    dlua_set_map(map_def *map);
    ~dlua_set_map();
};

class map_def;
dungeon_feature_type map_feature(map_def *map, const coord_def &c, int rawfeat);

class map_def
{
public:
    std::string     name;
    std::string     tags;
    std::string     place;

    depth_ranges     depths;
    map_section_type orient;
    int              chance;

    map_lines       map;
    mons_list       mons;
    item_list       items;

    keyed_specs     keyspecs;

    dlua_chunk      prelude, main, validate, veto;

    map_def         *original;

private:
    // This map has been loaded from an index, and not fully realised.
    bool            index_only;
    long            cache_offset;
    std::string     file;

public:
    map_def();
    void init();
    void reinit();

    void load();

    std::vector<coord_def> find_glyph(int glyph) const;
    coord_def find_first_glyph(int glyph) const;
    
    void write_index(FILE *) const;
    void write_full(FILE *);

    void read_index(FILE *);
    void read_full(FILE *);

    void set_file(const std::string &s);
    std::string run_lua(bool skip_main);

    // Returns true if the validation passed.
    bool test_lua_validate();

    // Returns true if *not* vetoed, i.e., the map is good to go.
    bool test_lua_veto();

    std::string validate_map_def();

    void add_prelude_line(int line,  const std::string &s);
    void add_main_line(int line, const std::string &s);

    void hmirror();
    void vmirror();
    void rotate(bool clockwise);
    void normalise();
    void resolve();
    void fixup();

    bool is_usable_in(const level_id &lid) const;
    
    keyed_mapspec *mapspec_for_key(int key);

    bool has_depth() const;
    void add_depth(const level_range &depth);
    void add_depths(depth_ranges::const_iterator s,
                    depth_ranges::const_iterator e);
    
    std::string add_key_item(const std::string &s);
    std::string add_key_mons(const std::string &s);
    std::string add_key_feat(const std::string &s);
    
    bool can_dock(map_section_type) const;
    coord_def dock_pos(map_section_type) const;
    coord_def float_dock();
    coord_def float_place();
    coord_def float_random_place() const;

    bool is_minivault() const;
    bool has_tag(const std::string &tag) const;
    bool has_tag_prefix(const std::string &tag) const;

    std::vector<std::string> get_shuffle_strings() const;
    std::vector<std::string> get_subst_strings() const;

    int glyph_at(const coord_def &c) const;

public:
    struct map_feature_finder
    {
        map_def &map;
        map_feature_finder(map_def &map_) : map(map_) { }
        // This may actually modify the underlying map by fixing KFEAT:
        // feature slots, but that's fine by us.
        dungeon_feature_type operator () (const coord_def &c) const
        {
            return (map_feature(&map, c, -1));
        }
    };

    struct map_bounds_check
    {
        map_def &map;
        map_bounds_check(map_def &map_) : map(map_) { }
        bool operator () (const coord_def &c) const
        {
            return (c.x >= 0 && c.x < map.map.width()
                    && c.y >= 0 && c.y < map.map.height());
        }
    };
    
private:
    void write_depth_ranges(FILE *) const;
    void read_depth_ranges(FILE *);
    bool test_lua_boolchunk(dlua_chunk &);
    std::string rewrite_chunk_errors(const std::string &s) const;
    
    std::string add_key_field(
        const std::string &s,
        std::string (keyed_mapspec::*set_field)(
            const std::string &s, bool fixed));
};

std::string escape_string(std::string in, const std::string &toesc,
                          const std::string &escapewith);
const char *map_section_name(int msect);

#endif
