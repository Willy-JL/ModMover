set_xmakever("2.5.1")

set_languages("cxx20")
set_arch("x64")

add_requires("jsoncons")

add_rules("mode.debug","mode.releasedbg", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

if is_mode("debug") then
    set_optimize("none")
elseif is_mode("releasedbg") then
    set_optimize("fastest")
elseif is_mode("release") then
    add_defines("NDEBUG")
    set_optimize("fastest")
end

add_cxflags("/bigobj", "/MP")
add_defines("UNICODE")

target("ModMover")
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX", "WINVER=0x0601")
    set_kind("shared")
    set_filename("ModMover.asi")
    add_files("./**.cpp")
    add_includedirs("./")
    add_syslinks("User32", "Version")
    add_packages("jsoncons")
    on_package(function(target)
        os.mkdir("package/bin/x64/plugins")
        os.cp(target:targetfile(), "package/bin/x64/plugins/")
        exclusions = io.open("package/bin/x64/plugins/ModMover_exclusions.txt", "w")
        exclusions:close()
        readme = io.open("package/bin/x64/plugins/ModMover_README.txt", "w")
        readme:write("ModMover will move incorrectly installed mods (only from inside the CP77 game folder) to the correct place at runtime and optionally (config file) move them back when the game is closed\nWill work great with Vortex if the mod authors package their mods recklessly\nCurrently supported: .archive and .reds mods\n\nAlgorithm:\n    Any match with an entry from the exclusions will be categorically ignored\n    .archive files will be moved:\n        Never if directly inside archive/pc/content\n        Never if directly inside archive/pc/mod\n        Always otherwise (inclusing archive/pc/patch)\n    .reds files will be moved:\n        Never if inside r6/scripts\n        Always otherwise\n\nHow to use exclusions:\n        One exclusion per line\n        Empty lines are ignored\n        No regex / wildcards, just simple string match\n        Matches full file path, meaning for example that a \"C:\" or \"Program Files\" exclusion is not a good idea\n        For folder separators both \"\\\" and \"/\" will work\n")
        readme:close()
        config = io.open("package/bin/x64/plugins/ModMover_config.json", "w")
        config:write("{\n    \"restore_after_game_closed\" : true,\n    \"use_exclusions\": true,\n    \"enabled_mod_types\": {\n        \".archive\": true,\n        \".reds\": true\n    }\n}")
        config:close()
    end)