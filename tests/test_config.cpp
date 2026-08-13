#include <gtest/gtest.h>
#include <fstream>

#include "rle4k_config.h"
#include "test_helpers.h"

TEST(Config, MapFormatId) {
    EXPECT_EQ(map_format_id(0), 0x4);
    EXPECT_EQ(map_format_id(1), 0x7);
    EXPECT_EQ(map_format_id(2), 0xB);
    EXPECT_EQ(map_format_id(3), 0xA);
    EXPECT_EQ(map_format_id(4), 0x8);
    EXPECT_EQ(map_format_id(5), 0xF);
    EXPECT_EQ(map_format_id(-1), -1);
    EXPECT_EQ(map_format_id(99), -1);
}

TEST(Config, JsonParsers) {
    const std::string json = R"({
        "input": "a.dif",
        "mode": 1,
        "repeat": 5,
        "pitch_width_um": 0.25,
        "formats": [0, 2, 4],
        "verbose": "strip",
        // comment line
        "gzip_level": 9
    })";

    EXPECT_EQ(json_get_str(json, "input"), "a.dif");
    EXPECT_EQ(json_get_str(json, "missing"), "");
    EXPECT_EQ(json_get_int(json, "mode"), 1);
    EXPECT_EQ(json_get_int(json, "absent", 42), 42);
    EXPECT_DOUBLE_EQ(json_get_double(json, "pitch_width_um"), 0.25);
    EXPECT_EQ(json_get_str(json, "verbose"), "strip");

    auto fmts = json_get_int_array(json, "formats");
    ASSERT_EQ(fmts.size(), 3u);
    EXPECT_EQ(fmts[0], 0);
    EXPECT_EQ(fmts[1], 2);
    EXPECT_EQ(fmts[2], 4);
    EXPECT_TRUE(json_get_int_array(json, "nope").empty());
}

TEST(Config, LoadConfigFile) {
    auto path = rle4k_test::temp_dir() / "cfg.json";
    {
        std::ofstream f(path);
        f << rle4k_builtin_default_config_json();
    }
    bench_config cfg;
    ASSERT_TRUE(load_config(path.string(), cfg));
    EXPECT_EQ(cfg.repeat, 3);
    EXPECT_EQ(cfg.formats.size(), 6u);
    EXPECT_EQ(cfg.verbose, "file");
    EXPECT_EQ(cfg.check_enabled, 0);
    EXPECT_EQ(cfg.cache_blocks_min, 32);
    EXPECT_EQ(cfg.cache_blocks_max, 128);
}

TEST(Config, LoadMissingReturnsFalse) {
    bench_config cfg;
    EXPECT_FALSE(load_config("Z:/no_such_rle4k_cfg.json", cfg));
}

TEST(Config, WriteAndEnsureDefault) {
    auto dir = rle4k_test::temp_dir() / "cfg_write";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto dest = (dir / "redcli.json").string();

    ASSERT_TRUE(rle4k_write_default_config(dest));
    EXPECT_TRUE(std::filesystem::exists(dest));

    bench_config cfg;
    ASSERT_TRUE(load_config(dest, cfg));
    EXPECT_EQ(cfg.block_line, 512);

    std::string out;
    /* ensure finds existing */
    auto cwd = std::filesystem::current_path();
    std::filesystem::current_path(dir);
    EXPECT_TRUE(rle4k_ensure_config_file(out));
    EXPECT_FALSE(out.empty());
    std::filesystem::current_path(cwd);
}

TEST(Config, BuiltinJsonNonEmpty) {
    const char* j = rle4k_builtin_default_config_json();
    ASSERT_NE(j, nullptr);
    EXPECT_NE(std::string(j).find("\"formats\""), std::string::npos);
}

TEST(Config, SearchDirsNonEmpty) {
    auto dirs = rle4k_config_search_dirs();
    EXPECT_FALSE(dirs.empty());
    EXPECT_FALSE(rle4k_exe_directory().empty());
}
