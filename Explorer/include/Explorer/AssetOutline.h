#ifndef REASSETEXPLORER_ASSETOUTLINE_H
#define REASSETEXPLORER_ASSETOUTLINE_H
#include <string>
#include <string_view>

#include "Core/LoadedGame.h"

class MessageIndex;
class RszTypeDatabase;

// One tab separated record per line:
//   N  depth  kind  name  detail  [key  [hidden]]   starts a node; hidden 1: the game starts it hidden
//   P  section  key  value            property of the preceding node
std::string OutlineAsset(const LoadedGame& game, const RszTypeDatabase* rsz, const std::string& pakPath,
                         const MessageIndex* messages = nullptr);

// One record per line:
//   L  id  name                                  each language, in file order
//   A  name  type                                each attribute (-1 empty, 0 int, 1 float, 2 string)
//   E  key  guid  hash  attributes...  texts...  each entry
// Tabs, newlines and backslashes inside values are escaped as \t, \n, \r and \\.
std::string MessageTable(const LoadedGame& game, const std::string& pakPath);
// "Unknown_<hash>" -> "Unknown_<hash>.msg.17" for formats the data identifies.
std::string IdentifyUnknown(const LoadedGame& game, const std::string& unknownPath);

// One record per line:
//   E  gui:N                                   element whose container owns the clips (outline key)
//   C  name  frames  loop  next  pattern       clip; frames <= 0 is a static state
//   T  name  root                              track: the element itself (root 1) or a child by name
//   V  attribute  component  attributeType  keys
// Keys are separated by \x1e; each is frame, interpolation, four hermite handles
// and the value, separated by \x1f.
std::string GuiClipTable(const LoadedGame& game, const std::string& pakPath);

// One record per line:
//   N  index  parent  name  group  summary  end     node; group 1 when it holds states
//   S  from  to  label                             transition between sibling states
//   T  group  state  condition                     state a group starts in
//   A  group  state  label                         transition from any state of the group
// Labels are the condition type and, in motion FSMs, the blend ("MotionEnd, CrossFade 20f").
std::string FsmGraph(const LoadedGame& game, const RszTypeDatabase& db, const std::string& pakPath);

#endif
