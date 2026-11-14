#ifndef PLAYERBOTS_PLUS_REPORT_H
#define PLAYERBOTS_PLUS_REPORT_H

// --------------------------------------------------------------------------
// What the module tells you, two ways (PlayerbotsPlus.Report.*).
//
// In game, a few short lines: .pbp status for all of it -- whether each part
// is working, and what looks wrong -- and .pbp hangouts, dungeons,
// groupquests and economy for one part each. Every counter, spot, run and
// party goes to a file instead: Report.File in the server's logs folder,
// written every Report.Minutes and whenever .pbp report asks. That is the
// one to read when something is wrong, or to send on.
//
// World thread only.
// --------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

// A part of the module in brief: a few short lines for the chat window, and
// what looks wrong with it, if anything (short, one thing each).
struct PbpBrief
{
    std::vector<std::string> lines;
    std::vector<std::string> issues;
};

// .pbp status: a line for each part, then what looks wrong.
std::vector<std::string> PbpReport_Overview();

// The whole report written to the file now: where it went and how many
// lines. False: it could not be written (`where` says why).
bool PbpReport_Write(std::string& where, uint32_t& lines);

// Every world tick: the file written again every Report.Minutes.
void PbpReport_Update(uint32_t diff);

// Where the file is, for telling people.
std::string PbpReport_Path();

// A line for chat, a line for a file: the colour codes out (|cffrrggbb, |r).
std::string PbpReport_Plain(std::string const& line);

#endif // PLAYERBOTS_PLUS_REPORT_H
