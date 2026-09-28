#include "desktop_shell/spotlight/providers/seeker_registry.hpp"

namespace eh::shell::seeker {

namespace {

struct Glyph {
  const char* keys;
  const char* ch;
};

constexpr Glyph kGlyphs[] = {
    {"smile happy face grin", "😄"}, {"laugh joy lol", "😂"}, {"wink", "😉"},
    {"heart love like", "❤️"}, {"broken heart heartbreak", "💔"}, {"star favorite", "⭐"},
    {"check tick yes correct done", "✅"}, {"cross no x wrong", "❌"}, {"warning alert", "⚠️"},
    {"question help", "?"},
    {"info", "ℹ️"}, {"plus add", "➕"}, {"minus subtract remove", "➖"},
    {"arrow up", "⬆️"}, {"arrow down", "⬇️"}, {"arrow left", "⬅️"}, {"arrow right", "➡️"},
    {"play", "▶️"}, {"pause", "⏸️"}, {"stop square", "⏹️"}, {"next skip forward", "⏭️"},
    {"prev previous back rewind", "⏮️"}, {"music note song", "🎵"}, {"notes", "🎶"},
    {"fire lit hot", "🔥"}, {"thumbs up approve yes", "👍"}, {"thumbs down deny no", "👎"},
    {"clap applause", "👏"}, {"wave hello hi", "👋"}, {"eyes look see", "👀"},
    {"thinking hmm", "🤔"}, {"crying sad tear", "😢"}, {"angry mad", "😠"},
    {"surprised wow", "😮"}, {"cool sunglasses", "😎"}, {"sleeping tired", "😴"},
    {"party celebration tada", "🎉"}, {"gift present", "🎁"}, {"trophy win", "🏆"},
    {"lightbulb idea", "💡"}, {"lock locked secure", "🔒"}, {"unlock open", "🔓"},
    {"key password", "🔑"}, {"gear settings cog", "⚙️"}, {"search magnifying find", "🔍"},
    {"bell notification", "🔔"}, {"calendar date", "📅"}, {"clock time", "🕐"},
    {"computer laptop", "💻"}, {"phone mobile", "📱"}, {"camera photo", "📷"},
    {"folder directory", "📁"}, {"file document", "📄"}, {"trash delete garbage", "🗑️"},
    {"link chain url", "🔗"}, {"globe world web", "🌐"}, {"sunny sun day", "☀️"},
    {"moon night", "🌙"}, {"cloud cloudy", "☁️"}, {"rain raining", "🌧️"},
    {"snow winter", "❄️"}, {"dog puppy pet", "🐶"}, {"cat kitten", "🐱"},
};

void glyphs_query(std::string_view q, std::vector<SpotlightHit>& out) {
  std::string needle(q);
  for (char& c : needle) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  }
  int added = 0;
  for (const auto& g : kGlyphs) {
    if (added >= 8) break;
    if (!needle.empty() && std::string(g.keys).find(needle) == std::string::npos) continue;
    SpotlightHit h;
    h.name = std::string(g.ch) + "  Copy glyph";
    h.genericName = "Glyphs";
    h.exec = std::string("wl-copy -- ") + g.ch;
    h.iconKey = "face-smile";
    h.score = 55;
    out.push_back(std::move(h));
    ++added;
  }
}

struct GlyphsReg {
  GlyphsReg() {
    Provider p;
    p.name = "glyphs";
    p.prefix = "emo";
    p.global = false;
    p.query = glyphs_query;
    register_provider(std::move(p));
  }
};

GlyphsReg g_glyphsReg;

}
}
