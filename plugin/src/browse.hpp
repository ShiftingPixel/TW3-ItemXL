// Tools for writing ItemXL YAML files: show, search, schema (browse.cpp; used by itemxl-cli).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "itemxl.hpp"
#include "version.hpp"

namespace itemxl {

// The definition files of the game and its DLC folders (no mods), and its texts in one language.
struct GameData {
    std::vector<std::unique_ptr<DefFile>> store;
    Index index;
    W3Strings strings;
    bool has_strings = false;
    bool load(const std::string& game, const std::string& dlc_dir, const std::string& lang);
    std::string shown_name(const Node& el) const;   // the name the player sees, "" when none
    // the definition files of a built bundle (mod00000_ItemXL) over the game's: what the game reads
    void overlay(const std::string& bundle_path);
};

struct Found {
    std::string tag, name, shown, category, file;
};

std::string show(GameData& g, const std::string& name, const std::string& tag = "");
std::vector<Found> search(GameData& g, const std::string& text, const std::string& tag = "");
std::string schema(GameData& g);
// A YAML file to start from: a new record based on an existing one ("" and error set when there is no such record).
std::string new_yaml(GameData& g, const std::string& name, const std::string& base, const std::string& tag,
                     std::string& error);

}  // namespace itemxl
