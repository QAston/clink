// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/globber.h>
#include <core/os.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/str.h>
#include <process/process.h>
#include <ntverp.h> // for VER_PRODUCTMAJORVERSION to deduce SDK version

//------------------------------------------------------------------------------
extern setting_bool g_glob_hidden;
extern setting_bool g_glob_system;



//------------------------------------------------------------------------------
static const char* get_string(lua_State* state, int index)
{
    if (lua_gettop(state) < index || !lua_isstring(state, index))
        return nullptr;

    return lua_tostring(state, index);
}

//------------------------------------------------------------------------------
/// -name:  os.chdir
/// -arg:   path:string
/// -ret:   boolean
/// Changes the current directory to <span class="arg">path</span> and returns
/// whether it was successful.
int set_current_dir(lua_State* state)
{
    bool ok = false;
    if (const char* dir = get_string(state, 1))
        ok = os::set_current_dir(dir);

    lua_pushboolean(state, (ok == true));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.getcwd
/// -ret:   string
/// Returns the current directory.
int get_current_dir(lua_State* state)
{
    str<288> dir;
    os::get_current_dir(dir);

    lua_pushlstring(state, dir.c_str(), dir.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.mkdir
/// -arg:   path:string
/// -ret:   boolean
/// Creates the directory <span class="arg">path</span> and returns whether it
/// was successful.
static int make_dir(lua_State* state)
{
    bool ok = false;
    if (const char* dir = get_string(state, 1))
        ok = os::make_dir(dir);

    lua_pushboolean(state, (ok == true));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.rmdir
/// -arg:   path:string
/// -ret:   boolean
/// Removes the directory <span class="arg">path</span> and returns whether it
/// was successful.
static int remove_dir(lua_State* state)
{
    bool ok = false;
    if (const char* dir = get_string(state, 1))
        ok = os::remove_dir(dir);

    lua_pushboolean(state, (ok == true));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.isdir
/// -arg:   path:string
/// -ret:   boolean
/// Returns whether <span class="arg">path</span> is a directory.
int is_dir(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    lua_pushboolean(state, (os::get_path_type(path) == os::path_type_dir));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.isfile
/// -arg:   path:string
/// -ret:   boolean
/// Returns whether <span class="arg">path</span> is a file.
static int is_file(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    lua_pushboolean(state, (os::get_path_type(path) == os::path_type_file));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.ishidden
/// -arg:   path:string
/// -ret:   boolean
/// Returns whether <span class="arg">path</span> has the hidden attribute set.
static int is_hidden(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    lua_pushboolean(state, os::is_hidden(path));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.unlink
/// -arg:   path:string
/// -ret:   boolean
/// Deletes the file <span class="arg">path</span> and returns whether it was
/// successful.
static int unlink(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    if (os::unlink(path))
    {
        lua_pushboolean(state, 1);
        return 1;
    }

    lua_pushnil(state);
    lua_pushliteral(state, "error");
    lua_pushinteger(state, 1);
    return 3;
}

//------------------------------------------------------------------------------
/// -name:  os.move
/// -arg:   src:string
/// -arg:   dest:string
/// -ret:   boolean
/// Moves the <span class="arg">src</span> file to the
/// <span class="arg">dest</span> file.
static int move(lua_State* state)
{
    const char* src = get_string(state, 1);
    const char* dest = get_string(state, 2);
    if (src != nullptr && dest != nullptr && os::move(src, dest))
    {
        lua_pushboolean(state, 1);
        return 1;
    }

    lua_pushnil(state);
    lua_pushliteral(state, "error");
    lua_pushinteger(state, 1);
    return 3;
}

//------------------------------------------------------------------------------
/// -name:  os.copy
/// -arg:   src:string
/// -arg:   dest:string
/// -ret:   boolean
/// Copies the <span class="arg">src</span> file to the
/// <span class="arg">dest</span> file.
static int copy(lua_State* state)
{
    const char* src = get_string(state, 1);
    const char* dest = get_string(state, 2);
    if (src == nullptr || dest == nullptr)
        return 0;

    lua_pushboolean(state, (os::copy(src, dest) == true));
    return 1;
}

//------------------------------------------------------------------------------
static void add_type_tag(str_base& out, const char* tag)
{
    if (out.length())
        out << ",";
    out << tag;
}

//------------------------------------------------------------------------------
int glob_impl(lua_State* state, bool dirs_only, bool back_compat=false)
{
    const char* mask = get_string(state, 1);
    if (mask == nullptr)
        return 0;

    bool extrainfo = back_compat ? false : lua_toboolean(state, 2);

    lua_createtable(state, 0, 0);

    globber globber(mask);
    globber.files(!dirs_only);
    globber.hidden(g_glob_hidden.get());
    globber.system(g_glob_system.get());
    if (back_compat)
        globber.suffix_dirs(false);

    int i = 1;
    str<288> file;
    str<16> type;
    int attr;
    while (globber.next(file, false, nullptr, &attr))
    {
        if (back_compat)
        {
            lua_pushlstring(state, file.c_str(), file.length());
        }
        else
        {
            lua_createtable(state, 0, 2);

            lua_pushliteral(state, "name");
            lua_pushlstring(state, file.c_str(), file.length());
            lua_rawset(state, -3);

            type.clear();
            add_type_tag(type, (attr & FILE_ATTRIBUTE_DIRECTORY) ? "dir" : "file");
            if (attr & FILE_ATTRIBUTE_HIDDEN)
                add_type_tag(type, "hidden");
            if (attr & FILE_ATTRIBUTE_READONLY)
                add_type_tag(type, "readonly");
            lua_pushliteral(state, "type");
            lua_pushlstring(state, type.c_str(), type.length());
            lua_rawset(state, -3);
        }

        lua_rawseti(state, -2, i++);
    }

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.globdirs
/// -arg:   globpattern:string
/// -arg:   [extrainfo:boolean]
/// -ret:   table
/// Collects directories matching <span class="arg">globpattern</span> and
/// returns them in a table of strings.
///
/// When <span class="arg">extrainfo</span> is true, then the returned table has
/// the following scheme:
/// <span class="tablescheme">{ {name:string, type:string}, ... }</span>.
///
/// The <span class="tablescheme">type</span> string can be "file" or "dir", and
/// may also contain ",hidden" and ",readonly" depending on the attributes
/// (making it usable as a match type for
/// <a href="#builder:addmatch">builder:addmatch()</a>).
///
/// Note: any quotation marks (<code>"</code>) in
/// <span class="arg">globpattern</span> are stripped.
int glob_dirs(lua_State* state)
{
    return glob_impl(state, true);
}

//------------------------------------------------------------------------------
/// -name:  os.globfiles
/// -arg:   globpattern:string
/// -arg:   [extrainfo:boolean]
/// -ret:   table
/// Collects files and/or directories matching
/// <span class="arg">globpattern</span> and returns them in a table of strings.
///
/// When <span class="arg">extrainfo</span> is true, then the returned table has
/// the following scheme:
/// <span class="tablescheme">{ {name:string, type:string}, ... }</span>.
///
/// The <span class="tablescheme">type</span> string can be "file" or "dir", and
/// may also contain ",hidden" and ",readonly" depending on the attributes
/// (making it usable as a match type for
/// <a href="#builder:addmatch">builder:addmatch()</a>).
///
/// Note: any quotation marks (<code>"</code>) in
/// <span class="arg">globpattern</span> are stripped.
int glob_files(lua_State* state)
{
    return glob_impl(state, false);
}

//------------------------------------------------------------------------------
/// -name:  os.getenv
/// -arg:   name:string
/// -ret:   string | nil
/// Returns the value of the named environment variable, or nil if it doesn't
/// exist.
///
/// Note: <code>os.getenv("HOME")</code> receives special treatment: if %HOME%
/// is not set then it is synthesized from %HOMEDRIVE% and %HOMEPATH%, or
/// from %USERPROFILE%.
int get_env(lua_State* state)
{
    const char* name = get_string(state, 1);
    if (name == nullptr)
        return 0;

    str<128> value;
    if (!os::get_env(name, value))
        return 0;

    lua_pushlstring(state, value.c_str(), value.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.setenv
/// -arg:   name:string
/// -arg:   value:string
/// -ret:   boolean
/// Sets the <span class="arg">name</span> environment variable to
/// <span class="arg">value</span> and returns whether it was successful.
int set_env(lua_State* state)
{
    const char* name = get_string(state, 1);
    const char* value = get_string(state, 2);
    if (name == nullptr)
        return 0;

    bool ok = os::set_env(name, value);
    lua_pushboolean(state, (ok == true));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.getenvnames
/// -ret:   table
/// Returns all environment variables in a table with the following scheme:
/// <span class="tablescheme">{ {name:string, value:string}, ... }</span>.
int get_env_names(lua_State* state)
{
    lua_createtable(state, 0, 0);

    WCHAR* root = GetEnvironmentStringsW();
    if (root == nullptr)
        return 1;

    str<128> var;
    WCHAR* strings = root;
    int i = 1;
    while (*strings)
    {
        // Skip env vars that start with a '='. They're hidden ones.
        if (*strings == '=')
        {
            strings += wcslen(strings) + 1;
            continue;
        }

        WCHAR* eq = wcschr(strings, '=');
        if (eq == nullptr)
            break;

        *eq = '\0';
        var = strings;

        lua_pushlstring(state, var.c_str(), var.length());
        lua_rawseti(state, -2, i++);

        ++eq;
        strings = eq + wcslen(eq) + 1;
    }

    FreeEnvironmentStringsW(root);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.gethost
/// -ret:   string
/// Returns the fully qualified file name of the host process.  Currently only
/// CMD.EXE can host Clink.
static int get_host(lua_State* state)
{
    WCHAR module[280];
    DWORD len = GetModuleFileNameW(nullptr, module, sizeof_array(module));
    if (!len)
        return 0;
    if (len == sizeof_array(module) && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        return 0;

    str<280> host;
    host = module;

    lua_pushlstring(state, host.c_str(), host.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.getalias
/// -arg:   name:string
/// -ret:   string
/// Returns command string for doskey alias <span class="arg">name</span>.
int get_alias(lua_State* state)
{
#if !defined(__MINGW32__) && !defined(__MINGW64__)
    const char* name = get_string(state, 1);
    if (name == nullptr)
        return 0;

    str<> out;
    if (!os::get_alias(name, out))
        return 0;

    lua_pushlstring(state, out.c_str(), out.length());
#endif // __MINGW32__
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.getaliases
/// -ret:   table
/// Returns doskey aliases in a table with the following scheme:
/// <span class="tablescheme">{ {name:string, command:string}, ... }</span>.
int get_aliases(lua_State* state)
{
    lua_createtable(state, 0, 0);

#if !defined(__MINGW32__) && !defined(__MINGW64__)
    str<280> path;
    if (!process().get_file_name(path))
        return 1;

    // Not const because Windows' alias API won't accept it.
    wstr<> name;
    name = (char*)path::get_name(path.c_str());

    // Get the aliases (aka. doskey macros).
    int buffer_size = GetConsoleAliasesLengthW(name.data());
    if (buffer_size == 0)
        return 1;

    wstr<> buffer;
    buffer.reserve(buffer_size);
    ZeroMemory(buffer.data(), buffer.size());   // Avoid race condition!
    if (GetConsoleAliasesW(buffer.data(), buffer.size(), name.data()) == 0)
        return 1;

    // Parse the result into a lua table.
    str<> out;
    WCHAR* alias = buffer.data();
    for (int i = 1; int(alias - buffer.data()) < buffer_size; ++i)
    {
        WCHAR* c = wcschr(alias, '=');
        if (c == nullptr)
            break;

        *c = '\0';
        out = alias;

        lua_pushlstring(state, out.c_str(), out.length());
        lua_rawseti(state, -2, i);

        ++c;
        alias = c + wcslen(c) + 1;
    }

#endif // __MINGW32__
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.getscreeninfo
/// -ret:   table
/// Returns dimensions of the terminal's buffer and visible window. The returned
/// table has the following scheme:
/// -show:  {
/// -show:  &nbsp; bufwidth,     -- [integer] width of the screen buffer
/// -show:  &nbsp; bufheight,    -- [integer] height of the screen buffer
/// -show:  &nbsp; winwidth,     -- [integer] width of the visible window
/// -show:  &nbsp; winheight,    -- [integer] height of the visible window
/// -show:  }
int get_screen_info(lua_State* state)
{
    int i;
    int buffer_width, buffer_height;
    int window_width, window_height;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    struct table_t {
        const char* name;
        int         value;
    };

    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    buffer_width = csbi.dwSize.X;
    buffer_height = csbi.dwSize.Y;
    window_width = csbi.srWindow.Right - csbi.srWindow.Left;
    window_height = csbi.srWindow.Bottom - csbi.srWindow.Top;

    lua_createtable(state, 0, 4);
    {
        struct table_t table[] = {
            { "bufwidth", buffer_width },
            { "bufheight", buffer_height },
            { "winwidth", window_width },
            { "winheight", window_height },
        };

        for (i = 0; i < sizeof_array(table); ++i)
        {
            lua_pushstring(state, table[i].name);
            lua_pushinteger(state, table[i].value);
            lua_rawset(state, -3);
        }
    }

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.getbatterystatus
/// -ret:   table
/// Returns a table containing the battery status for the device, or nil if an
/// error occurs.  The returned table has the following scheme:
/// -show:  {
/// -show:  &nbsp; level,         -- [integer] the battery life from 0 to 100, or -1 if an
/// -show:  &nbsp;                --            error occurred or there is no battery.
/// -show:  &nbsp; acpower,       -- [boolean] whether the device is connected to AC power.
/// -show:  &nbsp; charging,      -- [boolean] whether the battery is charging.
/// -show:  &nbsp; batterysaver,  -- [boolean] whether Battery Saver mode is active.
/// -show:  }
int get_battery_status(lua_State* state)
{
    SYSTEM_POWER_STATUS status;
    if (!GetSystemPowerStatus(&status))
        return 0;

    int level = -1;
    if (status.BatteryLifePercent <= 100)
        level = status.BatteryLifePercent;
    if (status.BatteryFlag & 128)
        level = -1;

    lua_createtable(state, 0, 4);

    lua_pushliteral(state, "level");
    lua_pushinteger(state, level);
    lua_rawset(state, -3);

    lua_pushliteral(state, "acpower");
    lua_pushboolean(state, (status.ACLineStatus == 1));
    lua_rawset(state, -3);

    lua_pushliteral(state, "charging");
    lua_pushboolean(state, ((status.BatteryFlag & 0x88) == 0x08));
    lua_rawset(state, -3);

    lua_pushliteral(state, "batterysaver");
#if defined( VER_PRODUCTMAJORVERSION ) && VER_PRODUCTMAJORVERSION >= 10
    lua_pushboolean(state, ((status.SystemStatusFlag & 1) == 1));
#else
    lua_pushboolean(state, ((status.Reserved1 & 1) == 1));
#endif
    lua_rawset(state, -3);

    return 1;
}

//------------------------------------------------------------------------------
void os_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "chdir",       &set_current_dir },
        { "getcwd",      &get_current_dir },
        { "mkdir",       &make_dir },
        { "rmdir",       &remove_dir },
        { "isdir",       &is_dir },
        { "isfile",      &is_file },
        { "ishidden",    &is_hidden },
        { "unlink",      &unlink },
        { "move",        &move },
        { "copy",        &copy },
        { "globdirs",    &glob_dirs },
        { "globfiles",   &glob_files },
        { "getenv",      &get_env },
        { "setenv",      &set_env },
        { "getenvnames", &get_env_names },
        { "gethost",     &get_host },
        { "getalias",    &get_alias },
        { "getaliases",  &get_aliases },
        { "getscreeninfo", &get_screen_info },
        { "getbatterystatus", &get_battery_status },
    };

    lua_State* state = lua.get_state();

    lua_getglobal(state, "os");

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_pop(state, 1);
}
